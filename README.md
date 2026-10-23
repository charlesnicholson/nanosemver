# nanosemver

[![CI](https://github.com/charlesnicholson/nanosemver/actions/workflows/ci.yml/badge.svg)](https://github.com/charlesnicholson/nanosemver/actions/workflows/ci.yml)
[![](https://img.shields.io/badge/license-public_domain-brightgreen.svg)](https://github.com/charlesnicholson/nanosemver/blob/main/LICENSE)
[![](https://img.shields.io/badge/license-0BSD-brightgreen)](https://github.com/charlesnicholson/nanosemver/blob/main/LICENSE)

Parse, compare and format [Semantic Versioning 2.0.0](https://semver.org) strings in C99.
Parsing is done in place: it never allocates, never copies, and reads only the bytes you
give it. Two files, `nanosemver.h` and `nanosemver.c`, needing nothing from the C
library beyond `memcpy` and `memcmp`.

```c
#include "nanosemver.h"

nsv_semver_t a, b;
nsv_parse(&a, "1.0.0-rc.1", 10);
nsv_parse(&b, "1.0.0", 5);
if (nsv_cmp(&a, &b) == NSV_CMP_LT) { /* a pre-release ranks below its release */ }
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
| `nsv_cmp(a, b)` | `NSV_CMP_LT` (0), `NSV_CMP_EQ` (1) or `NSV_CMP_GT` (2) |
| `nsv_copy(dst, cap, s)` | bytes written; `dst == NULL` asks for the size |
| `nsv_format(dst, n, s)` | length without the NUL; `dst == NULL` asks for it |
| `nsv_get_pre(s, &str)` / `nsv_get_build(s, &str)` | the length; `*str` is `""` when absent |

- `nsv_parse` takes a pointer and a length, so it parses substrings without a NUL. The
  whole range must be a version: no leading `v`, no whitespace.
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

## Development

`make` builds and runs the unit tests on Linux and macOS; `build.bat` does the same with
MSVC from a Developer Command Prompt. Both fetch the pinned [doctest](https://github.com/doctest/doctest)
through [envy](https://github.com/envy-package-manager/envy) on first use; pass
`DOCTEST_H=/path/to/doctest.h` to skip envy entirely.

| Command | Does |
|---|---|
| `make` | unit tests, built twice: as configured, and with `-DNSV_INT_MAX=32767` to reach the checks a 16-bit `int` needs |
| `make CC=gcc CXX=g++ CFG=Debug ARCH=32 SAN=address,undefined` | any compiler, configuration, width and sanitizer |
| `make coverage` | clang source coverage; fails unless every region, line and branch of `nanosemver.c` is covered, in both `int` configurations |
| `make fuzz FUZZ_ARGS="-t 60"` | the standalone fuzz driver, which runs with any compiler |
| `make libfuzzer` | libFuzzer with ASan and UBSan (Linux clang; Apple clang ships no libFuzzer) |
| `build.bat [fuzz \| clean]` | the unit tests, both configurations, and optionally the fuzz driver, with MSVC |

The tests check `nanosemver.c` against a deliberately naive reference implementation in
`tests/ref_semver.cc`, which is itself checked against the regular expression semver.org
publishes. Every string the tests parse sits against a guard page, so reading one byte
past the end faults even without a sanitizer. The fuzzers check the same properties on
generated and mutated input: the result code, every field, the format round-trip, deep
copy and relocation, and comparison in both directions against the reference.

## Releases

Push a tag like `v1.2.0` (or `v1.2.0-rc.1`, published as a pre-release). CI runs the
whole matrix against it and, only if everything passes, `release.sh` stamps the tag
into the first line of `nanosemver.h` and publishes it as a GitHub release, alongside
`nanosemver.c` and a zip of the full source carrying the same stamped header. The
checked-in header keeps its `@NSV_VERSION@` placeholder, so no commit ever bumps a
version. `./release.sh v1.2.0` shows locally what a tag would ship.

## License

Dual-licensed under the [Unlicense](https://unlicense.org) and
[0BSD](https://opensource.org/license/0bsd), your pick; see [LICENSE](LICENSE).
