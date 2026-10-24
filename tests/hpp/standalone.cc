// nanosemver.hpp first and alone, built under each C++ standard from C++11 and without
// exceptions or RTTI, by the Makefile and build.bat: compiled, never linked. It calls
// every member and operator, so each standard sees the call sites too. Not
// NSV_SEMVER_LIT: its designated initializers are C++20.
#include "nanosemver.hpp"

int nsv_hpp_standalone(char const *s, size_t len);

int nsv_hpp_standalone(char const *s, size_t len) {
  nsv::version a;
  size_t consumed = 0;
  int r = a.parse(s, len) + a.parse(s) + a.parse_prefix(s, len, consumed);
  nsv::version b(a.semver());
  r += b.assign(a.semver());
  nsv::version c(b);
  c = a;
  nsv::version d(static_cast<nsv::version &&>(c));
  d = static_cast<nsv::version &&>(b);

  char buf[32];
  r += d.format(buf, sizeof buf);
  r += d.matches(s, len, NSV_RANGE_EXCLUDE_PRE) + d.matches(s, len) + d.matches(s);
  r += static_cast<int>(d.major() + d.minor() + d.patch());
  r += d.pre()[0] + d.build()[0];
  bool const cmp[] = { a == d, a != d, a < d, a <= d, a > d, a >= d };
  for (bool x : cmp) {
    r += x ? 1 : 0;
  }
  return r;
}
