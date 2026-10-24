# nanosemver

[![CI](https://github.com/charlesnicholson/nanosemver/actions/workflows/ci.yml/badge.svg)](https://github.com/charlesnicholson/nanosemver/actions/workflows/ci.yml)
[![](https://img.shields.io/badge/license-public_domain-brightgreen.svg)](https://github.com/charlesnicholson/nanosemver/blob/main/LICENSE)
[![](https://img.shields.io/badge/license-0BSD-brightgreen)](https://github.com/charlesnicholson/nanosemver/blob/main/LICENSE)

Parse, compare, format and match ranges of [Semantic Versioning 2.0.0](https://semver.org)
strings in C99. Parsing is done in place: it never allocates, never copies, and reads only
the bytes you give it. Two files, `nanosemver.h` and `nanosemver.c`, needing nothing from
the C library beyond `memcpy`.

```c
#include "nanosemver.h"

nsv_semver_t a, b;
nsv_parse(&a, "1.0.0-rc.1", 10);
nsv_parse(&b, "1.0.0", 5);
if (nsv_cmp(&a, &b) == NSV_CMP_LT) { /* a pre-release ranks below its release */ }
if (nsv_range_match(">=1.0.0 <2.0.0-0 || ^3.1.0", 26, &b, 0) == 1) { /* b is in range */ }
```

## Three kinds of `nsv_semver_t`

| Kind | Made by | Strings live | Lifetime |
|---|---|---|---|
| View | `nsv_parse` | in the string you parsed | the source must outlive it |
| Copy | `nsv_copy` | right after the header, NUL-terminated | self-contained and relocatable |
| Literal | `NSV_SEMVER_LIT` | in string literals | static |

Every function accepts all three, so the cheap view is the default and a copy is
opt-in. `major`, `minor` and `patch` are plain `uint32_t` fields; read the pre-release and
build metadata through `nsv_get_pre` / `nsv_get_build`. A zero-initialized
`nsv_semver_t` is a valid `0.0.0`.

```c
/* A persistent copy: a sizing call, then one allocation holding everything. */
int n = nsv_copy(NULL, 0, &view);
nsv_semver_t *v = malloc(n);
nsv_copy(v, n, &view);

/* Or static storage, through a wrapper struct for alignment. */
static struct { nsv_semver_t v; char strs[64]; } g_ver;
nsv_copy(&g_ver, sizeof g_ver, &view);

/* Build stamping, with no runtime cost. */
static nsv_semver_t const k_version = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
```

A copy can be `memcpy`'d as a whole, but never copy its header alone (`*v`, struct
assignment, a `qsort` of copies): its strings stay behind. Sort an array of pointers
to copies instead.

## API

Every function returns `int`: `>= 0` is success, often a length, and `< 0` is an
`NSV_RET_ERR_*` code. `NULL` pointers are rejected with `NSV_RET_ERR_ARG`.

| Function | Returns |
|---|---|
| `nsv_parse(out, s, len)` | `NSV_RET_OK`; `*out` is untouched on error |
| `nsv_parse_prefix(out, s, len, &n)` | as `nsv_parse`, for the version at the start of `s`; `n` is its length |
| `nsv_cmp(a, b)` | `NSV_CMP_LT` (0), `NSV_CMP_EQ` (1) or `NSV_CMP_GT` (2) |
| `nsv_range_match(s, len, v, flags)` | 1 if `v` is in the range, 0 if not |
| `nsv_copy(dst, cap, s)` | bytes written; `dst == NULL` asks for the size |
| `nsv_format(dst, n, s)` | length without the NUL; `dst == NULL` asks for it |
| `nsv_get_pre(s, &str)` / `nsv_get_build(s, &str)` | the length; `*str` is `""` when absent |

- `nsv_parse` takes a pointer and a length, so it parses substrings without a NUL. The
  whole range must be a version: no leading `v`, no whitespace.
- `nsv_parse_prefix` parses the longest prefix that is a version, like `from_chars`, and
  leaves what follows to you: `"1.2.3, 2.0.0"` gives 1.2.3, but so do `"1.2.3.4"` and
  `"1.2.3x"`, and `"1.2.03"` gives 1.2.0. Check the byte after it when a version must
  end there.
- `nsv_cmp` follows semver precedence and ignores build metadata, so `NSV_CMP_EQ` does not
  mean identical. `nsv_cmp(a, b) - 1` is a `qsort`-style comparator.
- `nsv_format` never truncates: a cut-off version string is a different version. Too small
  a buffer gets `NSV_RET_ERR_BUF_TOO_SMALL` and an empty string.
- A view's strings are not NUL-terminated; always use the returned lengths, e.g.
  `printf("%.*s", len, str)`.

| Error | Meaning |
|---|---|
| `NSV_RET_ERR_ARG` | a `NULL` pointer |
| `NSV_RET_ERR_SYNTAX` | not a semver 2.0.0 string |
| `NSV_RET_ERR_OVERFLOW` | valid, but major, minor or patch exceeds `UINT32_MAX` |
| `NSV_RET_ERR_TOO_LONG` | valid, but longer than 65535 bytes, or a length exceeds a 16-bit `INT_MAX` |
| `NSV_RET_ERR_BUF_TOO_SMALL` | the destination is too small; nothing useful was written |
| `NSV_RET_ERR_ALIGN` | `nsv_copy`'s destination is not aligned for `nsv_semver_t` |

The spec sets no limits on length or numeric size; nanosemver's are `uint32_t` numbers
and 65535-byte strings. A syntax error always takes precedence, so `OVERFLOW` and
`TOO_LONG` only ever describe valid semver.

## Ranges

There is no range standard: semver.org defines versions and their precedence, nothing
more. The nearest thing is npm's [grammar](https://github.com/npm/node-semver#range-grammar),
which Composer, Conan and Neargye's C++ semver follow closely too, so `nsv_range_match`
takes it, without hyphen ranges, plus `!=`, and npm reads every range it accepts the same
way:

| Range | Matches |
|---|---|
| `1.2.3`, `=1.2.3` | equal precedence: build metadata never matters |
| `<1.2.3`, `<=1.2.3`, `>1.2.3`, `>=1.2.3` | by `nsv_cmp` precedence |
| `!=1.2.3` | anything else; not npm's, but Neargye's, Python's (PEP 440) and Composer's |
| `~1.2.3` | `>=1.2.3 <1.3.0-0`: the same `MAJOR.MINOR` |
| `^1.2.3`, `^0.2.3`, `^0.0.3` | `>=1.2.3 <2.0.0-0`, `>=0.2.3 <0.3.0-0`, `>=0.0.3 <0.0.4-0`: the same leftmost nonzero number |
| `1.2`, `1.2.x`, `1.x`, `*` | the whole line, pre-releases and all: `>=1.2.0-0 <1.3.0-0`; `*` is everything |
| `>=1.2`, `>1.2`, `<1.2`, `<=1.2` | `>=1.2.0-0`, `>=1.3.0-0`, `<1.2.0-0`, `<1.3.0-0` |
| `!=1.4` | off the 1.4 line |
| `~1`, `^1.2`, `^0.2` | `>=1.0.0-0 <2.0.0-0`, `>=1.2.0-0 <2.0.0-0`, `>=0.2.0-0 <0.3.0-0` |
| `A B` | both: comparators separated by spaces or tabs, with optional spaces after an operator |
| `A \|\| B` | either |

A partial version is up to three numbers followed by wildcards (`x`, `X` or `*`), with no
pre-release or build. Hyphen ranges (`1.2.3 - 2.0.0`) are a syntax error, as are other
grammars' spellings: Cargo and Python separate comparators with commas and have no OR,
Python writes `==` and `~=`, and RubyGems `~>`. An empty range is an error too, and so
is anything left over: the whole string must parse.

A pre-release is, by default, just a version ranking below its release, as with npm's
`includePrerelease`. So `>=1.0.0 <2.0.0` admits `2.0.0-rc.1`; `<2.0.0-0` stops below
every 2.0.0 pre-release, as `^` and `~` do. `NSV_RANGE_EXCLUDE_PRE` is npm's default
instead: a pre-release matches a set only if a comparator in it has a pre-release of the
same `MAJOR.MINOR.PATCH`, so `>=1.5.0-beta <2.0.0` admits `1.5.0-rc.1` but not
`1.6.0-rc.1`, and a partial version's lower bound is its `.0` release: `>=1.2` is
`>=1.2.0`. `!=` and partial versions never let a pre-release in. The only differences
from npm are its own special cases: it reads `>=0.0.0`, and the lower bound of `^0.0`,
`~0.0.0`, `0.x` and the like, as `*`, which lets 0.0.0's pre-releases past it, and a set
that is only `*` takes over the whole range, shutting out pre-releases the others name.

`make npm-check` holds this to npm's own semver package: a million generated ranges,
every range of up to two comparators over the operators and whole and partial versions,
and the test corpus, against versions around their bounds. Ranges with `!=` are left to
the reference tests.

## Performance

`nsv_parse` reads each byte once and never divides: overflow is caught by a comparison
per digit. `nsv_cmp` settles most comparisons on the numbers alone; when the pre-release
decides, it finds the first byte where the two differ and classifies only the identifier
around it, instead of splitting both into identifiers. On cores without a fast divide,
`nsv_format` divides by shifts and adds, so nothing from libgcc is linked in: on every
Cortex-M, `memcpy` is the only call out of `nanosemver.c`. `nsv_range_match` reads the
range once, without allocating, and each version in it twice: once to find where it ends,
once to parse it, which keeps `nsv_parse`'s one pass to itself.

Nanoseconds per call on an Apple M3 Pro at `-O2`; the Linux compilers ran in a VM on the
same machine. Each workload is 1024 versions from a fixed-seed generator in
`bench/bench.cc`: mostly one- and two-digit numbers, registry-style tags, hashes and
timestamps, and for the ranges, mostly `^` and `~`, then bounded pairs, exact versions
and alternatives, each against a version from the same mix.

| Workload | Apple clang 21 | gcc 15 | clang 22 |
|---|---:|---:|---:|
| `nsv_parse`, releases (`4.17.21`) | 7.1 | 7.6 | 7.1 |
| `nsv_parse`, pre-releases (`2.0.0-beta.3`) | 12.1 | 12.7 | 12.8 |
| `nsv_parse`, both (`1.0.0-rc.1+sha.5114f85`) | 21.2 | 23.8 | 22.8 |
| `nsv_parse`, 40-character hashes, 58 bytes | 36.5 | 42.2 | 41.0 |
| `nsv_parse_prefix`, a version and what follows it | 15.2 | 18.2 | 15.4 |
| `nsv_cmp`, the numbers decide | 1.0 | 1.1 | 1.1 |
| `nsv_cmp`, the pre-release decides | 4.9 | 4.9 | 4.9 |
| `nsv_range_match`, manifest-style (`^1.2.3`, `>=1.0.0 <2.0.0`) | 25.0 | 29.0 | 26.2 |
| `nsv_range_match`, four bounded alternatives | 128.9 | 197.1 | 147.3 |
| `nsv_format` | 7.9 | 7.7 | 7.6 |
| `std::sort` of all 1024, using `nsv_cmp` | 15.9 µs | 14.3 µs | 17.0 µs |

Cycles per call on Cortex-M at `-Os`, with the same inputs, counted in an emulator
(exact instruction counts, cycles from a simple zero-wait-state model), and code size:

| | Cortex-M0+ | Cortex-M4 |
|---|---:|---:|
| `nsv_parse`, releases | 227 | 239 |
| `nsv_parse`, pre-releases | 401 | 414 |
| `nsv_parse`, both | 649 | 669 |
| `nsv_parse`, 40-character hashes | 1085 | 1157 |
| `nsv_parse_prefix`, a version and what follows it | 461 | 486 |
| `nsv_cmp`, the numbers decide | 24 | 26 |
| `nsv_cmp`, the pre-release decides | 169 | 152 |
| `nsv_range_match`, manifest-style (`^1.2.3`, `>=1.0.0 <2.0.0`) | 934 | 950 |
| `nsv_range_match`, four bounded alternatives | 4717 | 4778 |
| `nsv_range_match`, partial versions (`^1.2`, `1.x`, `>=1 !=1.4.1`) | 728 | 673 |
| `nsv_format` | 322 | 240 |
| `nanosemver.o`, bytes of code and data | 2288 | 2288 |

All of it is a ratchet: `make ratchet` fails if the code grows past `size-budget.mk` or
any workload takes more cycles than `bench/cm/ratchet.txt` allows, and `make
ratchet-update` lowers both to what the code now takes. `nsv_range_match` and
`nsv_parse_prefix` are 1028 of those bytes on a Cortex-M0+; built with
`-ffunction-sections` and linked with `--gc-sections`, a program pays only for the
functions it calls.

`NSV_SOFT_DIV` selects the shift-and-add divide: 1 by default on Cortex-M0/M0+/M1, AVR,
MSP430 and RISC-V without M, 0 elsewhere. Define it to override.

### Measuring

`make bench` builds this tree's `nanosemver.c` and `BENCH_BASE`'s (default `origin/main`)
into one binary and times them interleaved, alternating which goes first, so thermal and
frequency drift hit both alike; every workload also checksums both sides' results, and a
mismatch voids the run. `make bench-cm` runs the same workloads through bare Cortex-M0+
and Cortex-M4 images of both in the [Unicorn](https://www.unicorn-engine.org) emulator.
A baseline from before `nsv_range_match` and `nsv_parse_prefix` times only this tree's.
`make size` prints the Cortex-M code size function by function, and `make size-check`
fails if it outgrows `size-budget.mk` or calls anything but `memcpy`. `make ratchet` adds
the emulator's cycle counts, workload by workload, against `bench/cm/ratchet.txt`; CI
runs it on every change. The Arm toolchain and [uv](https://docs.astral.sh/uv/), which
provisions the emulator, come through envy on first use; `make` never touches them.

## Development

`make` builds and runs the unit tests on Linux and macOS; `build.bat` does the same with
MSVC from a Developer Command Prompt. Both fetch the pinned [doctest](https://github.com/doctest/doctest)
through [envy](https://github.com/envy-package-manager/envy) on first use; pass
`DOCTEST_H=/path/to/doctest.h` to skip envy entirely.

| Command | Does |
|---|---|
| `make` | unit tests, built twice: as configured, and as for a small MCU, with `-DNSV_INT_MAX=32767` to reach the checks a 16-bit `int` needs and division by shifts |
| `make CC=gcc CXX=g++ CFG=Debug ARCH=32 SAN=address,undefined` | any compiler, configuration, width and sanitizer |
| `make coverage` | clang source coverage; fails unless every region, line and branch of `nanosemver.c` is covered, in both configurations |
| `make fuzz FUZZ_ARGS="-t 60"` | the standalone fuzz driver, which runs with any compiler |
| `make libfuzzer` | libFuzzer with ASan and UBSan (Linux clang; Apple clang ships no libFuzzer) |
| `make bench BENCH_ARGS=--quick` | host timings against `BENCH_BASE`; `--filter cmp` picks workloads |
| `make bench-cm` | emulated Cortex-M0+ and Cortex-M4 instruction counts and cycles against `BENCH_BASE` |
| `make size size-check` | Cortex-M code size, and the budget and `memcpy`-only checks |
| `make ratchet` / `make ratchet-update` | Cortex-M code size and cycles held to `size-budget.mk` and `bench/cm/ratchet.txt` / lowered to the code's |
| `make npm-check NPM_ARGS="1000000 7"` | `nsv_range_match` against npm's semver package: generated ranges, and a seed |
| `build.bat [fuzz \| clean]` | the unit tests, both configurations, and optionally the fuzz driver, with MSVC |

The tests check `nanosemver.c` against a deliberately naive reference implementation in
`tests/ref_semver.cc`, which is itself checked against the regular expression semver.org
publishes. It finds a prefix by trying every length, and matches a range by splitting it
into strings, with `^` and `~` rewritten into npm's two bounds. Every string the tests
parse sits against a guard page, so reading one byte past the end faults even without a
sanitizer. The fuzzers check the same properties on generated and mutated input: the
result code, every field, the format round-trip, deep copy and relocation, comparison in
both directions, the prefix parse, and ranges around the version, all against the
reference. `tests/test_long.cc` runs those checks exhaustively on long sections: every
byte value at every position, every length up to 40, and comparisons that differ at
every position. Exhaustively too: every string of up to six bytes of each kind for the
prefix parse, every range of up to three comparators for the matcher, and every byte at
every position of a range.

## Releases

Push a tag like `v1.2.0` (or `v1.2.0-rc.1`, published as a pre-release). CI runs the
whole matrix against it and, only if everything passes, `release.sh` stamps the tag
into `nanosemver.h` and publishes it as a GitHub release, alongside `nanosemver.c` and a
zip of the full source carrying the same stamped header. The stamp is the first line,
and `NSV_VERSION_MAJOR`, `NSV_VERSION_MINOR`, `NSV_VERSION_PATCH` and the string
`NSV_VERSION` (`"1.2.0-rc.1"`), for code that reports or checks which nanosemver it has.
The checked-in header keeps its `@NSV_VERSION@` placeholder and 0.0.0, so no commit ever
bumps a version. `./release.sh v1.2.0` shows locally what a tag would ship.

## License

Dual-licensed under the [Unlicense](https://unlicense.org) and
[0BSD](https://opensource.org/license/0bsd), your pick; see [LICENSE](LICENSE).
