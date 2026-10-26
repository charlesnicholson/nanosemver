/* nanosemver @NSV_VERSION@: nsv::SemVer, a C++ version that owns its strings, and
   nsv::SemVerView, one that views the string it was parsed from, over nanosemver.h.
   https://github.com/charlesnicholson/nanosemver
   charles.nicholson+nanosemver@gmail.com
   dual-licensed under 0bsd and unlicense, take your pick. see LICENSE for details. */
/* SPDX-License-Identifier: Unlicense OR 0BSD */
/* Header-only over nanosemver.h; link nanosemver.c, built as C. Needs C++11, and no heap,
   exceptions, RTTI or C++ standard library; from the C library, only strlen. */

#ifndef NANOSEMVER_HPP
#define NANOSEMVER_HPP

#include "nanosemver.h"

#include <string.h>  // not <cstring>: avr-g++ has no C++ standard headers

namespace nsv {

namespace detail {
constexpr bool same(char const *a, char const *b) {  // only for the static_assert below
  return (*a == *b) && (!*a || same(a + 1, b + 1));
}

template <size_t N>
struct Copy {  // SemVerInplace's storage: the header, then N bytes for both strings
  static_assert(N >= 2, "the pre-release and build strings' NULs need 2 bytes");
  nsv_semver_t v;
  char s[N];

  int store(nsv_semver_t const &from) noexcept {
    static_assert(offsetof(Copy, s) == sizeof(nsv_semver_t), "strings after the header");
    // Up to the array's end, not sizeof *this: copying *this may skip tail padding.
    int const n = nsv_copy(this, sizeof(nsv_semver_t) + N, &from);
    return (n < 0) ? n : NSV_RET_OK;
  }
};

struct View {  // SemVerView's storage: a header pointing into the parsed string
  nsv_semver_t v;

  int store(nsv_semver_t const &from) noexcept {
    if (!from.src && (from.pre_len || from.build_len)) {  // a copy: no pointer to view
      return NSV_RET_ERR_ARG;
    }
    v = from;
    return NSV_RET_OK;
  }
};

/* A version over the C API, its strings kept by Storage. from() and from_prefix() make
   one, with the error in rc and 0.0.0 on error. A default-constructed version is 0.0.0. */
template <class Storage>
class SemVerImpl {
 public:
  constexpr SemVerImpl() noexcept : s_() {}

  /* nsv_parse of s[0, len), or of the NUL-terminated s. rc is nsv_parse's, or for a
     SemVerInplace, nsv_copy's: ERR_BUF_TOO_SMALL if it does not fit, or ERR_TOO_LONG. */
  static SemVerImpl from(char const *s, size_t len, int &rc) noexcept {
    nsv_semver_t v = {};
    rc = nsv_parse(&v, s, len);
    return SemVerImpl(v, rc);  // built in the caller's variable, as C++17 requires
  }
  static SemVerImpl from(char const *s, int &rc) noexcept {
    return from(s, s ? strlen(s) : 0, rc);  // NULL is nsv_parse's ERR_ARG
  }

  // nsv_parse_prefix of s[0, len): consumed as it sets it, and rc as from() gives it.
  static SemVerImpl from_prefix(char const *s, size_t len, size_t &consumed,
                                int &rc) noexcept {
    nsv_semver_t v = {};
    rc = nsv_parse_prefix(&v, s, len, &consumed);
    return SemVerImpl(v, rc);
  }

  /* c copied (SemVerInplace) or viewed (SemVerView). rc is nsv_copy's, or for a view,
     ERR_ARG if c is a copy's header, which has no pointer to its strings. */
  static SemVerImpl from(nsv_semver_t const &c, int &rc) noexcept {
    rc = NSV_RET_OK;
    return SemVerImpl(c, rc);
  }

  uint32_t major() const noexcept { return s_.v.major; }
  uint32_t minor() const noexcept { return s_.v.minor; }
  uint32_t patch() const noexcept { return s_.v.patch; }

  // nsv_get_pre / nsv_get_build. NUL-terminated in a SemVerInplace, but not in a view.
  int pre(char const *&s) const noexcept { return nsv_get_pre(&s_.v, &s); }
  int build(char const *&s) const noexcept { return nsv_get_build(&s_.v, &s); }

  int format(char *dst, size_t n) const noexcept { return nsv_format(dst, n, &s_.v); }

  // nsv_range_match: 1 if *this is in the range, 0 if not, < 0 on error. Test for 1.
  int matches(char const *range, size_t len, unsigned flags = 0) const noexcept {
    return nsv_range_match(range, len, &s_.v, flags);
  }
  int matches(char const *range) const noexcept {
    return matches(range, range ? strlen(range) : 0);
  }

  // For the C API. A SemVerInplace's is a copy's header: pass its address, never copy it.
  nsv_semver_t const &semver() const noexcept { return s_.v; }

 private:
  SemVerImpl(nsv_semver_t const &v, int &rc) noexcept : s_() {  // new, so never overlaps v
    if (rc == NSV_RET_OK) {
      rc = s_.store(v);
    }
  }

  Storage s_;
};

/* nsv_cmp precedence for any two versions, so build metadata never counts: 1.0.0+a ==
   1.0.0+b. They sit here, next to SemVerImpl, for argument-dependent lookup. */
template <class A, class B>
bool operator==(SemVerImpl<A> const &a, SemVerImpl<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_EQ;
}
template <class A, class B>
bool operator!=(SemVerImpl<A> const &a, SemVerImpl<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_EQ;
}
template <class A, class B>
bool operator<(SemVerImpl<A> const &a, SemVerImpl<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_LT;
}
template <class A, class B>
bool operator<=(SemVerImpl<A> const &a, SemVerImpl<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_GT;
}
template <class A, class B>
bool operator>(SemVerImpl<A> const &a, SemVerImpl<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_GT;
}
template <class A, class B>
bool operator>=(SemVerImpl<A> const &a, SemVerImpl<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_LT;
}

}  // namespace detail

/* release.sh stamps the literal below as it does NSV_VERSION_LIT, so the two quoted
   expansions match only within one release. */
#define NSV_HPP_STR(...) #__VA_ARGS__  // variadic: the expansion has commas
#define NSV_HPP_QUOTE(...) NSV_HPP_STR(__VA_ARGS__)
static_assert(detail::same(NSV_HPP_QUOTE(NSV_VERSION_LIT),
                           NSV_HPP_QUOTE(NSV_SEMVER_LIT(0, 0, 0, "", ""))),
              "nanosemver.hpp and nanosemver.h are from different releases");
#undef NSV_HPP_QUOTE
#undef NSV_HPP_STR

template <size_t N>
using SemVerInplace = detail::SemVerImpl<detail::Copy<N>>;  // N bytes of strings, in place

using SemVer = SemVerInplace<64>;  // holds every version of up to 68 characters

using SemVerView = detail::SemVerImpl<detail::View>;  // its string must outlive it

}  // namespace nsv

#endif
