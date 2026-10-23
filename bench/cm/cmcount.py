# /// script
# requires-python = ">=3.10"
# dependencies = ["unicorn==2.1.4", "pyelftools==0.33"]
# ///
"""Instructions and estimated cycles per nanosemver call on an emulated Cortex-M.

Runs the same workloads through two images, base and new, each nanosemver.c linked
bare (bench/cm/link.ld) with whatever libgcc and newlib-nano routines it pulls in, and
prints them side by side. The corpus is bench/bench.cc's, generated identically, so
the two benchmarks rank the same inputs; --dump prints it for comparison.

Instruction counts are exact. Cycles come from a rough single-issue model with
zero-wait-state memory: good for ranking variants, not a datasheet figure.

usage: cmcount.py --cpu {m0,m4} --objdump PATH BASE.elf NEW.elf [--n N] [--filter S]
       cmcount.py --dump
"""

import argparse
import bisect
import re
import subprocess
import sys
import zlib

M64 = (1 << 64) - 1


# ------------------------------------------------------------------------ corpus --
# A transcription of bench/bench.cc's generators: same splitmix64, same draws, same
# order. Keep the two in step.


class Rng:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & M64
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n

    def pick(self, a):
        return a[self.below(len(a))]


TAGS = ["alpha", "beta", "rc", "dev", "pre", "next", "canary", "SNAPSHOT", "nightly",
        "preview", "insiders", "RC", "M", "b", "a"]


def num(r, major):
    p = r.below(100)
    if p < (60 if major else 40):
        return str(r.below(10))
    if p < (90 if major else 72):
        return str(10 + r.below(90))
    if p < 94:
        return str(100 + r.below(900))
    if p < 98:
        return str(1000 + r.below(99000))
    return str(20000101 + r.below(260000))


def hexs(r, n):
    return "".join("0123456789abcdef"[r.below(16)] for _ in range(n))


def core(r):
    s = num(r, True)
    s += "." + num(r, False)
    return s + "." + num(r, False)


def pre(r):
    p = r.below(100)
    s = r.pick(TAGS)
    if p < 25:
        pass
    elif p < 80:
        s += "." + str(r.below(10 if p < 70 else 200))
    elif p < 88:
        s += str(r.below(20))
    elif p < 94:
        s += "." + str(r.below(5))
        s += "." + str(r.below(30))
    elif p < 97:
        s += "." + str(20230101 + r.below(30000))
        s += "." + hexs(r, 7)
    else:
        s = str(r.below(10))
    return s


def build(r):
    k = r.below(6)
    if k == 0:
        return "build." + str(r.below(5000))
    if k == 1:
        return "sha." + hexs(r, 7)
    if k == 2:
        return "g" + hexs(r, 9)
    if k == 3:
        return str(20230101000000 + r.below(100000000))
    if k == 4:
        return "exp.sha." + hexs(r, 7)
    return "linux.x86-64"


def core_pre(r):
    s = core(r)
    return s + "-" + pre(r)


def core_build(r):
    s = core(r)
    return s + "+" + build(r)


def core_pre_build(r):
    s = core_pre(r)
    return s + "+" + build(r)


def long_one(r):
    k = r.below(4)
    s = core(r)
    if k == 0:
        return s + "+" + hexs(r, 40)
    if k == 2:
        return s + "-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay"
    s += "-" + r.pick(TAGS)
    if k == 1:
        return s + "." + hexs(r, 40)
    s += "." + str(r.below(100))
    s += "+" + str(20230101000000 + r.below(100000000))
    return s + "." + hexs(r, 40)


def mix_one(r):
    p = r.below(100)
    if p < 80:
        return core(r)
    if p < 94:
        return core_pre(r)
    if p < 98:
        return core_build(r)
    return core_pre_build(r)


def invalid_one(r):
    v = mix_one(r)
    k = r.below(8)
    if k < 5:
        return ["v" + v, "0" + v, v + " ", v[: v.find(".", v.find(".") + 1)], v + ".4"][k]
    s = core(r)
    if k == 5:
        return s + "-"
    s += "-" + r.pick(TAGS)
    return s + (".01" if k == 6 else "..1")


PARSE_SETS = [("core", 1, core), ("pre", 2, core_pre), ("build", 3, core_build),
              ("full", 4, core_pre_build), ("long", 5, long_one), ("mix", 6, mix_one),
              ("invalid", 7, invalid_one)]


def gen(seed, f, n):
    r = Rng(seed)
    return [f(r) for _ in range(n)]


# ------------------------------------------------------------------- cycle model --

COND = re.compile(r"(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)$")


def cost(mn, ops, taken, cpu):
    """Cycles for one instruction: ARMv6-M (m0) or ARMv7-M (m4) timings, no waits."""
    mn = mn.split(".")[0]
    branch = mn in ("b", "cbz", "cbnz") or (mn.startswith("b") and COND.sub("", mn) == "b")
    regs = (ops.count(",") + 1) if "{" in ops else 0
    pc = 2 if "pc" in ops else 0
    if mn == "bl":
        return 3
    if mn in ("bx", "blx"):
        return 2 if cpu == "m0" else 3
    if branch:
        return (2 if cpu == "m0" else 3) if taken else 1
    if mn in ("push", "stm", "stmia", "stmdb", "ldm", "ldmia"):
        return 1 + regs
    if mn == "pop":
        return 1 + regs + pc
    if cpu == "m4":
        if mn.startswith("it"):
            return 0  # folded into the next instruction
        if mn in ("ldrd", "strd"):
            return 3
        if mn in ("udiv", "sdiv"):
            return 7
        if mn.startswith("str"):
            return 1
    if mn.startswith(("ldr", "str")):
        return 2
    return 1


# ----------------------------------------------------------------------- machine --

RAM = 0x20000000
RAM_SIZE = 0x40000
STOP = 0x30000000
HDR = 24  # sizeof(nsv_semver_t) with 32-bit pointers
SRC = 12  # offsetof(nsv_semver_t, src)


class Machine:
    def __init__(self, elf_path, objdump, cpu):
        from elftools.elf.elffile import ELFFile
        from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
        from unicorn import arm_const as A

        self.A = A
        self.cpu = cpu
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M0 if cpu == "m0" else
                                  A.UC_CPU_ARM_CORTEX_M4)
        self.uc.mem_map(0, 0x40000)
        self.uc.mem_map(RAM, RAM_SIZE)
        self.uc.mem_map(STOP, 0x1000)
        self.sym = {}
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            for seg in elf.iter_segments():
                if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                    self.uc.mem_write(seg["p_paddr"], seg.data())
            for s in elf.get_section_by_name(".symtab").iter_symbols():
                if s.name and s["st_info"]["type"] == "STT_FUNC":
                    self.sym[s.name] = s["st_value"] & ~1
        self.funcs = sorted((a, n) for n, a in self.sym.items())
        self.insn = {}
        dis = subprocess.run([objdump, "-d", elf_path], capture_output=True, text=True,
                             check=True).stdout
        for line in dis.splitlines():
            m = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]{4}(?: [0-9a-f]{4})?\s+(\S+)\s*(.*)", line)
            if m:
                self.insn[int(m.group(1), 16)] = (m.group(2), m.group(3))
        self.count = self.cycles = 0
        self.prev = None
        self.uc.hook_add(UC_HOOK_CODE, self._step)

    def _step(self, uc, addr, size, _):
        if self.prev:
            pa, ps, pm, po = self.prev
            self.cycles += cost(pm, po, addr != pa + ps, self.cpu)
        mn, ops = self.insn.get(addr, ("?", ""))
        self.prev = (addr, size, mn, ops)
        self.count += 1

    def call(self, name, *args):
        A = self.A
        for reg, v in zip((A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2,
                           A.UC_ARM_REG_R3), args):
            self.uc.reg_write(reg, v & 0xFFFFFFFF)
        self.uc.reg_write(A.UC_ARM_REG_SP, RAM + RAM_SIZE - 16)
        self.uc.reg_write(A.UC_ARM_REG_LR, STOP | 1)
        self.prev = None
        c0, y0 = self.count, self.cycles
        self.uc.emu_start(self.sym[name] | 1, STOP)
        if self.prev:
            self.cycles += cost(self.prev[2], self.prev[3], True, self.cpu)
        rv = self.uc.reg_read(A.UC_ARM_REG_R0)
        return (rv - (1 << 32) if rv & 0x80000000 else rv), self.count - c0, self.cycles - y0

    def put(self, addr, data):
        self.uc.mem_write(addr, data)

    def get(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))


# --------------------------------------------------------------------- workloads --

STR = RAM + 0x100
OUT = RAM + 0x20
DST = RAM + 0x8000


def run(m, n, flt):
    """{workload: (insns per op, cycles per op, checksum)} for one image."""
    res = {}

    def keep(name, ti, tc, ops, ck):
        if not flt or flt in name:
            res[name] = (ti / ops, tc / ops, ck)

    def parse(s):
        m.put(STR, s)
        rv, i, c = m.call("nsv_parse", OUT, STR, len(s))
        return rv, i, c, m.get(OUT, HDR)

    # A version and its string at a fixed slot, src pointing at the string.
    def place(slot, s, hdr):
        base = RAM + 0x1000 + (slot * 0x1000)
        m.put(base + 0x100, s)
        m.put(base, hdr[:SRC] + (base + 0x100).to_bytes(4, "little") + hdr[SRC + 4:])
        return base

    parsed = {}
    for name, seed, f in PARSE_SETS:
        ti = tc = ck = 0
        ok = []
        for s in gen(seed, f, n):
            b = s.encode()
            rv, i, c, hdr = parse(b)
            ti += i
            tc += c
            ck = (ck * 31 + rv + (zlib.crc32(hdr[:SRC] + hdr[SRC + 4:]) if rv == 0 else 0)) & M64
            if rv == 0:
                ok.append((b, hdr))
        parsed[name] = ok
        keep("parse/" + name, ti, tc, n, ck)

    def cmp_set(name, pairs):
        ti = tc = ck = 0
        for (sa, ha), (sb, hb) in pairs:
            rv, i, c = m.call("nsv_cmp", place(0, sa, ha), place(1, sb, hb))
            ti += i
            tc += c
            ck = (ck * 3 + rv) & M64
        keep(name, ti, tc, len(pairs), ck)

    def pairs_of(v, seed):
        r = Rng(seed)
        out = []
        for _ in range(n):
            a = r.below(len(v))
            out.append((v[a], v[r.below(len(v))]))
        return out

    mix = parsed["mix"]
    cmp_set("cmp/random", pairs_of(mix, 8))
    pres = [(s.encode(), parse(s.encode())[3]) for s in gen(9, lambda r: "1.0.0-" + pre(r), n)]
    cmp_set("cmp/pre-random", pairs_of(pres, 9))
    cmp_set("cmp/equal-full", [(x, x) for x in parsed["full"]])
    cmp_set("cmp/equal-long", [(x, x) for x in parsed["long"]])

    def fmt_set(name, items, query):
        ti = tc = ck = 0
        for s, h in items:
            p = place(0, s, h)
            rv, i, c = m.call("nsv_format", 0 if query else DST, 0 if query else 256, p)
            if rv > 0 and not query:
                ck = (ck * 131 + zlib.crc32(m.get(DST, rv))) & M64
            ck = (ck * 3 + rv) & M64
            ti += i
            tc += c
        keep(name, ti, tc, len(items), ck)

    fmt_set("format/mix", mix, False)
    fmt_set("format/long", parsed["long"], False)
    fmt_set("format-len/mix", mix, True)

    ti = tc = ck = 0
    for s, h in mix:
        rv, i, c = m.call("nsv_copy", DST, 512, place(0, s, h))
        ti += i
        tc += c
        ck = (ck * 131 + zlib.crc32(m.get(DST + HDR, rv - HDR))) & M64
    keep("copy/mix", ti, tc, len(mix), ck)
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cpu", choices=["m0", "m4"])
    ap.add_argument("--objdump")
    ap.add_argument("--n", type=int, default=256, help="inputs per workload")
    ap.add_argument("--filter", default="")
    ap.add_argument("--dump", action="store_true", help="print the parse corpora")
    ap.add_argument("elfs", nargs="*", metavar="ELF")
    a = ap.parse_args()

    if a.dump:
        for name, seed, f in PARSE_SETS:
            for s in gen(seed, f, 1024):
                print(f"parse/{name}\t{s}")
        return 0
    if not a.cpu or not a.objdump or len(a.elfs) != 2:
        ap.error("--cpu, --objdump, BASE.elf and NEW.elf are required")

    base = run(Machine(a.elfs[0], a.objdump, a.cpu), a.n, a.filter)
    new = run(Machine(a.elfs[1], a.objdump, a.cpu), a.n, a.filter)
    print(f"{'workload':18s} {'base insn':>9s} {'new insn':>9s} {'base cyc':>9s} "
          f"{'new cyc':>9s}  {'x':>5s}")
    bad = False
    for k, (bi, bc, bk) in base.items():
        ni, nc, nk = new[k]
        bad |= bk != nk
        print(f"{k:18s} {bi:9.1f} {ni:9.1f} {bc:9.1f} {nc:9.1f}  {bc / nc:4.2f}x"
              f"{'' if bk == nk else '  CHECKSUM-MISMATCH'}")
    if bad:
        print("# *** CHECKSUM MISMATCH: base and new disagree; results are void. ***")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
