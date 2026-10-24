/* SPDX-License-Identifier: Unlicense OR 0BSD */
/* nanosemver.hpp: nsv::SemVer, a C++ version that owns its strings. Header-only on top
   of nanosemver.h; link nanosemver.c, built as C. C++11 or later, and needs no
   exceptions, RTTI or standard library. */

#ifndef NANOSEMVER_HPP
#define NANOSEMVER_HPP

#include "nanosemver.h"

namespace nsv {

/* An nsv_copy blob and its owner, one pointer in size: unlike a view from nsv_parse, it
   never dangles, as it needs nothing of the string it came from. Copying a version copies
   the blob, moving it hands the blob over, and destroying it frees the blob. A
   default-constructed or moved-from version is 0.0.0 and holds no blob.
   What can fail returns an NSV_RET_* code and leaves *this unmodified on error. Blobs come
   from ::operator new, so running out of memory is its failure, not an error code:
   std::bad_alloc, or without exceptions, the end of the program. */
class SemVer {
 public:
  constexpr SemVer() noexcept : p_(nullptr) {}

  /* A copy of s: a view, literal, or copy. rc is NSV_RET_OK, or ERR_TOO_LONG, and the
     version 0.0.0, if nsv_copy refuses s, which it does only for a 65535-byte pre-release
     (never one from nsv_parse) or, where int is 16 bits, a copy over INT_MAX bytes. */
  static SemVer from(nsv_semver_t const &s, int &rc) { return SemVer(s, rc); }

  SemVer(SemVer const &o) : p_(nullptr) {
    if (o.p_) {
      assign(*o.p_);  // cannot fail: nsv_copy made it once, so it makes it again
    }
  }
  SemVer(SemVer &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  SemVer &operator=(SemVer const &o) { return *this = SemVer(o); }
  SemVer &operator=(SemVer &&o) noexcept {
    nsv_semver_t *const p = o.p_;  // in this order, self-move changes nothing
    o.p_ = nullptr;
    ::operator delete(p_);
    p_ = p;
    return *this;
  }
  ~SemVer() { ::operator delete(p_); }

  /* nsv_parse of s[0, len), copied into *this. Returns NSV_RET_OK, or ERR_ARG /
     ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG, which also means a version too long to
     copy where int is 16 bits. */
  int parse(char const *s, size_t len) {
    nsv_semver_t v = {};
    int const rc = nsv_parse(&v, s, len);
    return (rc == NSV_RET_OK) ? assign(v) : rc;
  }
  /* As above, for the NUL-terminated s. */
  int parse(char const *s) { return parse(s, length(s)); }

  /* nsv_parse_prefix of s[0, len), copied into *this: the version at the start of s, its
     length in consumed. Returns, and sets consumed, as nsv_parse_prefix does; a version
     too long to copy is ERR_TOO_LONG, with consumed spanning it. */
  int parse_prefix(char const *s, size_t len, size_t &consumed) {
    nsv_semver_t v = {};
    int const rc = nsv_parse_prefix(&v, s, len, &consumed);
    return (rc == NSV_RET_OK) ? assign(v) : rc;
  }

  /* Replaces *this with a copy of s: a view, literal, or copy, *this's own included.
     Returns NSV_RET_OK, or ERR_TOO_LONG if nsv_copy refuses s. */
  int assign(nsv_semver_t const &s) {
    int const n = nsv_copy(nullptr, 0, &s);
    if (n < 0) {
      return n;
    }
    void *const p = ::operator new(static_cast<size_t>(n));
    nsv_copy(p, static_cast<size_t>(n), &s);  // cannot fail: sized for s, aligned by new
    ::operator delete(p_);
    p_ = static_cast<nsv_semver_t *>(p);
    return NSV_RET_OK;
  }

  uint32_t major() const noexcept { return semver().major; }
  uint32_t minor() const noexcept { return semver().minor; }
  uint32_t patch() const noexcept { return semver().patch; }

  /* Pre-release / build metadata, without the leading '-' / '+'. NUL-terminated, "" if
     absent, never NULL; valid until *this is modified or destroyed. */
  char const *pre() const noexcept {
    char const *s = "";
    nsv_get_pre(&semver(), &s);
    return s;
  }
  char const *build() const noexcept {
    char const *s = "";
    nsv_get_build(&semver(), &s);
    return s;
  }

  /* nsv_format: writes "MAJOR.MINOR.PATCH[-PRE][+BUILD]" and a NUL to dst. Never
     truncates. dst NULL: returns the length excluding the NUL; n is ignored.
     Returns the length excluding the NUL, or ERR_TOO_LONG / ERR_BUF_TOO_SMALL
     (dst[0] = '\0' if n > 0). */
  int format(char *dst, size_t n) const noexcept { return nsv_format(dst, n, &semver()); }

  /* nsv_range_match: whether *this satisfies the range in range[0, len), e.g.
     ">=1.2 <1.4 || ^2.1.0", with flags such as NSV_RANGE_EXCLUDE_PRE. Returns 1 if it
     does and 0 if not, or ERR_ARG / ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG: test for 1,
     since an error is nonzero too. */
  int matches(char const *range, size_t len, unsigned flags = 0) const noexcept {
    return nsv_range_match(range, len, &semver(), flags);
  }
  /* As above, for the NUL-terminated range, with no flags. */
  int matches(char const *range) const noexcept { return matches(range, length(range)); }

  /* The version for the C API, valid until *this is modified or destroyed. It is a copy's
     header: pass its address, never copy it, as its strings would stay behind. */
  nsv_semver_t const &semver() const noexcept { return p_ ? *p_ : zero(); }

 private:
  SemVer(nsv_semver_t const &s, int &rc) : p_(nullptr) { rc = assign(s); }  // for from

  static nsv_semver_t const &zero() noexcept {
    static nsv_semver_t const z = {};  // 0.0.0, for every version without a blob
    return z;
  }
  static size_t length(char const *s) noexcept {
    size_t n = 0;
    while (s && s[n]) {
      ++n;
    }
    return n;
  }

  nsv_semver_t *p_;  // the blob, or nullptr for 0.0.0
};

/* By semver precedence, through nsv_cmp. Build metadata never counts, so == is equal
   precedence, not identity: 1.0.0+a == 1.0.0+b. */
inline bool operator==(SemVer const &a, SemVer const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_EQ;
}
inline bool operator!=(SemVer const &a, SemVer const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_EQ;
}
inline bool operator<(SemVer const &a, SemVer const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_LT;
}
inline bool operator<=(SemVer const &a, SemVer const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_GT;
}
inline bool operator>(SemVer const &a, SemVer const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) == NSV_CMP_GT;
}
inline bool operator>=(SemVer const &a, SemVer const &b) noexcept {
  return nsv_cmp(&a.semver(), &b.semver()) != NSV_CMP_LT;
}

}  // namespace nsv

#endif
