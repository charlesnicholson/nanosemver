// Helpers shared by the unit tests.
#pragma once

#include "guarded.h"
#include "int_max.h"
#include "nanosemver.h"
#include "nsv_doctest.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace nsv_test {

// Parses s, which sits against a guard page so any over-read faults.
inline int parse_rc(std::string_view s) {
  static guarded g;
  nsv_semver_t v{};
  return nsv_parse(&v, g.place(s), s.size());
}

// Parses s or fails the test. The view points into s, which must outlive it.
inline nsv_semver_t parse_ok(std::string const &s) {
  nsv_semver_t v{};
  INFO("parsing \"", s, "\"");
  REQUIRE(nsv_parse(&v, s.data(), s.size()) == NSV_RET_OK);
  return v;
}

inline std::string format(nsv_semver_t const &v) {
  int const n = nsv_format(nullptr, 0, &v);
  REQUIRE(n >= 0);
  std::string s(static_cast<size_t>(n) + 1, '\x7f');
  REQUIRE(nsv_format(&s[0], s.size(), &v) == n);
  REQUIRE(s[static_cast<size_t>(n)] == '\0');
  s.resize(static_cast<size_t>(n));
  return s;
}

inline std::string pre(nsv_semver_t const &v) {
  char const *p = nullptr;
  int const n = nsv_get_pre(&v, &p);
  REQUIRE(n >= 0);
  REQUIRE(p != nullptr);
  return std::string(p, static_cast<size_t>(n));
}

inline std::string build(nsv_semver_t const &v) {
  char const *p = nullptr;
  int const n = nsv_get_build(&v, &p);
  REQUIRE(n >= 0);
  REQUIRE(p != nullptr);
  return std::string(p, static_cast<size_t>(n));
}

struct free_deleter {
  void operator()(void *p) const { std::free(p); }
};
using blob = std::unique_ptr<nsv_semver_t, free_deleter>;

inline blob alloc_blob(size_t n) {
  blob b(static_cast<nsv_semver_t *>(std::malloc(n)));
  REQUIRE(b != nullptr);
  return b;
}

// A deep copy in malloc'd storage, sized by a sizing call.
inline blob copy(nsv_semver_t const &v) {
  int const n = nsv_copy(nullptr, 0, &v);
  REQUIRE(n > 0);
  blob b = alloc_blob(static_cast<size_t>(n));
  REQUIRE(nsv_copy(b.get(), static_cast<size_t>(n), &v) == n);
  return b;
}

// nsv_cmp, recentred to -1/0/1 the way the header suggests for qsort.
inline int cmp(nsv_semver_t const &a, nsv_semver_t const &b) {
  int const c = nsv_cmp(&a, &b);
  REQUIRE(c >= NSV_CMP_LT);
  REQUIRE(c <= NSV_CMP_GT);
  return c - 1;
}

}  // namespace nsv_test
