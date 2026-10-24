#!/usr/bin/env python3
"""Mutation testing for nanosemver.c: how much of it the unit tests actually pin down.

Makes one small change at a time to the code (a comparison flipped, a constant off by
one, an operator swapped, a statement dropped, ...), builds the unit tests against it
in both configurations, and runs them. A mutant the tests fail on is killed; one they
pass is a survivor, either a change the tests miss or one that changes nothing (an
equivalent mutant). Survivors are listed with their line, for a person to judge: a test
for the first, an entry in tests/mutate-equivalent.txt, with the reason, for the second.
Entries match on the mutation and the line's text, so they follow code that moves; one
that no longer matches anything is reported as stale. Any survivor or stale entry fails.

  tests/mutate.py BUILD-DIR [--jobs N] [--filter SUBSTR] [--list]

BUILD-DIR is a `make` build directory whose test objects are up to date, e.g.
build/cc-Release-64. CC and CXX come from the environment (cc / c++).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "nanosemver.c")
INT16 = ["-DNSV_INT_MAX=32767", "-DNSV_SOFT_DIV=1", "-DNSV_WIDE_CMP=0"]
TIMEOUT = 120
EQUIVALENT = "tests/mutate-equivalent.txt"
# The unit tests that take most of the time, as doctest filters (commas separate them).
SLOW = ",".join(["*every range of up to three*", "fuzz: generated inputs",
                 "*every byte at every position*", "*every string of up to six bytes*"])


def code_mask(text):
    """True for each character that is code: not a comment, string or char literal's
    insides, or preprocessor line."""
    mask = [True] * len(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and text[i:].lstrip(" ").startswith("#"):
            j = i
            while True:  # to the end of the line, and of any it continues onto
                j = text.find("\n", j)
                j = n if j < 0 else j
                if j == n or text[j - 1] != "\\":
                    break
                j += 1
            for k in range(i, j):
                mask[k] = False
            i = j
            continue
        line_start = c == "\n"
        if text.startswith("/*", i):
            j = text.find("*/", i) + 2
            for k in range(i, j):
                mask[k] = False
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            for k in range(i, j):
                mask[k] = False
            i = j
        elif c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, j):
                mask[k] = False
            i = j + 1
        else:
            i += 1
    return mask


# (name, pattern, replacements): each match of pattern in code becomes each replacement.
RULES = [
    ("==", r"==", ["!="]),
    ("!=", r"!=", ["=="]),
    ("<=", r"<=", ["<", "=="]),
    (">=", r">=", [">", "=="]),
    ("<", r"(?<![<-])<(?![<=])", ["<=", "!="]),
    (">", r"(?<![>-])>(?![>=])", [">=", "!="]),
    ("&&", r"&&", ["||"]),
    ("||", r"\|\|", ["&&"]),
    ("!", r"!(?!=)", [""]),
    ("++", r"\+\+", ["--", ""]),
    ("--", r"--", ["++", ""]),
    ("+", r"(?<=[\w)\]] )\+(?= [\w(])", ["-"]),
    ("-", r"(?<=[\w)\]] )-(?= [\w(])", ["+"]),
    ("*", r"(?<=[\w)\]] )\*(?= [\w(])", ["/"]),
    ("&", r"(?<=[\w)\]] )&(?= [\w(])", ["|"]),
    ("|", r"(?<=[\w)\]] )\|(?= [\w(])", ["&"]),
    (">>", r">>(?!=)", ["<<"]),
    ("<<", r"<<(?!=)", [">>"]),
    ("|=", r"\|=", ["&=", "="]),
    ("&=", r"&=", ["|="]),
    ("+=", r"\+=", ["-="]),
    ("-=", r"-=", ["+="]),
    ("?:", r"\?", None),  # swaps the two arms of a ?: on one line
    ("int", r"(?<![\w.'])(\d+)(u?)(?![\w.'])", "int"),
    ("char", r"'(\\.|[^'\\])'", ["'Q'"]),
]


def mutants(text):
    """(line, description, mutated text) for every mutation of every code site."""
    mask = code_mask(text)
    lines = text.split("\n")
    starts, pos = [], 0
    for ln in lines:
        starts.append(pos)
        pos += len(ln) + 1

    def line_of(i):
        lo, hi = 0, len(starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if starts[mid] <= i:
                lo = mid
            else:
                hi = mid - 1
        return lo

    def nth(m, i):
        """' (2nd)' and so on when the same token occurs more than once on its line."""
        ln = line_of(i)
        same = [x.start() for x in re.finditer(re.escape(m.group(0)), lines[ln])]
        if len(same) < 2:
            return ""
        k = same.index(i - starts[ln]) + 1
        return f" ({k}{'st' if k == 1 else 'nd' if k == 2 else 'rd' if k == 3 else 'th'})"

    out = []
    for name, pat, reps in RULES:
        for m in re.finditer(pat, text):
            i, j = m.start(), m.end()
            if not all(mask[i:j]):
                continue
            line = line_of(i) + 1
            if reps == "int":
                v, suf = int(m.group(1)), m.group(2)
                for nv in sorted({v + 1, v - 1, 0, 1} - {v}):
                    if nv >= 0:
                        out.append((line, f"{v}{suf} -> {nv}{suf}{nth(m, i)}",
                                    text[:i] + f"{nv}{suf}" + text[j:]))
                continue
            if reps is None:  # ?: arms
                ln = lines[line - 1]
                k = ln.find("?")
                c = ln.find(":", k)
                end = max(ln.rfind(";"), ln.rfind(")"))
                if c < 0 or end < c:
                    continue
                a, b = ln[k + 1:c], ln[c + 1:end]
                if not a.strip() or not b.strip():
                    continue
                new = ln[:k + 1] + b + ":" + a + ln[end:]
                lines2 = lines[:]
                lines2[line - 1] = new
                out.append((line, "?: arms swapped", "\n".join(lines2)))
                continue
            for r in reps:
                out.append((line, f"{m.group(0)} -> {r or 'nothing'}{nth(m, i)}",
                            text[:i] + r + text[j:]))

    # Statements dropped: a line that is one expression statement.
    for idx, ln in enumerate(lines):
        s = ln.strip()
        if (s.endswith(";") and not s.startswith(("return", "int ", "unsigned", "uint", "char",
                                                   "size_t", "nsv_semver_t", "static", "#",
                                                   "/*", "*", "}", "for", "case", "default"))
                and all(mask[starts[idx]:starts[idx] + len(ln)][k] for k in range(len(ln))
                        if not ln[k].isspace())):
            lines2 = lines[:]
            lines2[idx] = ""
            out.append((idx + 1, f"dropped: {s}", "\n".join(lines2)))
    return out


def load_equivalent():
    """{(mutation, trimmed line): False} from EQUIVALENT: "MUTATION @ LINE  # why"."""
    out = {}
    with open(os.path.join(ROOT, EQUIVALENT)) as f:
        for raw in f:
            if raw.startswith("#") or not raw.strip():
                continue
            entry = raw.split("  # ")[0].rstrip()
            desc, _, ln = entry.partition(" @ ")
            out[(desc, ln)] = False
    return out


def objects(build, int16):
    base = os.path.join(build, "int16") if int16 else build
    objs = []
    for d, _, files in os.walk(os.path.join(base, "tests")):
        if not int16 and os.sep + "int16" in d[len(build):]:
            continue
        objs += [os.path.join(d, f) for f in files if f.endswith(".o")]
    return sorted(objs)


def run_one(args, mutant, workdir):
    line, desc, text = mutant
    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    src = os.path.join(workdir, "nanosemver.c")
    with open(src, "w") as f:
        f.write(text)
    for int16 in (False, True):
        obj = os.path.join(workdir, "m.o")
        exe = os.path.join(workdir, "t")
        cflags = ["-std=c99", "-Os", "-w", "-I" + ROOT] + (INT16 if int16 else [])
        if subprocess.run([cc] + cflags + ["-c", src, "-o", obj], capture_output=True).returncode:
            return (line, desc, "compile")  # not a valid program: neither killed nor kept
        if subprocess.run([cxx] + args.objs[int16] + [obj, "-o", exe],
                          capture_output=True).returncode:
            return (line, desc, "compile")
        # The quick tests first, which kill most mutants; the slow ones only for what
        # survives them. Either way every test runs before a mutant counts as surviving.
        for which in ("--test-case-exclude=" + SLOW, "--test-case=" + SLOW):
            try:
                r = subprocess.run([exe, "-m", "--abort-after=1", which], capture_output=True,
                                   timeout=TIMEOUT)
                if r.returncode:
                    return (line, desc, "killed")
            except subprocess.TimeoutExpired:
                return (line, desc, "killed")  # a hang is a failure too
    return (line, desc, "SURVIVED")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--filter", default="")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    objs = {False: objects(args.build, False), True: objects(args.build, True)}
    if not objs[False] or not objs[True]:
        sys.exit(f"no test objects under {args.build}; run make first")
    # A snapshot of them, so rebuilding while this runs cannot change what it links.
    snap = tempfile.mkdtemp(prefix="nsv-mutate-objs-")
    args.objs = {}
    for k, files in objs.items():
        args.objs[k] = []
        for n, f in enumerate(files):
            dst = os.path.join(snap, f"{int(k)}-{n}-{os.path.basename(f)}")
            shutil.copyfile(f, dst)
            args.objs[k].append(dst)

    text = open(SRC).read()
    lines = text.split("\n")
    equivalent = load_equivalent()
    ms, skipped = [], 0
    for m in mutants(text):
        if (m[1], lines[m[0] - 1].strip()) in equivalent:
            equivalent[(m[1], lines[m[0] - 1].strip())] = True
            skipped += 1
        elif args.filter in m[1]:
            ms.append(m)
    stale = [k for k, used in equivalent.items() if not used]
    if args.list:
        for line, desc, _ in ms:
            print(f"{line}: {desc}")
        return 0

    def one(m):
        with tempfile.TemporaryDirectory(prefix="nsv-mutate-") as d:
            return run_one(args, m, d)

    results = []
    with ThreadPoolExecutor(args.jobs) as ex:
        for r in ex.map(one, ms):
            results.append(r)
            if r[2] == "SURVIVED":
                print(f"  survived nanosemver.c:{r[0]}: {r[1]}", file=sys.stderr, flush=True)
            if len(results) % 100 == 0:
                print(f"  {len(results)}/{len(ms)}", file=sys.stderr, flush=True)
    killed = sum(r[2] == "killed" for r in results)
    bad = sum(r[2] == "compile" for r in results)
    surv = [r for r in results if r[2] == "SURVIVED"]
    for line, desc, _ in sorted(surv):
        print(f"SURVIVED nanosemver.c:{line}: {desc} @ {lines[line - 1].strip()}")
    for desc, ln in stale:
        print(f"STALE {EQUIVALENT}: {desc} @ {ln}")
    print(f"{len(results)} mutants: {killed} killed, {len(surv)} survived, {bad} did not build; "
          f"{skipped} equivalent, skipped")
    return 1 if (surv or stale) else 0


if __name__ == "__main__":
    sys.exit(main())
