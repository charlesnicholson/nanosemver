// nanosemver.hpp alone, compiled (never linked) as C++11 to C++23, with and without exceptions
// and RTTI, calling every member, and NSV_SEMVER_LIT and NSV_VERSION_LIT.
#include "nanosemver.hpp"

template <class V>
int nsv_hpp_members(char const *s, size_t len) {
  int rc = 0;
  size_t consumed = 0;
  V a = V::from(s, len, rc);
  int r = rc;
  V b = V::from(s, rc);
  r += rc;
  V c = V::from_prefix(s, len, consumed, rc);
  r += rc;
  V d = V::from(a.semver(), rc);
  r += rc;
  V e(b);
  e = c;
  V f(static_cast<V &&>(d));
  f = static_cast<V &&>(e);
  V const g;

  char buf[32];
  r += f.format(buf, sizeof buf);
  r += f.matches(s, len, NSV_RANGE_EXCLUDE_PRE) + f.matches(s, len) + f.matches(s);
  r += static_cast<int>(f.major() + f.minor() + f.patch() + g.major());
  char const *p = nullptr;
  r += f.pre(p) + f.build(p);
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
  int rc = 0;
  nsv::SemVer const a = nsv::SemVer::from(s, len, rc);
  nsv::SemVerInplace<2> const b = nsv::SemVerInplace<2>::from(s, len, rc);
  nsv::SemVerView const v = nsv::SemVerView::from(s, len, rc);
  static nsv_semver_t const lit = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
  static nsv_semver_t const bare = NSV_SEMVER_LIT(2, 0, 0, "", "");
  static nsv_semver_t const version = NSV_VERSION_LIT;
  nsv::SemVer const l = nsv::SemVer::from(lit, rc);
  nsv::SemVerView const lv = nsv::SemVerView::from(bare, rc);
  nsv::SemVerView const lver = nsv::SemVerView::from(version, rc);
  return nsv_hpp_members<nsv::SemVer>(s, len) + nsv_hpp_members<nsv::SemVerInplace<2>>(s, len) +
         nsv_hpp_members<nsv::SemVerView>(s, len) + nsv_hpp_compare(a, a) +
         nsv_hpp_compare(a, b) + nsv_hpp_compare(b, a) + nsv_hpp_compare(v, v) +
         nsv_hpp_compare(v, a) + nsv_hpp_compare(b, v) + nsv_hpp_compare(l, lv) +
         nsv_hpp_compare(lv, lver);
}
