/* nanosemver @NSV_VERSION@: in-place Semantic Versioning 2.0.0 (https://semver.org)
   parsing in C99. https://github.com/charlesnicholson/nanosemver
   charles.nicholson+nanosemver@gmail.com
   dual-licensed under 0bsd and unlicense, take your pick. see LICENSE for details. */
/* SPDX-License-Identifier: Unlicense OR 0BSD */

#ifndef NANOSEMVER_H
#define NANOSEMVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All functions return int: >= 0 is success (often a length), < 0 is one of these.
   OVERFLOW and TOO_LONG from nsv_parse mean valid semver beyond nanosemver's limits;
   SYNTAX takes precedence over both. */
enum {
  NSV_RET_OK = 0,
  NSV_RET_ERR_ARG = -1,            /* NULL pointer argument */
  NSV_RET_ERR_SYNTAX = -2,         /* not a valid semver 2.0.0 string */
  NSV_RET_ERR_OVERFLOW = -3,       /* major, minor, or patch > UINT32_MAX */
  NSV_RET_ERR_TOO_LONG = -4,       /* input > UINT16_MAX bytes, or result > INT_MAX */
  NSV_RET_ERR_BUF_TOO_SMALL = -5,  /* destination too small; no partial output */
  NSV_RET_ERR_ALIGN = -6,          /* nsv_copy destination misaligned for nsv_semver_t */
};

/* nsv_cmp results. Values are fixed: (nsv_cmp(a, b) - 1) is a qsort-style -1/0/1. */
enum {
  NSV_CMP_LT = 0,
  NSV_CMP_EQ = 1,
  NSV_CMP_GT = 2,
};

/* A parsed version. major/minor/patch are public. Read pre-release and build metadata
   through nsv_get_pre / nsv_get_build; the remaining fields are representation details:
     view (nsv_parse, NSV_SEMVER_LIT): src points into the caller's string.
     copy (nsv_copy): src is NULL and the strings immediately follow the header, so a
       copy's header must never be copied alone (struct assignment, qsort of headers).
   A zero-initialized nsv_semver_t is a valid 0.0.0. */
typedef struct nsv_semver {
  uint32_t major, minor, patch;
  char const *src;
  uint16_t pre_ofs, pre_len;
  uint16_t build_ofs, build_len;
} nsv_semver_t;

/* Static initializer from literals, e.g. for build stamping:
     static nsv_semver_t const k_ver = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
   Use "" for absent pre-release / build. PRE and BUILD must be string literals (enforced
   by concatenation). Contents are not validated: round-trip through nsv_format and
   nsv_parse in a test. */
#define NSV_SEMVER_LIT(MAJOR, MINOR, PATCH, PRE, BUILD) { \
  .major = (MAJOR), .minor = (MINOR), .patch = (PATCH), \
  .src = PRE "\0" BUILD, \
  .pre_ofs = 0, .pre_len = sizeof(PRE) - 1, \
  .build_ofs = sizeof(PRE), .build_len = sizeof(BUILD) - 1 }

/* Parses exactly len bytes of s (no NUL terminator needed). The whole range must be a
   semver 2.0.0 string: no leading 'v', no whitespace. *out becomes a view into s, so s
   must outlive *out. *out is unmodified on error.
   Returns NSV_RET_OK, or ERR_ARG / ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG. */
int nsv_parse(nsv_semver_t *out, char const *s, size_t len);

/* Compares by semver precedence. Build metadata is ignored, so NSV_CMP_EQ does not mean
   identical. Returns NSV_CMP_LT / NSV_CMP_EQ / NSV_CMP_GT, or ERR_ARG.
   qsort comparator: return nsv_cmp(a, b) - 1; valid for arrays of views. To sort copies,
   sort an array of pointers to them. */
int nsv_cmp(nsv_semver_t const *a, nsv_semver_t const *b);

/* Deep-copies s (view, literal, or copy) into dst as a self-contained, relocatable blob:
   the header followed by NUL-terminated pre-release and build strings. The blob may be
   memcpy'd as a whole; never copy its header alone. dst and s must not overlap.
   dst NULL: returns the required size in bytes; cap is ignored.
   Otherwise dst must be aligned for nsv_semver_t. malloc'd memory is; for static or stack
   storage use a wrapper struct, never a bare char array:
     static struct { nsv_semver_t v; char strs[64]; } g_ver;
     nsv_copy(&g_ver, sizeof g_ver, &view);  // then use &g_ver.v
   Returns bytes written, or ERR_ARG / ERR_ALIGN / ERR_BUF_TOO_SMALL / ERR_TOO_LONG (a
   65535-byte pre-release, whose build offset in the copy would not fit). */
int nsv_copy(void *dst, size_t cap, nsv_semver_t const *s);

/* Writes "MAJOR.MINOR.PATCH[-PRE][+BUILD]" and a NUL terminator to dst. Never truncates.
   dst NULL: returns the length excluding the NUL (allocate ret + 1); n is ignored.
   Returns the length excluding the NUL, or ERR_ARG / ERR_TOO_LONG / ERR_BUF_TOO_SMALL
   (dst[0] = '\0' if n > 0). */
int nsv_format(char *dst, size_t n, nsv_semver_t const *s);

/* Pre-release / build metadata, without the leading '-' / '+'. Returns the length (0 if
   absent), or ERR_ARG if s is NULL, or ERR_TOO_LONG if int is 16 bits and the length
   exceeds INT_MAX. If out is non-NULL, *out is set to the string, or "" if absent; never
   NULL. Only copies and literals are NUL-terminated, so always use the returned length,
   e.g. printf("%.*s", len, str). */
int nsv_get_pre(nsv_semver_t const *s, char const **out);
int nsv_get_build(nsv_semver_t const *s, char const **out);

#ifdef __cplusplus
}
#endif

#endif
