/* nanosemver @NSV_VERSION@: nsv::SemVer, a C++ version that owns its strings, and
   nsv::SemVerView, one that views the string it was parsed from, over nanosemver.h.
   https://github.com/charlesnicholson/nanosemver
   charles.nicholson+nanosemver@gmail.com
   dual-licensed under 0bsd and unlicense, take your pick. see LICENSE for details. */
/* SPDX-License-Identifier: Unlicense OR 0BSD */
/* Header-only on top of nanosemver.h; link nanosemver.c, built as C. C++11 or later, and
   needs no heap, exceptions or RTTI, and from the standard library only std::strlen. */

#ifndef NANOSEMVER_HPP
#define NANOSEMVER_HPP

#include "nanosemver.h"

#include <cstring>

namespace nsv {

namespace detail {
/* For the static_assert below, which evaluates it at compile time; nothing calls it at
   run time. */
constexpr bool same(char const *a, char const *b) {
  return (*a == *b) && (!*a || same(a + 1, b + 1));
}

/* SemVerN<N>'s storage: an nsv_copy blob in place, the header and then N bytes for the
   pre-release and build strings and their NULs. */
template <size_t N>
struct Copy {
  static_assert(N >= 2, "the pre-release and build strings' NULs need 2 bytes");
  nsv_semver_t v;
  char s[N];

  /* Capped at the array, not sizeof *this, so no string lands in tail padding, which
     copying *this need not carry. */
  int store(nsv_semver_t const &from) noexcept {
    static_assert(offsetof(Copy, s) == sizeof(nsv_semver_t), "strings after the header");
    int const n = nsv_copy(this, sizeof(nsv_semver_t) + N, &from);
    return (n < 0) ? n : NSV_RET_OK;
  }
};

/* SemVerView's storage: a view's header, pointing into the string it came from. */
struct View {
  nsv_semver_t v;

  /* A copy's header does not point at its strings, so it cannot be viewed. */
  int store(nsv_semver_t const &from) noexcept {
    if (!from.src && (from.pre_len || from.build_len)) {
      return NSV_RET_ERR_ARG;
    }
    v = from;
    return NSV_RET_OK;
  }
};

/* A version over the C API, its strings held as Storage holds them: SemVerN<N>, SemVer
   and SemVerView, below. from and from_prefix make one, with any error in rc and the version
   0.0.0 on error; replace one by assigning another. A default-constructed version is
   0.0.0. A version is plain bytes: copying it copies them, and there is nothing to free. */
template <class Storage>
class SemVerImpl {
 public:
  constexpr SemVerImpl() noexcept : s_() {}

  /* nsv_parse of s[0, len), or of the NUL-terminated s: rc is NSV_RET_OK, or ERR_ARG /
     ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG, or for a SemVerN<N>, ERR_BUF_TOO_SMALL if
     the version does not fit, or ERR_TOO_LONG where int is 16 bits and its copy is over
     INT_MAX bytes. */
  static SemVerImpl from(char const *s, size_t len, int &rc) noexcept {
    nsv_semver_t v = {};
    rc = nsv_parse(&v, s, len);
    return stored(v, rc);
  }
  static SemVerImpl from(char const *s, int &rc) noexcept {
    return from(s, s ? std::strlen(s) : 0, rc);  // NULL is nsv_parse's ERR_ARG
  }

  /* nsv_parse_prefix of s[0, len): the version at its start, its length in consumed, as
     nsv_parse_prefix gives them, and rc as from gives it. */
  static SemVerImpl from_prefix(char const *s, size_t len, size_t &consumed,
                                int &rc) noexcept {
    nsv_semver_t v = {};
    rc = nsv_parse_prefix(&v, s, len, &consumed);
    return stored(v, rc);
  }

  /* c, as a SemVerN<N>'s copy, or a SemVerView's view: rc as from gives it, but for a
     view, ERR_ARG if c is a copy's header, which does not point at its strings. */
  static SemVerImpl from(nsv_semver_t const &c, int &rc) noexcept {
    rc = NSV_RET_OK;
    return stored(c, rc);
  }

  uint32_t major() const noexcept { return s_.v.major; }
  uint32_t minor() const noexcept { return s_.v.minor; }
  uint32_t patch() const noexcept { return s_.v.patch; }

  /* nsv_get_pre / nsv_get_build: the length, and in s the bytes, or "" if absent.
     NUL-terminated in a SemVerN<N>; in a view, the string's own bytes, so use the length. */
  int pre(char const *&s) const noexcept { return nsv_get_pre(&s_.v, &s); }
  int build(char const *&s) const noexcept { return nsv_get_build(&s_.v, &s); }

  /* nsv_format: writes "MAJOR.MINOR.PATCH[-PRE][+BUILD]" and a NUL to dst. */
  int format(char *dst, size_t n) const noexcept { return nsv_format(dst, n, &s_.v); }

  /* nsv_range_match: 1 if *this satisfies the range in range[0, len), or the
     NUL-terminated range, and 0 if not, or an error, which is nonzero too: test for 1. */
  int matches(char const *range, size_t len, unsigned flags = 0) const noexcept {
    return nsv_range_match(range, len, &s_.v, flags);
  }
  int matches(char const *range) const noexcept {
    return matches(range, range ? std::strlen(range) : 0);
  }

  /* The version for the C API. A SemVerN<N>'s is a copy's header: pass its address, never
     copy it, as its strings would stay behind. */
  nsv_semver_t const &semver() const noexcept { return s_.v; }

 private:
  static SemVerImpl stored(nsv_semver_t const &v, int &rc) noexcept {
    SemVerImpl x;
    if (rc == NSV_RET_OK) {
      rc = x.s_.store(v);  // a fresh x, so never overlapping v, wherever v's strings are
    }
    return x;
  }

  Storage s_;
};

/* By semver precedence, through nsv_cmp, between any two versions. Here, beside
   SemVerImpl, so that argument-dependent lookup finds them. Build metadata never
   counts, so == is equal precedence, not identity: 1.0.0+a == 1.0.0+b. */
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

/* release.sh stamps this with the release, as it does nanosemver.h's NSV_VERSION, so a
   nanosemver.hpp only builds against the nanosemver.h it shipped with: the whole version,
   so not v1.4.0's against v1.4.0-rc.2's. */
static_assert(detail::same(NSV_VERSION, "0.0.0"),
              "nanosemver.hpp and nanosemver.h are from different releases");

/* A version that owns its strings, N bytes of them in place: it outlives the string it came
   from. */
template <size_t N>
using SemVerN = detail::SemVerImpl<detail::Copy<N>>;

/* SemVerN at 64 bytes, which hold every version of up to 68 characters. */
using SemVer = SemVerN<64>;

/* A version that views the string it came from, which must outlive it: nothing copied,
   and no length cap. */
using SemVerView = detail::SemVerImpl<detail::View>;

}  // namespace nsv

#endif
