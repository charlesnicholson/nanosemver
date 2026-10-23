/* SPDX-License-Identifier: Unlicense OR 0BSD */
#include "nanosemver.h"

#include <limits.h>
#include <string.h>

/* With a 32-bit int every length fits in the int return values. Narrower ints need
   the NSV_RET_ERR_TOO_LONG checks; defining NSV_INT_MAX below INT_MAX compiles them
   in on a 32-bit host so the tests can reach them. */
#ifndef NSV_INT_MAX
#define NSV_INT_MAX INT_MAX
#endif

/* C99 has no alignof; a struct pads its second member out to that member's alignment. */
typedef struct {
  char c;
  nsv_semver_t v;
} nsv_align_probe_t;

static int nsv_is_digit(char c) {
  return ((unsigned)(unsigned char)c - '0') < 10u;
}

/* [0-9A-Za-z-] */
static int nsv_is_ident(char c) {
  unsigned const u = (unsigned char)c;
  return ((u - '0') < 10u) || (((u | 0x20u) - 'a') < 26u) || (u == '-');
}

/* s[i], or NUL past the end. Callers only compare the result against '.', '-', '+'. */
static char nsv_peek(char const *s, size_t n, size_t i) {
  return (i < n) ? s[i] : '\0';
}

/* Scans a numeric identifier, "0" or [1-9][0-9]*, into *v and sets *ovf if it exceeds
   UINT32_MAX. A leading zero ends the scan after the '0', so the caller's next
   expectation fails. Returns the index past it, or 0 if s[i] is not a digit. */
static size_t nsv_scan_num(char const *s, size_t n, size_t i, uint32_t *v, int *ovf) {
  uint32_t x = 0;
  if ((i == n) || !nsv_is_digit(s[i])) {
    return 0;
  }
  if (s[i] == '0') {
    ++i;
  } else {
    do {
      uint32_t const d = (uint32_t)(unsigned char)s[i] - '0';
      if (x > ((UINT32_MAX - d) / 10u)) {
        *ovf = 1;
      } else {
        x = (x * 10u) + d;
      }
    } while ((++i < n) && nsv_is_digit(s[i]));
  }
  *v = x;
  return i;
}

/* Scans dot-separated identifiers of [0-9A-Za-z-]. With no_leading_zero, all-digit
   identifiers must be "0" or start with [1-9] (pre-release rule). Returns the index
   past the last identifier, or 0 on an empty identifier or a leading zero. */
static size_t nsv_scan_ids(char const *s, size_t n, size_t i, int no_leading_zero) {
  for (;;) {
    size_t const b = i;
    int digits = 1;
    while ((i < n) && nsv_is_ident(s[i])) {
      digits &= nsv_is_digit(s[i]);
      ++i;
    }
    if ((i == b) || (no_leading_zero && digits && (s[b] == '0') && ((i - b) > 1u))) {
      return 0;
    }
    if (nsv_peek(s, n, i) != '.') {
      return i;
    }
    ++i;
  }
}

int nsv_parse(nsv_semver_t *out, char const *s, size_t len) {
  nsv_semver_t v;
  size_t i, pre_b = 0, pre_e = 0, build_b = 0, build_e = 0;
  int ovf = 0;

  if (!out || !s) {
    return NSV_RET_ERR_ARG;
  }

  i = nsv_scan_num(s, len, 0, &v.major, &ovf);
  if (!i || (nsv_peek(s, len, i) != '.')) {
    return NSV_RET_ERR_SYNTAX;
  }
  i = nsv_scan_num(s, len, i + 1, &v.minor, &ovf);
  if (!i || (nsv_peek(s, len, i) != '.')) {
    return NSV_RET_ERR_SYNTAX;
  }
  i = nsv_scan_num(s, len, i + 1, &v.patch, &ovf);
  if (!i) {
    return NSV_RET_ERR_SYNTAX;
  }

  if (nsv_peek(s, len, i) == '-') {
    pre_b = i + 1;
    pre_e = i = nsv_scan_ids(s, len, pre_b, 1);
    if (!i) {
      return NSV_RET_ERR_SYNTAX;
    }
  }
  if (nsv_peek(s, len, i) == '+') {
    build_b = i + 1;
    build_e = i = nsv_scan_ids(s, len, build_b, 0);
    if (!i) {
      return NSV_RET_ERR_SYNTAX;
    }
  }
  if (i != len) {
    return NSV_RET_ERR_SYNTAX;
  }

  /* Valid semver from here on; what remains is whether it fits. */
  if (ovf) {
    return NSV_RET_ERR_OVERFLOW;
  }
  if (len > UINT16_MAX) {
    return NSV_RET_ERR_TOO_LONG;
  }

  v.src = s;
  v.pre_ofs = (uint16_t)pre_b;
  v.pre_len = (uint16_t)(pre_e - pre_b);
  v.build_ofs = (uint16_t)build_b;
  v.build_len = (uint16_t)(build_e - build_b);
  *out = v;
  return NSV_RET_OK;
}

static char const *nsv_base(nsv_semver_t const *s) {
  return s->src ? s->src : ((char const *)s + sizeof(*s));
}

static int nsv_cmp_u32(uint32_t a, uint32_t b) {
  return (a > b) - (a < b);
}

/* Semver 11.3-11.4 on two pre-release spans, -1/0/1. Empty means none, which ranks
   above every pre-release. Identifiers are well-formed, so numeric ones have no
   leading zeros and compare by length, then digit by digit, at any size. */
static int nsv_cmp_pre(char const *a, size_t an, char const *b, size_t bn) {
  size_t i = 0, j = 0;
  if (!an || !bn) {
    return (an == 0) - (bn == 0);
  }
  for (;;) {
    size_t ie = i, je = j, al, bl;
    int ad = 1, bd = 1, c;
    while ((ie < an) && (a[ie] != '.')) {
      ad &= nsv_is_digit(a[ie++]);
    }
    while ((je < bn) && (b[je] != '.')) {
      bd &= nsv_is_digit(b[je++]);
    }
    if (ad != bd) {
      return ad ? -1 : 1; /* numeric < alphanumeric */
    }
    al = ie - i;
    bl = je - j;
    if (ad && (al != bl)) {
      return (al < bl) ? -1 : 1;
    }
    c = memcmp(a + i, b + j, (al < bl) ? al : bl);
    if (c) {
      return (c < 0) ? -1 : 1;
    }
    if (al != bl) {
      return (al < bl) ? -1 : 1; /* a prefix sorts first */
    }
    if ((ie == an) || (je == bn)) {
      return (ie != an) - (je != bn); /* fewer identifiers sorts first */
    }
    i = ie + 1;
    j = je + 1;
  }
}

int nsv_cmp(nsv_semver_t const *a, nsv_semver_t const *b) {
  int c;
  if (!a || !b) {
    return NSV_RET_ERR_ARG;
  }
  c = nsv_cmp_u32(a->major, b->major);
  if (!c) {
    c = nsv_cmp_u32(a->minor, b->minor);
  }
  if (!c) {
    c = nsv_cmp_u32(a->patch, b->patch);
  }
  if (!c) {
    c = nsv_cmp_pre(nsv_base(a) + a->pre_ofs, a->pre_len, nsv_base(b) + b->pre_ofs,
                    b->pre_len);
  }
  return c + 1; /* NSV_CMP_LT / NSV_CMP_EQ / NSV_CMP_GT */
}

int nsv_copy(void *dst, size_t cap, nsv_semver_t const *s) {
  nsv_semver_t h;
  char const *base;
  char *d;
  size_t need;

  if (!s) {
    return NSV_RET_ERR_ARG;
  }
  /* The build string's offset in the copy is pre_len + 1, which has to fit too. */
  if (s->pre_len == UINT16_MAX) {
    return NSV_RET_ERR_TOO_LONG;
  }
  need = sizeof(h) + s->pre_len + 1u + s->build_len + 1u;
#if NSV_INT_MAX < 0x30000L
  if (need > (size_t)NSV_INT_MAX) {
    return NSV_RET_ERR_TOO_LONG;
  }
#endif
  if (!dst) {
    return (int)need;
  }
  if ((uintptr_t)dst % offsetof(nsv_align_probe_t, v)) {
    return NSV_RET_ERR_ALIGN;
  }
  if (cap < need) {
    return NSV_RET_ERR_BUF_TOO_SMALL;
  }

  base = nsv_base(s);
  h = *s;
  h.src = NULL;
  h.pre_ofs = 0;
  h.build_ofs = (uint16_t)(s->pre_len + 1u);

  d = (char *)dst + sizeof(h);
  memcpy(d, base + s->pre_ofs, s->pre_len);
  d[s->pre_len] = '\0';
  d += h.build_ofs;
  memcpy(d, base + s->build_ofs, s->build_len);
  d[s->build_len] = '\0';

  *(nsv_semver_t *)dst = h;
  return (int)need;
}

static size_t nsv_u32_len(uint32_t v) {
  size_t n = 1;
  while (v >= 10u) {
    v /= 10u;
    ++n;
  }
  return n;
}

static char *nsv_put_u32(char *d, uint32_t v, size_t len) {
  char *p = d + len;
  do {
    *--p = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v);
  return d + len;
}

static char *nsv_put_span(char *d, char sep, char const *src, size_t len) {
  if (len) {
    *d++ = sep;
    memcpy(d, src, len);
    d += len;
  }
  return d;
}

int nsv_format(char *dst, size_t n, nsv_semver_t const *s) {
  char const *base;
  size_t l0, l1, l2, len;

  if (!s) {
    return NSV_RET_ERR_ARG;
  }
  l0 = nsv_u32_len(s->major);
  l1 = nsv_u32_len(s->minor);
  l2 = nsv_u32_len(s->patch);
  len = l0 + 1u + l1 + 1u + l2 + (s->pre_len ? (s->pre_len + 1u) : 0u) +
        (s->build_len ? (s->build_len + 1u) : 0u);
#if NSV_INT_MAX < 0x30000L
  if (len > (size_t)NSV_INT_MAX) {
    return NSV_RET_ERR_TOO_LONG;
  }
#endif
  if (!dst) {
    return (int)len;
  }
  if (n <= len) {
    if (n) {
      dst[0] = '\0';
    }
    return NSV_RET_ERR_BUF_TOO_SMALL;
  }

  base = nsv_base(s);
  dst = nsv_put_u32(dst, s->major, l0);
  *dst++ = '.';
  dst = nsv_put_u32(dst, s->minor, l1);
  *dst++ = '.';
  dst = nsv_put_u32(dst, s->patch, l2);
  dst = nsv_put_span(dst, '-', base + s->pre_ofs, s->pre_len);
  dst = nsv_put_span(dst, '+', base + s->build_ofs, s->build_len);
  *dst = '\0';
  return (int)len;
}

static int nsv_get_span(nsv_semver_t const *s, uint16_t ofs, uint16_t len,
                        char const **out) {
#if NSV_INT_MAX < 0x30000L
  if ((unsigned)len > (unsigned)NSV_INT_MAX) {
    return NSV_RET_ERR_TOO_LONG;
  }
#endif
  if (out) {
    *out = len ? (nsv_base(s) + ofs) : "";
  }
  return (int)len;
}

int nsv_get_pre(nsv_semver_t const *s, char const **out) {
  return s ? nsv_get_span(s, s->pre_ofs, s->pre_len, out) : NSV_RET_ERR_ARG;
}

int nsv_get_build(nsv_semver_t const *s, char const **out) {
  return s ? nsv_get_span(s, s->build_ofs, s->build_len, out) : NSV_RET_ERR_ARG;
}
