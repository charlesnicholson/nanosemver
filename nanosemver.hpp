/* nanosemver @NSV_VERSION@: nsv::SemVer, a C++ version that owns its strings, and
   nsv::SemVerView, one that views the string it was parsed from, over nanosemver.h. https://github.com/charlesnicholson/nanosemver
   charles.nicholson+nanosemver@gmail.com
   dual-licensed under 0bsd and unlicense, take your pick. see LICENSE for details. */
/* SPDX-License-Identifier: Unlicense OR 0BSD */
/* Header-only on top of nanosemver.h; link nanosemver.c, built as C. C++11 or later, and
   needs no heap, exceptions, RTTI or standard library. */

#ifndef NANOSEMVER_HPP
#define NANOSEMVER_HPP

#include "nanosemver.h"

namespace nsv {

namespace detail {
constexpr bool same(char const *a, char const *b) {
  return (*a == *b) && (!*a || same(a + 1, b + 1));
}

inline size_t length(char const *s) noexcept {
  size_t n = 0;
  while (s && s[n]) {
    ++n;
  }
  return n;
}
}  // namespace detail

#if defined(__GNUC__)
#define NSV_HPP_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define NSV_HPP_NOINLINE __declspec(noinline)
#else
#define NSV_HPP_NOINLINE
#endif

/* release.sh stamps this with the release, as it does nanosemver.h's NSV_VERSION, so a
   nanosemver.hpp only builds against the nanosemver.h it shipped with: the whole version,
   so not v1.4.0's against v1.4.0-rc.2's. */
static_assert(detail::same(NSV_VERSION, "0.0.0"),
              "nanosemver.hpp and nanosemver.h are from different releases");

/* An nsv_copy blob held in place: the header, then an array of N bytes, 64 by default, for
   the pre-release and build strings and their NULs. Nothing is allocated, and unlike a
   view from nsv_parse, a version never dangles, as it needs nothing of the string it came
   from. N bytes hold every version of up to N + 4 characters, and a longer one whose
   pre-release and build fit; one that does not fit is NSV_RET_ERR_BUF_TOO_SMALL. A version
   is plain bytes: copying or moving one copies them, and there is nothing to free. A
   default-constructed version is 0.0.0. What can fail returns an NSV_RET_* code and leaves
   *this unmodified on error. Where a version need not outlive its string, SemVerView, below,
   copies nothing and has no length cap. */
template <size_t N = 64>
class SemVer {
  static_assert(N >= 2, "the pre-release and build strings' NULs need 2 bytes");

 public:
  constexpr SemVer() noexcept : b_() {}

  /* A copy of s: a view, literal, or copy. rc is NSV_RET_OK, or ERR_BUF_TOO_SMALL if s
     does not fit, or ERR_TOO_LONG if nsv_copy refuses s, which it does only for a
     65535-byte pre-release (never one from nsv_parse) or, where int is 16 bits, a copy
     over INT_MAX bytes; on error the version is 0.0.0. */
  static SemVer from(nsv_semver_t const &s, int &rc) noexcept {
    SemVer v;
    rc = v.assign(s);
    return v;
  }

  /* nsv_parse of s[0, len), copied into *this. Returns NSV_RET_OK, or ERR_ARG /
     ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG, or for a version that nsv_parse accepts but
     that does not fit, ERR_BUF_TOO_SMALL, or ERR_TOO_LONG where int is 16 bits and its copy
     is over INT_MAX bytes. */
  int parse(char const *s, size_t len) noexcept {
    nsv_semver_t v = {};
    int const rc = nsv_parse(&v, s, len);
    return (rc == NSV_RET_OK) ? assign(v) : rc;
  }
  /* As above, for the NUL-terminated s. */
  int parse(char const *s) noexcept { return parse(s, detail::length(s)); }

  /* nsv_parse_prefix of s[0, len), copied into *this: the version at the start of s, its
     length in consumed. Returns, and sets consumed, as nsv_parse_prefix does; a version
     that does not fit is as with parse, with consumed spanning it. */
  int parse_prefix(char const *s, size_t len, size_t &consumed) noexcept {
    nsv_semver_t v = {};
    int const rc = nsv_parse_prefix(&v, s, len, &consumed);
    return (rc == NSV_RET_OK) ? assign(v) : rc;
  }

  /* Replaces *this with a copy of s: a view, literal, or copy, *this's own included.
     Returns NSV_RET_OK, or ERR_BUF_TOO_SMALL / ERR_TOO_LONG as from does. */
  int assign(nsv_semver_t const &s) noexcept {
    /* Straight into b_, as nsv_copy checks everything before it writes, unless s lies in
       *this, as a view of pre() does: nsv_copy's source and destination must not overlap.
       Capped at the array, not sizeof b_, so nothing goes in tail padding, which copying
       b_ need not carry. */
    if (!apart(s)) {
      return assign_through_copy(s);
    }
    int const n = nsv_copy(&b_, sizeof(nsv_semver_t) + N, &s);
    return (n < 0) ? n : NSV_RET_OK;
  }

  uint32_t major() const noexcept { return b_.v.major; }
  uint32_t minor() const noexcept { return b_.v.minor; }
  uint32_t patch() const noexcept { return b_.v.patch; }

  /* Pre-release / build metadata, without the leading '-' / '+'. NUL-terminated, "" if
     absent, never NULL; valid until *this is modified or destroyed. Read in place, where
     nsv_copy put them: each at its offset into the array, and NUL-terminated even when
     empty. 0.0.0's offsets are 0, and its array all zeros. */
  char const *pre() const noexcept { return b_.s + b_.v.pre_ofs; }
  char const *build() const noexcept { return b_.s + b_.v.build_ofs; }

  /* nsv_format: writes "MAJOR.MINOR.PATCH[-PRE][+BUILD]" and a NUL to dst. Never
     truncates. dst NULL: returns the length excluding the NUL; n is ignored.
     Returns the length excluding the NUL, or ERR_TOO_LONG / ERR_BUF_TOO_SMALL
     (dst[0] = '\0' if n > 0). */
  int format(char *dst, size_t n) const noexcept { return nsv_format(dst, n, &b_.v); }

  /* nsv_range_match: whether *this satisfies the range in range[0, len), e.g.
     ">=1.2 <1.4 || ^2.1.0", with flags such as NSV_RANGE_EXCLUDE_PRE. Returns 1 if it
     does and 0 if not, or ERR_ARG / ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG: test for 1,
     since an error is nonzero too. */
  int matches(char const *range, size_t len, unsigned flags = 0) const noexcept {
    return nsv_range_match(range, len, &b_.v, flags);
  }
  /* As above, for the NUL-terminated range, with no flags. */
  int matches(char const *range) const noexcept {
    return matches(range, detail::length(range));
  }

  /* The version for the C API, valid until *this is modified or destroyed. It is a copy's
     header: pass its address, never copy it, as its strings would stay behind. */
  nsv_semver_t const &semver() const noexcept { return b_.v; }

 private:
  /* Through a temporary SemVer, for an s in *this. Out of line, so only this path takes
     the stack the temporary needs. */
  NSV_HPP_NOINLINE int assign_through_copy(nsv_semver_t const &s) noexcept {
    SemVer t;
    int const n = nsv_copy(&t.b_, sizeof(nsv_semver_t) + N, &s);
    if (n < 0) {
      return n;
    }
    *this = t;
    return NSV_RET_OK;
  }

  /* Whether s, its header and the strings nsv_copy reads from it, lies outside *this. As
     integers, since pointers into unrelated objects have no order. */
  bool apart(nsv_semver_t const &s) const noexcept {
    uintptr_t const h = reinterpret_cast<uintptr_t>(&s);
    uintptr_t const base = s.src ? reinterpret_cast<uintptr_t>(s.src) : (h + sizeof s);
    return outside(h, sizeof s) && outside(base + s.pre_ofs, s.pre_len) &&
           outside(base + s.build_ofs, s.build_len);
  }
  /* [p, p + n) misses b_: it starts at or past b_'s end, or ends at or before its start,
     each a single unsigned compare by wrapping around. An empty span inside b_ does not
     miss it, which only costs the temporary. */
  bool outside(uintptr_t p, size_t n) const noexcept {
    uintptr_t const lo = reinterpret_cast<uintptr_t>(&b_);
    return ((p - lo) >= sizeof b_) && ((lo - p) >= n);
  }

  /* The blob, as nsv_copy lays it out: the header, and its strings right after it. A
     struct, as nanosemver.h suggests for static storage, so the header is a real object,
     aligned, and copying a version copies a blob, which is relocatable: nothing points
     into it, not even from *this. */
  struct blob {
    nsv_semver_t v;
    char s[N];
  } b_;
  static_assert(offsetof(blob, s) == sizeof(nsv_semver_t), "strings right after the header");
};

/* A version that views the string it was parsed from, as nsv_parse's does: nothing is
   copied and nothing caps its length, but the string must outlive the view. Where a
   version has to outlive its string, use SemVer<N>. A view is one header, plain bytes:
   copying it copies the view, not the string. A default-constructed view is 0.0.0. What can
   fail returns an NSV_RET_* code and leaves *this unmodified on error. */
class SemVerView {
 public:
  constexpr SemVerView() noexcept : v_() {}

  /* A view of v's own strings, valid while v is unmodified and alive: so SemVer<N> and
     SemVerView compare with each other, and a SemVer<N> passes for a SemVerView. */
  template <size_t N>
  SemVerView(SemVer<N> const &v) noexcept : v_(v.semver()) {
    v_.src = v.pre() - v_.pre_ofs;  // a copy's strings, where a view's src points
  }

  /* nsv_parse of s[0, len), viewing s. Returns NSV_RET_OK, or ERR_ARG / ERR_SYNTAX /
     ERR_OVERFLOW / ERR_TOO_LONG. */
  int parse(char const *s, size_t len) noexcept { return nsv_parse(&v_, s, len); }
  /* As above, for the NUL-terminated s. */
  int parse(char const *s) noexcept { return parse(s, detail::length(s)); }

  /* nsv_parse_prefix of s[0, len), viewing s: the version at the start of s, its length
     in consumed. Returns, and sets consumed, as nsv_parse_prefix does. */
  int parse_prefix(char const *s, size_t len, size_t &consumed) noexcept {
    return nsv_parse_prefix(&v_, s, len, &consumed);
  }

  uint32_t major() const noexcept { return v_.major; }
  uint32_t minor() const noexcept { return v_.minor; }
  uint32_t patch() const noexcept { return v_.patch; }

  /* Pre-release / build metadata, without the leading '-' / '+': the length, 0 if absent,
     and in s where it starts in the string viewed, or "" if absent; never NULL. Not
     NUL-terminated, as it is the string's own bytes: use the length. */
  size_t pre(char const *&s) const noexcept { return span(v_.pre_ofs, v_.pre_len, s); }
  size_t build(char const *&s) const noexcept { return span(v_.build_ofs, v_.build_len, s); }

  /* nsv_format, as SemVer<N>'s. */
  int format(char *dst, size_t n) const noexcept { return nsv_format(dst, n, &v_); }

  /* nsv_range_match, as SemVer<N>'s. */
  int matches(char const *range, size_t len, unsigned flags = 0) const noexcept {
    return nsv_range_match(range, len, &v_, flags);
  }
  int matches(char const *range) const noexcept {
    return matches(range, detail::length(range));
  }

  /* The version for the C API: a view's header, which may be copied, while the string
     lasts. */
  nsv_semver_t const &semver() const noexcept { return v_; }

 private:
  size_t span(uint16_t ofs, uint16_t len, char const *&s) const noexcept {
    s = len ? (v_.src + ofs) : "";
    return len;
  }

  nsv_semver_t v_;
};

/* By semver precedence, through nsv_cmp: SemVer<N>s of any sizes, and through the
   conversion, SemVerViews and SemVer<N>s in any mix. Build metadata never counts, so == is
   equal precedence, not identity: 1.0.0+a == 1.0.0+b. */
template <size_t A, size_t B>
bool operator==(SemVer<A> const &a, SemVer<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_EQ;
}
template <size_t A, size_t B>
bool operator!=(SemVer<A> const &a, SemVer<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_EQ;
}
template <size_t A, size_t B>
bool operator<(SemVer<A> const &a, SemVer<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_LT;
}
template <size_t A, size_t B>
bool operator<=(SemVer<A> const &a, SemVer<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_GT;
}
template <size_t A, size_t B>
bool operator>(SemVer<A> const &a, SemVer<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_GT;
}
template <size_t A, size_t B>
bool operator>=(SemVer<A> const &a, SemVer<B> const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_LT;
}
inline bool operator==(SemVerView const &a, SemVerView const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_EQ;
}
inline bool operator!=(SemVerView const &a, SemVerView const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_EQ;
}
inline bool operator<(SemVerView const &a, SemVerView const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_LT;
}
inline bool operator<=(SemVerView const &a, SemVerView const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_GT;
}
inline bool operator>(SemVerView const &a, SemVerView const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_GT;
}
inline bool operator>=(SemVerView const &a, SemVerView const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_LT;
}

}  // namespace nsv

#undef NSV_HPP_NOINLINE

#endif
