/* nanosemver @NSV_VERSION@: in-place Semantic Versioning 2.0.0 (https://semver.org)
   parsing in C99. https://github.com/charlesnicholson/nanosemver
   charles.nicholson+nanosemver@gmail.com
   dual-licensed under 0bsd and unlicense, take your pick. see LICENSE for details. */
/* SPDX-License-Identifier: Unlicense OR 0BSD */

#ifndef NANOSEMVER_H
#define NANOSEMVER_H

#include <stddef.h>
#include <stdint.h>

/* 1 compiles in nsv_diag_set_print, for messages that say why a version or range failed to
   parse; define it for nanosemver.c and its callers alike. 0, the default, leaves out all
   of it, strings included. */
#ifndef NSV_DIAG
#define NSV_DIAG 0
#endif

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

/* This header's release, stamped from its tag (0.0.0 unreleased), as an NSV_SEMVER_LIT:
   nsv_semver_t const v = NSV_VERSION_LIT; makes an nsv_semver_t of it, to compare. */
#define NSV_VERSION_LIT NSV_SEMVER_LIT(0, 0, 0, "", "")

/* Parses exactly len bytes of s (no NUL terminator needed). The whole range must be a
   semver 2.0.0 string: no leading 'v', no whitespace. *out becomes a view into s, so s
   must outlive *out. *out is unmodified on error.
   Returns NSV_RET_OK, or ERR_ARG / ERR_SYNTAX / ERR_OVERFLOW / ERR_TOO_LONG. */
int nsv_parse(nsv_semver_t *out, char const *s, size_t len);

/* Parses the version at the start of s[0, len), in the manner of from_chars: the longest
   prefix that is a semver 2.0.0 string, whose length goes in *consumed. What follows is
   the caller's to check: "1.2.3, 2.0.0" gives 1.2.3, and so does "1.2.3.4", and "1.2.03"
   gives 1.2.0. *consumed is 0 on ERR_SYNTAX, where no prefix is a version, and on
   ERR_OVERFLOW and ERR_TOO_LONG it spans the version that does not fit. *out is unmodified
   on error. Returns as nsv_parse does. */
int nsv_parse_prefix(nsv_semver_t *out, char const *s, size_t len, size_t *consumed);

/* Compares by semver precedence. Build metadata is ignored, so NSV_CMP_EQ does not mean
   identical. Returns NSV_CMP_LT / NSV_CMP_EQ / NSV_CMP_GT, or ERR_ARG.
   qsort comparator: return nsv_cmp(a, b) - 1; valid for arrays of views. To sort copies,
   sort an array of pointers to them. */
int nsv_cmp(nsv_semver_t const *a, nsv_semver_t const *b);

/* nsv_range_match flags. */
enum {
  /* npm's default: a version with a pre-release satisfies a comparator set only if a
     comparator in it has a pre-release on the same MAJOR.MINOR.PATCH (!= and partial
     versions never do). So >=1.0.0 <2.0.0 rejects 1.5.0-beta, and >=1.5.0-beta <2.0.0
     admits 1.5.0-rc.1 but not 1.6.0-rc.1. */
  NSV_RANGE_EXCLUDE_PRE = 1,
};

/* Whether v satisfies the range in s[0, len), e.g. ">=1.2 <1.4 || ^2.1.0". No standard
   defines ranges; this grammar is npm's (https://github.com/npm/node-semver#range-grammar)
   without hyphen ranges, plus != and the few spellings from other grammars that mean the
   same in all of them, and npm reads every range of it, spelled as npm's, the same way:
     range       comparator sets separated by "||": v satisfies any one of them
     set         comparators separated by spaces, tabs or a comma, or by nothing before
                 an operator (>=1.0.0<2.0.0): v satisfies all of them
     comparator  an operator, optional spaces, and a version: a semver 2.0.0 version V
                 or a partial version P below, either of which may start with a v
                 (v1.2.3, as npm has it)
       <V <=V >V >=V   by nsv_cmp precedence
       V =V ==V        equal precedence, so build metadata never matters
       !=V             not equal; != and == are PEP 440's and Composer's
       ~V ~=V ~>V      >=V, with the same MAJOR.MINOR: ~1.2.3 is >=1.2.3 <1.3.0-0; ~= is
                       PEP 440's, ~> RubyGems' and Hex's
       ^V              >=V, with the same leftmost nonzero of MAJOR.MINOR.PATCH:
                       ^1.2.3 is >=1.2.3 <2.0.0-0, ^0.2.3 is >=0.2.3 <0.3.0-0, and
                       ^0.0.3 is >=0.0.3 <0.0.4-0
     or a partial version P, up to three numbers then wildcards (x, X or *), with no
     pre-release or build, which stands for its whole line, pre-releases and all:
       P =P            1.2 and 1.2.x are >=1.2.0-0 <1.3.0-0, and * is everything
       <P <=P >P >=P   <1.2 is <1.2.0-0, <=1.2 is <1.3.0-0, >1.2 is >=1.3.0-0
       !=P             off the line, with a wildcard: !=1.4.x is <1.4.0-0 or >=1.5.0-0
       ~P ^P           ~1 and ^1.2 are >=1.0.0-0 <2.0.0-0 and >=1.2.0-0 <2.0.0-0
   ==, ~= and ~> take no partial version, and != none without a wildcard, since their
   grammars read ==1.2 and !=1.2 as 1.2.0, and ~=1.2 and ~>1.2 as >=1.2 <2. Spaces and
   commas only separate, so there are no hyphen ranges, whose spaces are all that tells
   1.2.3 - 2.3.4 from the version 1.2.3-2.3.4; and an empty range is a syntax error.
   By default a pre-release is just a version ranking below its release, as with npm's
   includePrerelease, so >=1.0.0 <2.0.0 admits 2.0.0-rc.1: write <2.0.0-0 to stop below
   every 2.0.0 pre-release, as ^ and ~ do. NSV_RANGE_EXCLUDE_PRE is npm's default, under
   which a partial version's lower bound is its .0 release (>=1.2 is >=1.2.0). It differs
   only where npm reads >=0.0.0 as * and a set of * as the whole range.
   Returns 1 if v satisfies the range and 0 if not, or ERR_ARG / ERR_SYNTAX, or
   ERR_OVERFLOW / ERR_TOO_LONG if a version in the range is beyond nanosemver's limits.
   The whole range is always parsed, so a malformed one is ERR_SYNTAX however v compares. */
int nsv_range_match(char const *s, size_t len, nsv_semver_t const *v, unsigned flags);

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

#if NSV_DIAG
/* Takes a message in one or more pieces, the last ending in '\n', as fwrite(s, 1, len, ctx)
   might. */
typedef void (*nsv_diag_print_t)(void *ctx, char const *s, size_t len);

/* Has each ERR_SYNTAX, ERR_OVERFLOW and ERR_TOO_LONG from nsv_parse, nsv_parse_prefix and
   nsv_range_match say where and why through print(ctx, ...), until print is NULL, as at
   the start. Nothing else changes, return values included. It is one setting for the whole
   program, so set it before any thread parses:
     bad range at column 10: expected a version
       >=1.0.0 <
                ^ */
void nsv_diag_set_print(nsv_diag_print_t print, void *ctx);
#endif

#ifdef __cplusplus
}
#endif

#endif
