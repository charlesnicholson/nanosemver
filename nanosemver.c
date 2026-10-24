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

/* Where v / 10 is a library call, nsv_format divides by shifts and adds instead:
   Cortex-M0/M0+/M1 (ARMv6-M), AVR, MSP430 and RISC-V without M. Elsewhere a multiply
   does it faster. */
#ifndef NSV_SOFT_DIV
#if defined(__ARM_ARCH_6M__) || defined(__AVR__) || defined(__MSP430__) || \
    (defined(__riscv) && !defined(__riscv_div))
#define NSV_SOFT_DIV 1
#else
#define NSV_SOFT_DIV 0
#endif
#endif

/* NSV_NOINLINE keeps a rarer path's code and register pressure out of its caller.
   NSV_INLINE inlines a hot helper into its callers when optimizing for speed, where
   compilers otherwise decide differently with every small change; under -Os the
   compiler chooses, and out of line is also faster on register-starved Thumb-1. */
#if defined(__GNUC__)
#define NSV_NOINLINE __attribute__((noinline))
#ifdef __OPTIMIZE_SIZE__
#define NSV_INLINE
#else
#define NSV_INLINE __inline__ __attribute__((always_inline))
#endif
#elif defined(_MSC_VER)
#define NSV_NOINLINE __declspec(noinline)
#define NSV_INLINE __forceinline
#else
#define NSV_NOINLINE
#define NSV_INLINE
#endif

/* C99 has no alignof; a struct pads its second member out to that member's alignment. */
typedef struct {
  char c;
  nsv_semver_t v;
} nsv_align_probe_t;

static int nsv_is_digit(char c) {
  return ((unsigned)(unsigned char)c - '0') < 10u;
}

/* Scans dot-separated identifiers of [0-9A-Za-z-] in [p, e), which follows a '-', '+'
   or '.'. With pre, all-digit identifiers must be "0" or start with [1-9] (the
   pre-release rule). Returns the end of the longest valid run: the separator before an
   empty identifier, just past the '0' of one with a leading zero, or the end of the
   last. So a whole version ends at e, and a prefix wherever this stops. */
static NSV_INLINE char const *nsv_scan_ids(char const *p, char const *e, unsigned pre) {
  for (;;) {
    char const *const b = p;
    unsigned alpha = 0;
    for (; p != e; ++p) {
      unsigned const c = (unsigned char)*p;
      if ((c - '0') >= 10u) {
        if ((((c | 0x20u) - 'a') >= 26u) && (c != '-')) {
          break;
        }
        alpha = 1;
      }
    }
    if (p == b) {
      return b - 1;
    }
    if (pre && !alpha && (*b == '0') && ((p - b) > 1)) {
      return b + 1;
    }
    if ((p == e) || (*p != '.')) {
      return p;
    }
    ++p;
  }
}

int nsv_parse(nsv_semver_t *out, char const *s, size_t len) {
  char const *p, *e, *pre = NULL, *pre_e = NULL, *build = NULL, *build_e = NULL;
  uint32_t num[3];
  unsigned k, ovf = 0;

  if (!out || !s) {
    return NSV_RET_ERR_ARG;
  }
  p = s;
  e = s + len;

  /* MAJOR.MINOR.PATCH, each "0" or [1-9][0-9]*. A leading zero ends the number after
     the '0', so whatever follows fails the separator check. Overflow is noted, not
     returned: a syntax error later in the string takes precedence. */
  for (k = 0; k < 3u; ++k) {
    uint32_t x, d;
    if ((p == e) || ((x = (uint32_t)(unsigned char)*p - '0') >= 10u)) {
      return NSV_RET_ERR_SYNTAX;
    }
    ++p;
    if (x) {
      while ((p != e) && ((d = (uint32_t)(unsigned char)*p - '0') < 10u)) {
        /* x * 10 + d > UINT32_MAX, without a division; rarely taken. */
        if ((x >= 429496729u) && ((x > 429496729u) || (d > 5u))) {
          ovf = 1;
        }
        x = (x * 10u) + d;
        ++p;
      }
    }
    num[k] = x;
    if (k < 2u) {
      if ((p == e) || (*p != '.')) {
        return NSV_RET_ERR_SYNTAX;
      }
      ++p;
    }
  }

  /* A scan that stops short leaves p before e, and anywhere but a '+' that fails. */
  if ((p != e) && (*p == '-')) {
    pre = ++p;
    pre_e = p = nsv_scan_ids(p, e, 1);
    if ((p != e) && (*p != '+')) {
      return NSV_RET_ERR_SYNTAX;
    }
  }
  if ((p != e) && (*p == '+')) {
    build = ++p;
    build_e = p = nsv_scan_ids(p, e, 0);
  }
  if (p != e) {
    return NSV_RET_ERR_SYNTAX;
  }

  /* Valid semver from here on; what remains is whether it fits. */
  if (ovf) {
    return NSV_RET_ERR_OVERFLOW;
  }
  if (len > UINT16_MAX) {
    return NSV_RET_ERR_TOO_LONG;
  }

  out->major = num[0];
  out->minor = num[1];
  out->patch = num[2];
  out->src = s;
  out->pre_ofs = (uint16_t)(pre ? (pre - s) : 0);
  out->pre_len = (uint16_t)(pre_e - pre);
  out->build_ofs = (uint16_t)(build ? (build - s) : 0);
  out->build_len = (uint16_t)(build_e - build);
  return NSV_RET_OK;
}

/* The length of the longest prefix of [s, e) with semver's syntax, numbers of any size
   and any length; 0 if there is none. A '-' or '+' with no identifier after it is not
   part of it. */
static size_t nsv_prefix_len(char const *s, char const *e) {
  char const *p = s;
  unsigned k;
  for (k = 0; k < 3u; ++k) {
    if ((p == e) || !nsv_is_digit(*p)) {
      return 0;
    }
    if (*p++ != '0') {
      while ((p != e) && nsv_is_digit(*p)) {
        ++p;
      }
    }
    if (k < 2u) {
      if ((p == e) || (*p != '.')) {
        return 0;
      }
      ++p;
    }
  }
  if ((p != e) && (*p == '-')) {
    p = nsv_scan_ids(p + 1, e, 1);
  }
  if ((p != e) && (*p == '+')) {
    p = nsv_scan_ids(p + 1, e, 0);
  }
  return (size_t)(p - s);
}

/* Two passes, so that nsv_parse keeps its single one to itself. */
int nsv_parse_prefix(nsv_semver_t *out, char const *s, size_t len, size_t *consumed) {
  if (!out || !s || !consumed) {
    return NSV_RET_ERR_ARG;
  }
  *consumed = nsv_prefix_len(s, s + len);
  return nsv_parse(out, s, *consumed);
}

static char const *nsv_base(nsv_semver_t const *s) {
  return s->src ? s->src : ((char const *)s + sizeof(*s));
}

/* Semver 11.3-11.4 on two pre-release spans, -1/0/1. Empty means none, which ranks
   above every pre-release. Everything before the first differing byte is equal,
   including where the identifiers split, so only the identifier holding that byte
   matters, and only whether it is all digits: that takes the digit runs either side
   of the difference, never a whole identifier. Identifiers are well-formed, so
   numeric ones have no leading zeros and compare by length first. Out of line: most
   comparisons never get here, and its registers would cost them all a prologue. */
static NSV_NOINLINE int nsv_cmp_pre(char const *a, size_t an, char const *b, size_t bn) {
  size_t const n = (an < bn) ? an : bn;
  size_t i = 0, k, ae, be;
  unsigned num, ad, bd, ca, cb;

  if (!an || !bn) {
    return (an == 0) - (bn == 0);
  }
  while ((i < n) && (a[i] == b[i])) {
    ++i;
  }
  if (i == n) {
    /* One is a prefix of the other. Whether the longer continues with more
       identifiers or a longer last one, the shorter sorts first. */
    return (an > bn) - (an < bn);
  }
  for (k = i; (k > 0) && nsv_is_digit(a[k - 1]); --k) {
  }
  num = (k == 0) || (a[k - 1] == '.'); /* the shared part is all digits */
  for (ae = i; (ae < an) && nsv_is_digit(a[ae]); ++ae) {
  }
  for (be = i; (be < bn) && nsv_is_digit(b[be]); ++be) {
  }
  ad = num && ((ae == an) || (a[ae] == '.'));
  bd = num && ((be == bn) || (b[be] == '.'));
  if (ad != bd) {
    return ad ? -1 : 1; /* numeric < alphanumeric */
  }
  if (ad && (ae != be)) {
    return (ae < be) ? -1 : 1;
  }
  /* Same-length numbers, or ASCII order, where an identifier that ends here is a
     prefix of the other and sorts first. */
  ca = (a[i] == '.') ? 0u : (unsigned char)a[i];
  cb = (b[i] == '.') ? 0u : (unsigned char)b[i];
  return (ca < cb) ? -1 : 1;
}

int nsv_cmp(nsv_semver_t const *a, nsv_semver_t const *b) {
#if UINTPTR_MAX > 0xFFFFFFFFu
  /* 64-bit registers take major and minor as one key, sparing a sort's comparator an
     unpredictable branch. */
  uint64_t x, y;
  if (!a || !b) {
    return NSV_RET_ERR_ARG;
  }
  x = ((uint64_t)a->major << 32) | a->minor;
  y = ((uint64_t)b->major << 32) | b->minor;
  if (x == y) {
    x = a->patch;
    y = b->patch;
    if (x == y) {
      return nsv_cmp_pre(nsv_base(a) + a->pre_ofs, a->pre_len, nsv_base(b) + b->pre_ofs,
                         b->pre_len) + 1;
    }
  }
  return (x < y) ? NSV_CMP_LT : NSV_CMP_GT;
#else
  /* 32-bit cores, where a wide key costs registers: the first unequal field decides,
     written as selects, which compile to the fewest instructions. */
  uint32_t x, y;
  if (!a || !b) {
    return NSV_RET_ERR_ARG;
  }
  x = a->major;
  y = b->major;
  if (x == y) {
    x = a->minor;
    y = b->minor;
  }
  if (x == y) {
    x = a->patch;
    y = b->patch;
  }
  if (x != y) {
    return (x < y) ? NSV_CMP_LT : NSV_CMP_GT;
  }
  return nsv_cmp_pre(nsv_base(a) + a->pre_ofs, a->pre_len, nsv_base(b) + b->pre_ofs,
                     b->pre_len) + 1;
#endif
}

/* The nsv_cmp results a comparator accepts, as a set of bits. */
#define NSV_BIT_LT (1u << NSV_CMP_LT)
#define NSV_BIT_EQ (1u << NSV_CMP_EQ)
#define NSV_BIT_GT (1u << NSV_CMP_GT)

static char const *nsv_skip_ws(char const *p, char const *e) {
  while ((p != e) && ((*p == ' ') || (*p == '\t'))) {
    ++p;
  }
  return p;
}

int nsv_range_match(char const *s, size_t len, nsv_semver_t const *v, unsigned flags) {
  char const *p, *e;
  int ret = 0; /* 1 once a set matches, or the first OVERFLOW or TOO_LONG */
  unsigned fresh;

  if (!s || !v) {
    return NSV_RET_ERR_ARG;
  }
  p = s;
  e = s + len;
  /* A set's state: bit 0 that a comparator rejected v, bit 1 that v may match, which
     under EXCLUDE_PRE a pre-release v does only with a comparator with a pre-release on
     its MAJOR.MINOR.PATCH. The set matches in state 2. */
  fresh = (v->pre_len && (flags & NSV_RANGE_EXCLUDE_PRE)) ? 0u : 2u;
  /* Every set is parsed, even once the answer is known, so a malformed range is always
     an error. */
  for (;;) {
    unsigned state = fresh;
    p = nsv_skip_ws(p, e);
    do {
      nsv_semver_t c;
      char const *q;
      size_t n;
      unsigned accept = NSV_BIT_EQ, keep = 0, same;
      int rc;
      char op;

      /* The operator: the nsv_cmp results it accepts, and for ^ and ~ how many of
         MAJOR.MINOR.PATCH v must share with c, from the left: ~ through MINOR, ^ through
         the leftmost nonzero, worked out below. That is what their upper bounds come to
         (~1.2.3 is <1.3.0-0, ^0.2.3 is <0.3.0-0), without computing one to overflow. */
      op = (p != e) ? *p : '\0';
      if ((op == '<') || (op == '>')) {
        accept = (op == '<') ? NSV_BIT_LT : NSV_BIT_GT;
        if ((++p != e) && (*p == '=')) {
          ++p;
          accept |= NSV_BIT_EQ;
        }
      } else if ((op == '^') || (op == '~')) {
        ++p;
        accept = NSV_BIT_EQ | NSV_BIT_GT;
        keep = (op == '~') ? 2u : 4u;
      } else if (op == '=') {
        ++p;
      }

      p = nsv_skip_ws(p, e);
      rc = nsv_parse_prefix(&c, p, (size_t)(e - p), &n);
      p += n;
      q = nsv_skip_ws(p, e);
      /* A version ends at a space, a tab, a '|' or the end, never at another operator. */
      if ((rc == NSV_RET_ERR_SYNTAX) || ((q == p) && (p != e) && (*p != '|'))) {
        return NSV_RET_ERR_SYNTAX;
      }
      p = q;
      if (rc) {
        if (ret >= 0) {
          ret = rc;
        }
        continue;
      }
      same = (v->major != c.major) ? 0u
             : (v->minor != c.minor) ? 1u
             : (v->patch != c.patch) ? 2u
                                     : 3u;
      if (keep > 3u) {
        keep = c.major ? 1u : (c.minor ? 2u : 3u);
      }
      if (!((accept >> nsv_cmp(v, &c)) & 1u) || (same < keep)) {
        state |= 1u;
      }
      if (c.pre_len && (same == 3u)) {
        state |= 2u;
      }
    } while ((p != e) && (*p != '|'));
    if ((state == 2u) && !ret) {
      ret = 1;
    }
    if (p == e) {
      return ret;
    }
    if ((++p == e) || (*p != '|')) {
      return NSV_RET_ERR_SYNTAX;
    }
    ++p;
  }
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

#if NSV_SOFT_DIV
/* Decimal digits in v, by comparison. */
static NSV_INLINE size_t nsv_u32_len(uint32_t v) {
  size_t n = 1;
  uint32_t p = 10;
  while ((n < 10u) && (v >= p)) {
    ++n;
    p *= 10u;
  }
  return n;
}

/* v / 10 and v % 10 by Hacker's Delight divu10: q is v / 10 or one short, exactly,
   over all of uint32_t. */
static NSV_INLINE uint32_t nsv_div10(uint32_t v, uint32_t *r) {
  uint32_t q = (v >> 1) + (v >> 2);
  q += q >> 4;
  q += q >> 8;
  q += q >> 16;
  q >>= 3;
  *r = v - (q * 10u);
  if (*r > 9u) {
    ++q;
    *r -= 10u;
  }
  return q;
}
#else
/* Decimal digits in v; a divide by a constant is a multiply. */
static NSV_INLINE size_t nsv_u32_len(uint32_t v) {
  size_t n = 1;
  while (v >= 10u) {
    v /= 10u;
    ++n;
  }
  return n;
}

static NSV_INLINE uint32_t nsv_div10(uint32_t v, uint32_t *r) {
  uint32_t const q = v / 10u;
  *r = v - (q * 10u);
  return q;
}
#endif

/* Writes v in decimal ending just before end; returns its first digit. */
static NSV_INLINE char *nsv_put_u32(char *end, uint32_t v) {
  do {
    uint32_t r;
    v = nsv_div10(v, &r);
    *--end = (char)('0' + r);
  } while (v);
  return end;
}

/* Writes sep and len bytes of src ending just before end; returns the start. */
static char *nsv_put_span(char *end, char sep, char const *src, size_t len) {
  if (len) {
    end -= len;
    memcpy(end, src, len);
    *--end = sep;
  }
  return end;
}

int nsv_format(char *dst, size_t n, nsv_semver_t const *s) {
  char const *base;
  size_t len;

  if (!s) {
    return NSV_RET_ERR_ARG;
  }
  len = nsv_u32_len(s->major) + 1u + nsv_u32_len(s->minor) + 1u + nsv_u32_len(s->patch) +
        (s->pre_len ? (s->pre_len + 1u) : 0u) + (s->build_len ? (s->build_len + 1u) : 0u);
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

  /* Back to front, so each number's digits come out least significant first. */
  base = nsv_base(s);
  dst += len;
  *dst = '\0';
  dst = nsv_put_span(dst, '+', base + s->build_ofs, s->build_len);
  dst = nsv_put_span(dst, '-', base + s->pre_ofs, s->pre_len);
  dst = nsv_put_u32(dst, s->patch);
  *--dst = '.';
  dst = nsv_put_u32(dst, s->minor);
  *--dst = '.';
  nsv_put_u32(dst, s->major);
  return (int)len;
}

/* Shared by both accessors, which reduce to a tail call. */
static NSV_NOINLINE int nsv_get_span(nsv_semver_t const *s, uint16_t ofs, uint16_t len,
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
