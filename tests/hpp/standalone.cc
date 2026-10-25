// nanosemver.hpp first and alone, built under each C++ standard from C++11 and without
// exceptions or RTTI, by the Makefile and build.bat: compiled, never linked. It calls
// every member and operator of SemVer, at the default size and another, and of SemVerView,
// and compares each with the others, so each standard sees the call sites too. Not
// NSV_SEMVER_LIT: its designated initializers are C++20.
#include "nanosemver.hpp"

template <size_t N>
int nsv_hpp_members(char const *s, size_t len) {
  nsv::SemVer<N> a;
  size_t consumed = 0;
  int r = a.parse(s, len) + a.parse(s) + a.parse_prefix(s, len, consumed);
  int rc = 0;
  nsv::SemVer<N> b = nsv::SemVer<N>::from(a.semver(), rc);
  r += rc;
  nsv::SemVer<N> const e = nsv::SemVer<N>::from(s, len, rc);
  r += rc + static_cast<int>(e.major());
  nsv::SemVer<N> const f = nsv::SemVer<N>::from(s, rc);
  r += rc + static_cast<int>(f.major());
  nsv::SemVer<N> const g = nsv::SemVer<N>::from_prefix(s, len, consumed, rc);
  r += rc + static_cast<int>(g.major());
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

inline int nsv_hpp_view(char const *s, size_t len) {
  nsv::SemVerView a;
  size_t consumed = 0;
  int r = a.parse(s, len) + a.parse(s) + a.parse_prefix(s, len, consumed);
  nsv::SemVerView b(a);
  b = a;
  int rc = 0;
  nsv::SemVerView const d = nsv::SemVerView::from(s, len, rc);
  r += rc + static_cast<int>(d.major());
  nsv::SemVerView const e = nsv::SemVerView::from(s, rc);
  r += rc + static_cast<int>(e.major());
  nsv::SemVerView const f = nsv::SemVerView::from_prefix(s, len, consumed, rc);
  r += rc + static_cast<int>(f.major());
  nsv::SemVer<> const owned;
  nsv::SemVerView const c = owned;

  char buf[32];
  r += b.format(buf, sizeof buf);
  r += b.matches(s, len, NSV_RANGE_EXCLUDE_PRE) + b.matches(s, len) + b.matches(s);
  r += static_cast<int>(b.major() + b.minor() + b.patch() + c.major());
  char const *p = nullptr;
  r += static_cast<int>(b.pre(p) + b.build(p));
  r += static_cast<int>(b.semver().major);
  return r;
}

template <class A, class B>
int nsv_hpp_compare(A const &a, B const &b) {
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
  nsv::SemVerView v;
  a.parse(s, len);
  b.assign(a.semver());
  v.parse(s, len);
  return nsv_hpp_members<64>(s, len) + nsv_hpp_members<2>(s, len) + nsv_hpp_view(s, len) +
         nsv_hpp_compare(a, a) + nsv_hpp_compare(a, b) + nsv_hpp_compare(b, a) +
         nsv_hpp_compare(v, v) + nsv_hpp_compare(v, a) + nsv_hpp_compare(b, v);
}
