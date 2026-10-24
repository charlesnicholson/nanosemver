// A deliberately naive semver 2.0.0 implementation, the oracle for the differential
// tests and the fuzzers. It shares no code or structure with nanosemver.c: it splits
// on separators with std::string instead of scanning, and compares numeric
// identifiers by zero-padding them to equal width. tests/test_ref.cc checks it against
// the official regular expression from semver.org.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ref {

// Mirrors the nsv_parse result codes, without depending on them.
enum class status { ok, syntax, overflow, too_long };

struct version {
  std::uint32_t major = 0, minor = 0, patch = 0;
  bool has_pre = false, has_build = false;
  std::string pre, build;  // without the leading '-' / '+'
};

// The same limits as nsv_parse: numbers fit in uint32_t, the string in 65535 bytes,
// and syntax errors take precedence over both.
status parse(std::string_view s, version *out);

// Semver precedence, -1/0/1. Both must have parsed.
int cmp(version const &a, version const &b);

// The longest prefix of s that parse accepts, or would but for the limits: its status,
// and its length in *consumed. syntax and 0 if there is none. Tries every length, longest
// first, so it is quadratic.
status parse_prefix(std::string_view s, version *out, size_t *consumed);

// nsv_range_match's grammar and meaning, by splitting: sets on "||", comparators on
// whitespace, a token that is all operator taking the next as its version, which is
// whole or else partial (1.2, 1.x, *). ^, ~ and partial versions are npm's bounds,
// written out and compared in 64 bits so they cannot overflow; != negates the whole
// line. On ok, *match is whether v satisfies the range; otherwise the status is the
// range's first error, syntax before any other.
status range_match(std::string_view range, version const &v, bool exclude_pre, bool *match);

// One comparator, e.g. ">=1.2": whether v passes it, and whether it names a pre-release
// on v's MAJOR.MINOR.PATCH, which is what lets a pre-release in under exclude_pre.
status term_match(std::string_view comparator, version const &v, bool exclude_pre, bool *pass,
                  bool *names_pre);

}  // namespace ref
