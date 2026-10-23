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

}  // namespace ref
