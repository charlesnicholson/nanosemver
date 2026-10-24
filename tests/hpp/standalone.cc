// nanosemver.hpp first and alone, built under each C++ standard from C++11 and without
// exceptions or RTTI, by the Makefile and build.bat: compiled, never linked. It calls
// every member and operator, of the default size and another, and compares the two, so
// each standard sees the call sites too. Not NSV_SEMVER_LIT: its designated initializers
// are C++20.
#include "nanosemver.hpp"

template <size_t N>
int nsv_hpp_members(char const *s, size_t len) {
  nsv::SemVer<N> a;
  size_t consumed = 0;
  int r = a.parse(s, len) + a.parse(s) + a.parse_prefix(s, len, consumed);
  int rc = 0;
  nsv::SemVer<N> b = nsv::SemVer<N>::from(a.semver(), rc);
  r += rc;
  r += b.assign(a.semver());
  nsv::SemVer<N> c(b);
  c = a;
  nsv::SemVer<N> d(static_cast<nsv::SemVer<N> &&>(c));
  d = static_cast<nsv::SemVer<N> &&>(b);

  char buf[32];
  r += d.format(buf, sizeof buf);
  r += d.matches(s, len, NSV_RANGE_EXCLUDE_PRE) + d.matches(s, len) + d.matches(s);
  r += static_cast<int>(d.major() + d.minor() + d.patch());
  r += d.pre()[0] + d.build()[0];
  return r;
}

template <size_t A, size_t B>
int nsv_hpp_compare(nsv::SemVer<A> const &a, nsv::SemVer<B> const &b) {
  bool const cmp[] = { a == b, a != b, a < b, a <= b, a > b, a >= b };
  int r = 0;
  for (bool x : cmp) {
    r += x ? 1 : 0;
  }
  return r;
}

inline int nsv_hpp_standalone(char const *s, size_t len) {
  nsv::SemVer<> a;
  nsv::SemVer<2> b;
  a.parse(s, len);
  b.assign(a.semver());
  return nsv_hpp_members<48>(s, len) + nsv_hpp_members<2>(s, len) +
         nsv_hpp_compare(a, a) + nsv_hpp_compare(a, b) + nsv_hpp_compare(b, a);
}
