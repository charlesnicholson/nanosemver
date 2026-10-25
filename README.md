# nanosemver

[![CI](https://github.com/charlesnicholson/nanosemver/actions/workflows/ci.yml/badge.svg)](https://github.com/charlesnicholson/nanosemver/actions/workflows/ci.yml)
[![](https://img.shields.io/badge/license-public_domain-brightgreen.svg)](https://github.com/charlesnicholson/nanosemver/blob/main/LICENSE)
[![](https://img.shields.io/badge/license-0BSD-brightgreen)](https://github.com/charlesnicholson/nanosemver/blob/main/LICENSE)

Parse, compare, format and match ranges of [Semantic Versioning 2.0.0](https://semver.org)
strings in C99. Parsing is done in place: it never allocates, never copies, and reads only
the bytes you give it. Two files, `nanosemver.h` and `nanosemver.c`, needing nothing from
the C library beyond `memcpy`; for C++, an optional third, the header `nanosemver.hpp`.

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

## C++

`nanosemver.hpp` is optional and C++ only: a header over the C API, with `nanosemver.c`
still built as C. It has two kinds of version, which compare with each other:

- `nsv::SemVer` holds its own copy in place, the header and a 64-byte array for the
  pre-release and build strings, so it never allocates and never dangles: for a version
  that has to outlive the string it came from. `nsv::SemVerInplace<N>` is the same with `N`
  bytes; `SemVer` is `SemVerInplace<64>`.
- `nsv::SemVerView` is `nsv_parse`'s view: nothing copied, no length cap, but the string
  must outlive it: for a version parsed, used and dropped while its string is at hand.

It needs C++11 or later, no heap, exceptions, RTTI or C++ standard library, and from the
C library only `strlen`, through `<string.h>`, so it builds where there are no C++
standard headers at all, as with avr-g++.

```cpp
#include "nanosemver.hpp"

int rc;
nsv::SemVer const v = nsv::SemVer::from("1.4.0-rc.2+g3f9a1c2", rc);  // 64 B in place
if (rc != NSV_RET_OK) { /* not a version: v is 0.0.0 */ }
nsv::SemVer const w = nsv::SemVer::from("1.4.0", rc);
bool const older = v < w;  // true: a pre-release ranks below its release
int const in = v.matches(">=1.2.0 <2.0.0 || ^3.1.0");  // 1 if so, 0 if not, < 0 on error
char buf[32];
int const len = v.format(buf, sizeof buf);  // 19: "1.4.0-rc.2+g3f9a1c2"
char const *pre;
int const n = v.pre(pre);  // 4: "rc.2", NUL-terminated
// 8 bytes: too few for these two strings and their NULs, so NSV_RET_ERR_BUF_TOO_SMALL
nsv::SemVerInplace<8> const small = nsv::SemVerInplace<8>::from("1.4.0-rc.2+g3f9a1c2", rc);
```

```cpp
char const tag[] = "1.4.0-rc.2+g3f9a1c2";  // must outlive the view
int rc;
nsv::SemVerView const v = nsv::SemVerView::from(tag, rc);
if (rc != NSV_RET_OK) { /* not a version: v is 0.0.0 */ }
char const *pre;
int const n = v.pre(pre);  // 4: "rc.2", then "+g3f9a1c2": tag's own bytes
bool const newer = v > nsv::SemVer();  // true: views and SemVers compare
nsv::SemVer const kept = nsv::SemVer::from(v.semver(), rc);  // a copy, to outlive tag
```

- `from(s, rc)` takes a NUL-terminated string and `from(s, len, rc)` a length, as
  `nsv_parse` does, and `from_prefix(s, len, n, rc)` is `nsv_parse_prefix`. `from(c, rc)`
  takes an `nsv_semver_t`: a `SemVerInplace<N>` copies any, a view, copy or literal, and a
  `SemVerView` views a view or literal, but not a copy, whose header does not point at
  its strings (`NSV_RET_ERR_ARG`). Each makes a new version, with its error in `rc` and
  `0.0.0` on error. (`NSV_SEMVER_LIT` itself needs C++20 in C++, for its designated
  initializers.)
- Each returns a temporary, which C++17 requires, and older compilers choose, to build in
  the variable it initializes, so a `SemVerInplace<N>` takes no stack for its `N` bytes.
  To replace a version, assign it another: that builds the new one first and copies it
  over, so it does take the version's size in stack.
- `N` bytes hold the pre-release, the build metadata and a NUL after each, laid out as
  `nsv_copy` lays them out, so every version of up to `N + 4` characters fits, and a
  longer one does if its strings do; one that does not is `NSV_RET_ERR_BUF_TOO_SMALL`. A
  `SemVerInplace<N>` is `sizeof(nsv_semver_t) + N` bytes, rounded up to the header's alignment,
  so a `SemVer` is 96 on 64-bit targets and 88 on 32-bit ones. A `SemVerView` is the
  header.
- `==`, `!=`, `<`, `<=`, `>` and `>=` are `nsv_cmp`'s precedence, between any two
  versions, so `==` ignores build metadata: `1.0.0+a == 1.0.0+b`.
- `matches(range)`, NUL-terminated and without flags, and `matches(range, len, flags)`
  return what `nsv_range_match` does: 1, 0, or an `NSV_RET_ERR_*` code. Test for `== 1`,
  since an error is nonzero too.
- `major()`, `minor()` and `patch()` are the numbers; `pre(p)` and `build(p)` are
  `nsv_get_pre` and `nsv_get_build`, the length with `p` at the bytes, NUL-terminated in
  a `SemVerInplace<N>` and the string's own in a view; `format(dst, n)` is `nsv_format`.
- `semver()` is the version for the C API: pass `&v.semver()`. A `SemVerInplace<N>`'s is a
  copy's header, so never copy it, as its strings would stay behind.
- Either is plain bytes, trivially copyable: copying or moving one copies them, even by
  `memcpy`, and there is nothing to free. A copied view views the same string.

## Comparing versions

`nsv_cmp` ranks two versions by semver precedence (semver.org §11):

1. `MAJOR`, `MINOR` and `PATCH` compare as numbers, in that order: `1.9.0 < 1.10.0 < 2.0.0`.
2. A pre-release ranks below its release: `1.0.0-rc.1 < 1.0.0`.
3. Two pre-releases of the same version compare identifier by identifier, left to right:
   - numeric identifiers as numbers: `1.0.0-beta.2 < 1.0.0-beta.11`;
   - alphanumeric ones in ASCII order, so uppercase first: `1.0.0-RC < 1.0.0-rc`;
   - a numeric identifier below an alphanumeric one: `1.0.0-1 < 1.0.0-alpha`;
   - and when one runs out first, it ranks below: `1.0.0-alpha < 1.0.0-alpha.1`.
4. Build metadata never counts: `1.0.0+a` and `1.0.0+b` are `NSV_CMP_EQ`, equal in
   precedence though not identical.

semver.org's own example, in order:
`1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 <
1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0`.

## Ranges

`nsv_range_match(s, len, v, flags)` answers whether `v` is in a range like
`>=1.2.0 <1.4.0 || ^2.1.0`. There is no range standard: semver.org defines versions and
precedence, nothing more. The nearest thing is npm's
[grammar](https://github.com/npm/node-semver#range-grammar), which Composer and Conan
follow closely too. nanosemver takes npm's, without hyphen ranges, plus `!=` and the few
spellings from other grammars that mean the same in all of them. npm reads every range
nanosemver accepts, spelled as npm's, the same way.

### Syntax

- A **range** is one or more comparator sets separated by `||`, with optional spaces or
  tabs around it. `v` is in the range if it is in any set.
- A **set** is one or more comparators separated by spaces, tabs or a comma, or by
  nothing before an operator. `v` is in the set if it satisfies every comparator in it.
- A **comparator** is an optional operator, optional spaces or tabs, and a version:
  `=`, `<`, `<=`, `>`, `>=`, `!=`, `~` or `^`, or another grammar's `==`, `~=` or `~>`,
  then `1.2.3`; no operator means `=`.
- The **version** is a full semver 2.0.0 version, or a **partial** one: one to three
  components, numbers then wildcards (`x`, `X` or `*`), with no pre-release or build, as
  in `1`, `1.2`, `1.x`, `1.2.*` and `*`. Either may start with a `v`, as in npm:
  `v1.2.3`, `>=v1.2`.
- The whole string must parse: an empty range, a stray `|` or comma, and anything left
  over are all `NSV_RET_ERR_SYNTAX`, however `v` compares.

Spaces and commas only ever separate: they never change what anything means. A version
ends where its characters do, so `>=1.0.0<2.0.0` is `>=1.0.0 <2.0.0` and
`>=1.2, <1.5` is `>=1.2 <1.5`; a space is needed only where two things would otherwise
run together, as two versions would. That rule is why npm's hyphen ranges are out:
`1.2.3 - 2.3.4` is a range only because of its spaces, and `1.2.3-2.3.4` is one
pre-release version.

### Whole versions

| Comparator | Means | Matches | Does not match |
|---|---|---|---|
| `1.2.3`, `=1.2.3`, `==1.2.3` | equal precedence | `1.2.3`, `1.2.3+build` | `1.2.4`, `1.2.3-rc` |
| `!=1.2.3` | not equal | `1.2.4`, `1.2.3-rc` | `1.2.3`, `1.2.3+b` |
| `<1.2.3` | below | `1.2.2`, `1.2.3-rc` | `1.2.3` |
| `<=1.2.3` | at or below | `1.2.3` | `1.2.4-0` |
| `>1.2.3` | above | `1.2.4-0`, `1.3.0` | `1.2.3+b` |
| `>=1.2.3` | at or above | `1.2.3` | `1.2.3-rc` |
| `~1.2.3`, `~=1.2.3`, `~>1.2.3` | `>=1.2.3 <1.3.0-0`: the same `MAJOR.MINOR` | `1.2.9` | `1.2.2`, `1.3.0-0` |
| `^1.2.3` | `>=1.2.3 <2.0.0-0`: the same leftmost nonzero number | `1.9.0` | `2.0.0-rc.1` |
| `^0.2.3` | `>=0.2.3 <0.3.0-0` | `0.2.9` | `0.3.0` |
| `^0.0.3` | `>=0.0.3 <0.0.4-0` | `0.0.3` | `0.0.4` |

`X.Y.Z-0` is the lowest version with that core, below all its pre-releases, so `<2.0.0-0`
stops below every 2.0.0 pre-release, which `<2.0.0` does not.

### Partial versions

A partial version stands for its whole line, pre-releases and all, and the operators
compare lines:

| Comparator | Means | Matches | Does not match |
|---|---|---|---|
| `1.2`, `1.2.x`, `=1.2` | `>=1.2.0-0 <1.3.0-0` | `1.2.0-beta`, `1.2.99` | `1.3.0-0` |
| `1`, `1.x`, `1.*.*` | `>=1.0.0-0 <2.0.0-0` | `1.0.0-0`, `1.99.0` | `2.0.0-0` |
| `*`, `x` | everything | `0.0.0-0`, `9.9.9` | |
| `!=1.4.x` | off the 1.4 line | `1.3.9`, `1.5.0` | `1.4.0-0`, `1.4.99` |
| `<1.2` | below the 1.2 line: `<1.2.0-0` | `1.1.99` | `1.2.0-0` |
| `<=1.2` | up to the end of it: `<1.3.0-0` | `1.2.99` | `1.3.0-0` |
| `>1.2` | above it: `>=1.3.0-0` | `1.3.0-0` | `1.2.99` |
| `>=1.2` | from its start: `>=1.2.0-0` | `1.2.0-beta` | `1.1.9` |
| `~1.2`, `~1` | the line, like `1.2` and `1` | `1.2.9`; `1.9.9` | `1.3.0-0`; `2.0.0-0` |
| `^1.2` | `>=1.2.0-0 <2.0.0-0` | `1.9.9` | `2.0.0-0` |
| `^0.2` | `>=0.2.0-0 <0.3.0-0` | `0.2.9` | `0.3.0-0` |
| `^0.0`, `^0` | `<0.1.0-0`, `<1.0.0-0` | `0.0.9`; `0.9.9` | `0.1.0`; `1.0.0` |

At the top of the range, `>1.4294967295` carries into the next number, so it matches
`2.0.0`, and nothing is above `>4294967295`. A carried bound keeps its `-0` even under
`NSV_RANGE_EXCLUDE_PRE`: npm's is `>=1.4294967296.0`, which every 2.0.0 pre-release is
above.

`==`, `~=` and `~>` take no partial version, and `!=` one only with a wildcard: the
grammars those operators come from read `==1.2` and `!=1.2` as 1.2.0, and `~=1.2` and
`~>1.2` as `>=1.2 <2`, where npm's terms would make each the 1.2 line. Rather than guess,
each is a syntax error.

### Combining

| Range | Matches | Does not match |
|---|---|---|
| `>=1.2.0 <1.4.0 \|\| ^2.1.0` | `1.3.5`, `2.5.0` | `2.0.0` |
| `>=1.2 !=1.3.1 <2` | `1.3.2` | `1.3.1` |
| `>= 1.2.3 < 2` | `1.5.0` | `2.0.0` |
| `>=1.2, <1.5` | `1.4.9` | `1.5.0` |
| `>=1.0.0<2.0.0 \|\| v3.0.0` | `3.0.0` | `2.0.0` |

### Pre-releases

By default a pre-release is just a version that ranks below its release, as with npm's
`includePrerelease`. `NSV_RANGE_EXCLUDE_PRE` gives npm's default instead:

- a pre-release `v` is in a set only if a comparator in it names a pre-release of the same
  `MAJOR.MINOR.PATCH`; `!=` and partial versions never do;
- and a partial version's lower bound is its `.0` release, so `>=1.2` is `>=1.2.0`.

| Range | Version | Default | `NSV_RANGE_EXCLUDE_PRE` |
|---|---|---:|---:|
| `>=1.0.0 <2.0.0` | `1.5.0-beta` | 1 | 0 |
| `>=1.0.0 <2.0.0` | `2.0.0-rc.1` | 1 | 0 |
| `>=1.0.0 <2.0.0-0` | `2.0.0-rc.1` | 0 | 0 |
| `>=1.5.0-beta <2.0.0` | `1.5.0-rc.1` | 1 | 1 |
| `>=1.5.0-beta <2.0.0` | `1.6.0-rc.1` | 1 | 0 |
| `^1.2.3-beta.2` | `1.2.3-beta.4` | 1 | 1 |
| `^1.2.3-beta.2` | `1.2.4-beta.2` | 1 | 0 |
| `1.2` | `1.2.0-beta` | 1 | 0 |
| `>=1.2 <1.2.0-rc.5` | `1.2.0-beta` | 1 | 0 |
| `!=1.2.3-rc.1` | `1.2.3-rc.2` | 1 | 0 |

The flag only ever takes matches away, and never from a release.

### Errors

`NSV_RET_ERR_SYNTAX` for a malformed range wins over everything. Otherwise a version in
the range beyond nanosemver's limits, a number past `UINT32_MAX` (`<4294967296.0.0`,
`>=4294967296`) or a version longer than 65535 bytes, is `NSV_RET_ERR_OVERFLOW` or
`NSV_RET_ERR_TOO_LONG`, the first such in the range, whether or not `v` would match.

### Coming from another grammar

Other grammars' spellings that mean the same in all of them are taken as they are:

| Written | Grammar | Same as |
|---|---|---|
| `>=1.2, <1.5` | Cargo, Python (PEP 440), Composer | `>=1.2 <1.5` |
| `==1.2.3` | Python, Composer | `=1.2.3` |
| `~=1.4.5` | Python | `~1.4.5` |
| `~> 1.4.5` | RubyGems, Hex | `~1.4.5` |
| `v1.2.3`, `=v1.2.3` | npm | `1.2.3` |
| `>=1.0.0<2.0.0` | | `>=1.0.0 <2.0.0` |

The rest are syntax errors, not a guess at what was meant:

| Written | Grammar | Why not | Write |
|---|---|---|---|
| `1.2.3 - 2.3.4` | npm | its spaces make it a range: `1.2.3-2.3.4` is one version | `>=1.2.3 <=2.3.4` |
| `==1.2`, `!=1.2` | Python, Composer | 1.2.0 there, the 1.2 line in npm's terms | `==1.2.0` or `1.2.x`; `!=1.2.0` or `!=1.2.x` |
| `~=1.4`, `~>1.4` | Python; RubyGems, Hex | `>=1.4 <2` there, the 1.4 line in npm's | `>=1.4 <2` or `~1.4` |
| an empty range | npm, meaning any | an unset field would match everything | `*` |

And one that is not an error but means something else elsewhere: Cargo reads a bare
`1.2.3` as `^1.2.3`; here it is exactly `1.2.3`.

### Where npm differs

npm reads two things its own way. In its default mode it treats `>=0.0.0`, and the lower
bound of `^0.0`, `~0.0.0`, `0.x` and the like, as `*`, which lets 0.0.0's pre-releases
past it. And a set that is only `*` takes over the whole range, shutting out
pre-releases the other sets name. nanosemver does neither. Everywhere else,
`make npm-check` holds it to npm's own semver package: a million generated ranges,
every range of up to two comparators over the operators and whole and partial versions,
the unit tests' own cases, and the test corpus, against versions around their bounds.
Ranges with commas, `==` or comparators run together go to npm respelled as its own
(`>=1.2 <1.5`, `=1.2.3`, `>=1.0.0 <2.0.0`), and ranges with `!=`, which has no npm
spelling, are left to the reference tests.

## Error messages

Define `NSV_DIAG` to 1, for `nanosemver.c` and the code that calls it alike, and
`nsv_diag_set_print(print, ctx)` has every `NSV_RET_ERR_SYNTAX`, `NSV_RET_ERR_OVERFLOW`
and `NSV_RET_ERR_TOO_LONG` from `nsv_parse`, `nsv_parse_prefix` and `nsv_range_match`
say where and why, through `print`:

```c
static void print(void *ctx, char const *s, size_t len) { fwrite(s, 1, len, ctx); }

nsv_diag_set_print(print, stderr);
nsv_range_match(">=1.0.0 <", 9, &v, 0);  /* NSV_RET_ERR_SYNTAX, and: */
```

```
bad range at column 10: expected a version
  >=1.0.0 <
           ^
```

| Version or range | Message |
|---|---|
| `1.2.03` | `bad version at column 5: leading zero` |
| `1.2.3.4` | `bad version at column 6: more than three components` |
| `1.2.3-rc..1` | `bad version at column 10: expected an identifier` |
| `1.0.0 - 2.0.0` | `bad range at column 7: hyphen ranges are not supported; use >= and <=` |
| `~>1.2` | `bad range at column 3: expected a whole version after '~>'` |
| `>=4294967296` | `bad range at column 3: number over 4294967295` |
| an empty range | `bad range at column 1: empty range; use * for any version` |

- Nothing else changes: the return values are the same, and a parse that succeeds costs
  nothing more. One that fails parses the string again, to find where and why.
- `print` gets each message in one or more pieces, the last ending in `'\n'`. `NULL`, as
  at the start, prints nothing. It is one setting for the whole program, so set it before
  any thread parses. From C++, `nsv::SemVer` and `nsv::SemVerView` print too, through the
  same calls.
- A message shows up to 32 bytes of the string either side of the fault, with `...` where
  it is cut, a byte outside printable ASCII as `\xHH`, and a tab as a tab, so the caret
  lines up under it.
- `NSV_DIAG` is 0 by default, which leaves out all of it: the function, its pointer, and
  every string. On a Cortex-M0+ it adds about 2.3 KB, some 640 bytes of that strings, and
  calls nothing new but `print`.

## Building

Add `nanosemver.c` to your build, as C; from C++, optionally include `nanosemver.hpp`,
which has nothing to build. `nanosemver.c` calls nothing but `memcpy`. On cores without a
fast divide, `nsv_format` divides by shifts and adds, so nothing from libgcc is linked in
either: `NSV_SOFT_DIV` selects that, 1 by default on Cortex-M0/M0+/M1, AVR, MSP430 and
RISC-V without M, 0 elsewhere; define it to override. Built with `-ffunction-sections`
and linked with `--gc-sections`, a program pays only for the functions it calls: on a
Cortex-M0+, range matching adds about 950 bytes to parsing and comparing, and prefix
parsing shares the parser's code.

## Development

`make` builds and runs the unit tests on Linux and macOS; `build.bat` does the same with
MSVC from a Developer Command Prompt. Both fetch the pinned [doctest](https://github.com/doctest/doctest)
through [envy](https://github.com/envy-package-manager/envy) on first use; pass
`DOCTEST_H=/path/to/doctest.h` to skip envy entirely.

| Command | Does |
|---|---|
| `make` | unit tests, built three times: as configured; as for a small MCU, with `-DNSV_INT_MAX=32767` to reach the checks a 16-bit `int` needs, division by shifts, and `nsv_cmp`'s 32-bit compare; and with `-DNSV_DIAG=1`, for its messages; and `nanosemver.hpp` compiled on its own as C++11, 14, 17, 20 and 23, with and without exceptions and RTTI, and without the C++ standard headers |
| `make CC=gcc CXX=g++ CFG=Debug ARCH=32 SAN=address,undefined` | any compiler, configuration, width and sanitizer |
| `make coverage` | clang source coverage; fails unless every region, line and branch of `nanosemver.c` is covered, in all three configurations |
| `make fuzz FUZZ_ARGS="-t 60"` | the standalone fuzz driver, which runs with any compiler, with `-DNSV_DIAG=1` |
| `make libfuzzer` | libFuzzer with ASan and UBSan, with `-DNSV_DIAG=1` (Linux clang; Apple clang ships no libFuzzer) |
| `make bench BENCH_ARGS=--quick` | host timings against `BENCH_BASE`; `--filter cmp` picks workloads |
| `make bench-cm` | emulated Cortex-M0+ and Cortex-M4 instruction counts and cycles against `BENCH_BASE` |
| `make size size-check` | Cortex-M size, code, data and strings, with and without `NSV_DIAG`, and the budget and `memcpy`-only checks |
| `make ratchet` / `make ratchet-update` | Cortex-M code size and cycles held to `size-budget.mk` and `bench/cm/ratchet.txt` / lowered to the code's |
| `make npm-check NPM_ARGS="1000000 7"` | `nsv_range_match` against npm's semver package: generated ranges, and a seed |
| `make mutate MUTATE_ARGS="--jobs 8 --shard 0/4"` | mutation testing: fails on any change to `nanosemver.c` the unit tests miss, unless `tests/mutate-equivalent.txt` explains why it changes nothing |
| `build.bat [fuzz \| clean]` | the unit tests, all three configurations, `nanosemver.hpp` on its own under each C++ standard MSVC has, and optionally the fuzz driver, with MSVC |

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
prefix parse; every range of up to three comparators over whole versions, and of up to two
over whole and partial versions and `!=`, for the matcher; and every byte at every
position of a range. The matcher's hand-written cases are shared with the reference, which
has to give the same answers, and `make npm-check` holds them to npm's.

Some properties need no oracle at all, and `tests/test_range_props.cc` checks them on
random ranges against versions at every boundary: every operator agrees with its
definition in terms of the others, `||` is the union of its sets, spacing, order and
repetition change nothing, a set of bounds admits an unbroken run of versions,
`NSV_RANGE_EXCLUDE_PRE` only ever removes matches and never from a release, and build
metadata on the version never matters.

`tests/test_hpp.cc` holds `nsv::SemVer` and `nsv::SemVerView` to the C API under them:
every parse, prefix parse, comparison, range match and format against the C function's
own answer, `SemVerInplace` at sizes from 2 bytes to 64 KiB, and views of the longest versions
there are; whether a version fits, at every size from 2 to 16 and around 48, 64 and 256;
every error leaving `0.0.0`; and copies that outlive their source string.

With `NSV_DIAG`, `tests/diag/test_diag.cc` holds every message to its exact text, and
every string of up to five bytes as a version and as a range, and everything the fuzzers
try, to a check that shares no code with `nanosemver.c`: the message is rebuilt from its
column and reason, the reason has to fit what is there, and a version's fault has to be
where the reference implementation's longest valid start of a version ends.

`make mutate` measures all of that: it makes a thousand small changes to
`nanosemver.c`, each a flipped comparison, an off-by-one constant, a swapped operator or a
dropped statement, and fails unless the tests catch every one that changes behavior.
The few that cannot, like `k < 3` for `k != 3` in a loop counting up from zero, are listed
with their reasons in `tests/mutate-equivalent.txt`.

## Releases

Push a tag like `v1.2.0` (or `v1.2.0-rc.1`, published as a pre-release). CI runs the
whole matrix against it and, only if everything passes, `release.sh` stamps the tag
into `nanosemver.h` and `nanosemver.hpp` and publishes them as a GitHub release, alongside
`nanosemver.c` and a zip of the full source carrying the same stamped headers. The stamp
is the first line of each, and in `nanosemver.h`, `NSV_VERSION_LIT`
(`NSV_SEMVER_LIT(1, 2, 0, "rc.1", "")`): `nsv_semver_t const v = NSV_VERSION_LIT;` makes
a version of it, for code that compares or reports which nanosemver it has. A stamped
`nanosemver.hpp` refuses to build against any other release's `nanosemver.h`. The
checked-in headers keep their `@NSV_VERSION@` placeholder and 0.0.0, so no commit ever
bumps a version.
`./release.sh v1.2.0` shows locally what a tag would ship.

## License

Dual-licensed under the [Unlicense](https://unlicense.org) and
[0BSD](https://opensource.org/license/0bsd), your pick; see [LICENSE](LICENSE).
