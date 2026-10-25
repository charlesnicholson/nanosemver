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

/* nsv_cmp takes major and minor as one 64-bit key where registers are that wide, and
   compares field by field elsewhere. Defining NSV_WIDE_CMP picks one, so the tests build
   both on any host. */
#ifndef NSV_WIDE_CMP
#if UINTPTR_MAX > 0xFFFFFFFFu
#define NSV_WIDE_CMP 1
#else
#define NSV_WIDE_CMP 0
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

/* nsv_parse, and with consumed nsv_parse_prefix: one pass over the longest prefix of s
   with semver's syntax, which nsv_parse requires to be all of s. */
static int nsv_parse_to(nsv_semver_t *out, char const *s, size_t len, size_t *consumed) {
  char const *p, *e, *q, *pre = NULL, *pre_e = NULL, *build = NULL, *build_e = NULL;
  uint32_t num[3], *n;
  unsigned ovf = 0;

  if (!out || !s) {
    return NSV_RET_ERR_ARG;
  }
  p = s;
  e = s + len;

  /* MAJOR.MINOR.PATCH, each "0" or [1-9][0-9]*. A leading zero ends the number after
     the '0', so whatever follows fails the separator check. Overflow is noted, not
     returned: a syntax error later in the string takes precedence. */
  for (n = num;; ++p) {
    uint32_t x, d;
    if ((p == e) || ((x = (uint32_t)(unsigned char)*p - '0') >= 10u)) {
      goto syntax;
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
    *n++ = x;
    if (n == (num + 3)) {
      break;
    }
    if ((p == e) || (*p != '.')) {
      goto syntax;
    }
  }

  /* A '-' or '+' with no identifier after it is not part of a prefix, and a scan that
     stops short leaves p before e, which only nsv_parse_prefix accepts. */
  if ((p != e) && (*p == '-') && ((q = nsv_scan_ids(p + 1, e, 1)) != p)) {
    pre = p + 1;
    pre_e = p = q;
  }
  if ((p != e) && (*p == '+') && ((q = nsv_scan_ids(p + 1, e, 0)) != p)) {
    build = p + 1;
    build_e = p = q;
  }
  if (consumed) {
    *consumed = (size_t)(p - s);
  } else if (p != e) {
    return NSV_RET_ERR_SYNTAX;
  }

  /* Valid semver from here on; what remains is whether it fits. */
  if (ovf) {
    return NSV_RET_ERR_OVERFLOW;
  }
  if ((size_t)(p - s) > UINT16_MAX) {
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

syntax:
  if (consumed) {
    *consumed = 0;
  }
  return NSV_RET_ERR_SYNTAX;
}

/* With NSV_DIAG, a parse that fails on what it was given, not ERR_ARG, is walked again to
   print where and why. rc is a variable, so it can be read twice. */
#if NSV_DIAG
static int nsv_diag_print_bad_version(int rc, char const *s, size_t len);
static int nsv_diag_print_bad_range(int rc, char const *s, size_t len);
#define NSV_DIAG_PRINT_BAD(what, rc, s, len) \
  (((rc) < NSV_RET_ERR_ARG) ? nsv_diag_print_bad_##what((rc), (s), (len)) : (rc))
#else
#define NSV_DIAG_PRINT_BAD(what, rc, s, len) (rc)
#endif

int nsv_parse(nsv_semver_t *out, char const *s, size_t len) {
  int const rc = nsv_parse_to(out, s, len, NULL);
  return NSV_DIAG_PRINT_BAD(version, rc, s, len);
}

int nsv_parse_prefix(nsv_semver_t *out, char const *s, size_t len, size_t *consumed) {
  int const rc = consumed ? nsv_parse_to(out, s, len, consumed) : NSV_RET_ERR_ARG;
  return NSV_DIAG_PRINT_BAD(version, rc, s, len);
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
#if NSV_WIDE_CMP
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

/* A comparator's operator, packed: the nsv_cmp results it accepts as bits 0-2; how many
   of MAJOR.MINOR.PATCH v must share with the bound, from the left, as bits 3-5, where 4
   means ^'s leftmost nonzero (= and != say 3, which an equal version shares anyway);
   NSV_HOW_NOT to negate the test, for != and a partial version's <= (<=1.2 is not >1.2),
   and NSV_HOW_PARTIAL for a partial version's bound, neither of which ever lets a
   pre-release in; NSV_HOW_EXCL under NSV_RANGE_EXCLUDE_PRE; NSV_HOW_OVF from a partial
   version too big; and NSV_HOW_WHOLE for ==, ~= and ~>, which take no partial version. */
#define NSV_BIT_LT (1u << NSV_CMP_LT)
#define NSV_BIT_EQ (1u << NSV_CMP_EQ)
#define NSV_BIT_GT (1u << NSV_CMP_GT)
#define NSV_HOW_KEEP(n) ((unsigned)(n) << 3)
#define NSV_HOW_NOT (1u << 6)
#define NSV_HOW_PARTIAL (1u << 7)
#define NSV_HOW_EXCL (1u << 8)
#define NSV_HOW_OVF (1u << 9)
#define NSV_HOW_WHOLE (1u << 10)

static char const *nsv_skip_ws(char const *p, char const *e) {
  while ((p != e) && ((*p == ' ') || (*p == '\t'))) {
    ++p;
  }
  return p;
}

/* A partial version at the start of [s, e), as npm's ranges have them: up to three
   components, each a number as in a version or a wildcard (x, X or *), numbers first,
   and no pre-release or build. Only for what nsv_parse_prefix rejects, so never three
   numbers. Returns its end, or NULL for a syntax error, and makes c and *how the bound
   npm reads it as, which the rest of nsv_range_match takes as any other: 1.2 is
   >=1.2.0-0 sharing MAJOR.MINOR, >1.2 is >=1.3.0-0, <=1.2 is <1.3.0-0, and * is
   >=0.0.0-0. Lower bounds lose the -0 under NSV_RANGE_EXCLUDE_PRE, as npm's default
   drops it: >=1.2 is then >=1.2.0, which 1.2.0-beta is not. */
static NSV_NOINLINE char const *nsv_parse_partial(nsv_semver_t *c, char const *p,
                                                  char const *e, unsigned *how) {
  unsigned i = 0, k = 0, keep, h = *how | NSV_HOW_PARTIAL;
  unsigned const acc = h & 7u;
  c->major = c->minor = c->patch = 0;
  for (;;) {
    unsigned ch;
    uint32_t x, d;
    if (p == e) { /* a missing component, as in "" or "1." */
      return NULL;
    }
    ch = (unsigned char)*p++;
    x = ch - '0';
    if (((ch | 0x20u) != 'x') && (ch != '*')) {
      if ((k != i) || (x >= 10u)) {
        return NULL;
      }
      if (x) {
        while ((p != e) && ((d = (uint32_t)(unsigned char)*p - '0') < 10u)) {
          if ((x >= 429496729u) && ((x > 429496729u) || (d > 5u))) {
            h |= NSV_HOW_OVF;
          }
          x = (x * 10u) + d;
          ++p;
        }
      }
      *(i ? &c->minor : &c->major) = x;
      k = i + 1u;
    }
    if ((++i == 3u) || (p == e) || (*p != '.')) {
      break;
    }
    ++p;
  }
  /* In their own grammars ==1.2 is 1.2.0, ~=1.2 and ~>1.2 are >=1.2 <2 and !=1.2 is not
     1.2.0, none of them the 1.2 line: so ==, ~= and ~> take only whole versions, and !=
     a partial one only with a wildcard. */
  if ((h & NSV_HOW_WHOLE) || ((h & NSV_HOW_NOT) && (k == i))) {
    return NULL;
  }

  if (acc == (NSV_BIT_LT | NSV_BIT_EQ)) { /* <=1.2 is not >1.2 */
    h ^= NSV_BIT_LT | NSV_BIT_EQ | NSV_BIT_GT | NSV_HOW_NOT;
  }
  if ((h & 7u) == NSV_BIT_GT) {
    /* >1.2 is >=1.3, the next line; past UINT32_MAX nothing is above it. A carry takes
       k to 0, which keeps the -0 below in either mode: >1.4294967295 is npm's
       >=1.4294967296.0, and the first version above that is 2.0.0-0, not 2.0.0. */
    if (k && (((k == 1u) || !(k = ++c->minor ? k : 0u)) ? ++c->major : 1u)) {
      h ^= NSV_BIT_EQ;
    } else {
      h &= ~7u;
    }
  } else if (acc == NSV_BIT_EQ) { /* = and != take the whole line */
    h |= NSV_BIT_GT;
  }
  /* =, !=, ^ and ~ share up to the numbers given; ^ only the first, if it is nonzero. */
  keep = (h >> 3) & 7u;
  if (keep == 4u) {
    keep = ((k == 2u) && c->major) ? 1u : 3u;
  }
  h = (h & ~NSV_HOW_KEEP(7)) | NSV_HOW_KEEP((keep < k) ? keep : k);
  c->src = "0";
  c->pre_ofs = 0;
  c->pre_len = !(k && ((h & (NSV_HOW_EXCL | NSV_HOW_NOT | NSV_BIT_LT | NSV_BIT_EQ)) ==
                       (NSV_HOW_EXCL | NSV_BIT_EQ)));
  *how = h;
  return p;
}

/* A comparator's operator at p, folded into *how. Returns its end, p if there is none, or
   NULL for a '!' alone, or for none where a version just ended (glued). */
static NSV_INLINE char const *nsv_range_op(char const *p, char const *e, unsigned glued,
                                           unsigned *how) {
  char const op = (p != e) ? *p : '\0';
  /* ^ and ~ also hold MAJOR.MINOR.PATCH to the bound's, ~ through MINOR and ^ through the
     leftmost nonzero: that is what their upper bounds come to (~1.2.3 is <1.3.0-0, ^0.2.3
     is <0.3.0-0), without computing one to overflow. */
  if ((op == '<') || (op == '>')) {
    *how ^= NSV_BIT_EQ | NSV_HOW_KEEP(3) | ((op == '<') ? NSV_BIT_LT : NSV_BIT_GT);
    if ((++p != e) && (*p == '=')) {
      ++p;
      *how |= NSV_BIT_EQ;
    }
  } else if ((op == '^') || (op == '~')) {
    *how ^= NSV_BIT_GT | NSV_HOW_KEEP(3 ^ ((op == '~') ? 2u : 4u));
    if ((++p != e) && (op == '~') && ((*p == '=') || (*p == '>'))) {
      ++p; /* PEP 440's ~= and RubyGems' ~>, which agree with ~ on whole versions */
      *how |= NSV_HOW_WHOLE;
    }
  } else if ((op == '=') || (op == '!')) {
    /* != and == are not npm's, but PEP 440's and Composer's */
    if ((++p != e) && (*p == '=')) {
      ++p;
      *how |= (op == '!') ? NSV_HOW_NOT : NSV_HOW_WHOLE;
    } else if (op == '!') {
      return NULL;
    }
  } else if (glued) { /* only an operator may follow a version with nothing between */
    return NULL;
  }
  return p;
}

/* A version ends at a space, a tab, a comma, a '|', the next comparator's operator or the
   end. Returns what follows, or NULL for a comma that is not between two comparators. */
static NSV_INLINE char const *nsv_range_sep(char const *p, char const *e) {
  p = nsv_skip_ws(p, e);
  if ((p != e) && (*p == ',')) {
    p = nsv_skip_ws(p + 1, e);
    if ((p == e) || (*p == '|')) {
      return NULL;
    }
  }
  return p;
}

static int nsv_range_to(char const *s, size_t len, nsv_semver_t const *v, unsigned flags) {
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
    unsigned state = fresh, glued = 0;
    p = nsv_skip_ws(p, e);
    do {
      nsv_semver_t c;
      char const *q;
      size_t n;
      unsigned how = NSV_BIT_EQ | NSV_HOW_KEEP(3) |
                     ((flags & NSV_RANGE_EXCLUDE_PRE) ? NSV_HOW_EXCL : 0u);
      unsigned keep, same;
      int rc;

      p = nsv_range_op(p, e, glued, &how);
      if (!p) {
        return NSV_RET_ERR_SYNTAX;
      }
      /* A version, npm's v1.2.3 among them, or failing that a partial one. */
      p = nsv_skip_ws(p, e);
      if ((p != e) && (*p == 'v')) {
        ++p;
      }
      rc = nsv_parse_to(&c, p, (size_t)(e - p), &n);
      if (rc == NSV_RET_ERR_SYNTAX) {
        unsigned h = how; /* not how's own address, which would keep it out of a register */
        q = nsv_parse_partial(&c, p, e, &h);
        if (!q) {
          return rc;
        }
        how = h;
        n = (size_t)(q - p);
        rc = (how & NSV_HOW_OVF) ? NSV_RET_ERR_OVERFLOW : NSV_RET_OK;
      }
      p += n;
      q = nsv_range_sep(p, e);
      if (!q) {
        return NSV_RET_ERR_SYNTAX;
      }
      glued = (q == p);
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
      keep = (how >> 3) & 7u;
      if (keep > 3u) {
        keep = c.major ? 1u : (c.minor ? 2u : 3u);
      }
      /* Whether v passes, against whether the test is negated (NSV_HOW_NOT). */
      if (((((how >> nsv_cmp(v, &c)) & 1u) && (same >= keep)) ? 1u : 0u) ==
          ((how / NSV_HOW_NOT) & 1u)) {
        state |= 1u;
      }
      if (c.pre_len && (same == 3u) && !(how & (NSV_HOW_NOT | NSV_HOW_PARTIAL))) {
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

int nsv_range_match(char const *s, size_t len, nsv_semver_t const *v, unsigned flags) {
  int const rc = nsv_range_to(s, len, v, flags);
  return NSV_DIAG_PRINT_BAD(range, rc, s, len);
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

#if NSV_DIAG
static nsv_diag_print_t nsv_diag_print_fn;
static void *nsv_diag_print_ctx;

void nsv_diag_set_print(nsv_diag_print_t print, void *ctx) {
  nsv_diag_print_fn = print;
  nsv_diag_print_ctx = ctx;
}

/* What is wrong and where: why, then quote_len bytes of quote in quotes. */
typedef struct {
  char const *at, *why, *quote;
  size_t quote_len;
} nsv_diag_bad_t;

/* MAJOR.MINOR.PATCH at p: the first fault in them, or NULL if there is none. */
static char const *nsv_diag_bad_core(char const *p, char const *e, char const **why) {
  unsigned i;
  for (i = 0;; ++i) {
    if ((p == e) || !nsv_is_digit(*p)) {
      *why = "expected a number";
      return p;
    }
    if ((*p == '0') && ((p + 1) != e) && nsv_is_digit(p[1])) {
      *why = "leading zero";
      return p;
    }
    while ((p != e) && nsv_is_digit(*p)) {
      ++p;
    }
    if (i == 2u) {
      return NULL;
    }
    if ((p == e) || (*p != '.')) {
      *why = "expected '.'";
      return p;
    }
    ++p;
  }
}

/* Why the version [t, p), partial or not, cannot go on into the byte at p. */
static void nsv_diag_bad_after(char const *t, char const *p, unsigned partial,
                               nsv_diag_bad_t *b) {
  unsigned const c = (unsigned char)*p;
  unsigned ids = 0, build = 0; /* whether it has a pre-release or build, and a build */
  for (; t != p; ++t) {
    ids |= (*t == '-') || (*t == '+');
    build |= (*t == '+');
  }
  b->at = p;
  if (nsv_is_digit(*p)) { /* where a number stopped short */
    b->at = p - (p[-1] == '0');
    b->why = (p[-1] == '0') ? "leading zero" : "number after a wildcard";
  } else if (c == '.') {
    b->at = p + ids;
    b->why = ids ? "expected an identifier" : "more than three components";
  } else if (((c == '-') || (c == '+')) && partial) {
    b->why = "pre-release or build on a partial version";
  } else if ((c == '-') || ((c == '+') && !build)) {
    b->at = p + 1;
    b->why = (c == '-') ? "expected a pre-release" : "expected build metadata";
  } else {
    b->why = "unexpected ";
    b->quote = p;
    b->quote_len = 1;
  }
}

/* Why the version at t, after the operator at o, is neither whole nor partial. */
static void nsv_diag_bad_partial(char const *t, char const *e, char const *o, unsigned how,
                                 nsv_diag_bad_t *b) {
  char const *p = t;
  unsigned i, wild = 0;
  for (i = 0;; ++i) {
    unsigned const digit = (p != e) && nsv_is_digit(*p);
    if ((p != e) && (((*p | 0x20) == 'x') || (*p == '*'))) {
      wild = 1;
      ++p;
    } else if (digit && !wild) {
      if (*p++ != '0') {
        while ((p != e) && nsv_is_digit(*p)) {
          ++p;
        }
      }
    } else {
      b->at = p;
      b->why = digit ? "number after a wildcard"
               : i   ? "expected a number or a wildcard"
                     : "expected a version";
      return;
    }
    if ((i == 2u) || (p == e) || (*p != '.')) {
      break;
    }
    ++p;
  }
  if ((p != e) && nsv_is_digit(*p)) { /* a number cut short by a leading zero */
    nsv_diag_bad_after(t, p, 1, b);
    return;
  }
  /* Well formed, so the operator takes no partial version: ==, ~=, ~> or !=. */
  b->at = t;
  b->why = (how & NSV_HOW_NOT) ? "expected a whole version or a wildcard after "
                               : "expected a whole version after ";
  b->quote = o;
  b->quote_len = 2;
}

/* The version at t, OVERFLOW or TOO_LONG as rc says: the number too big, or the whole. */
static void nsv_diag_bad_limit(int rc, char const *t, nsv_diag_bad_t *b) {
  b->at = t;
  b->why = "version longer than 65535 bytes";
  if (rc == NSV_RET_ERR_OVERFLOW) {
    uint32_t x = 0;
    b->why = "number over 4294967295";
    for (;; ++t) { /* to the first number that does not fit, which there is */
      uint32_t const d = (uint32_t)(unsigned char)*t - '0';
      if (!nsv_is_digit(*t)) {
        b->at = t + 1;
        x = 0;
      } else if ((x > 429496729u) || ((x == 429496729u) && (d > 5u))) {
        return;
      } else {
        x = (x * 10u) + d;
      }
    }
  }
}

/* A message, passed to nsv_diag_print_fn a buffer at a time. */
typedef struct {
  char buf[64];
  size_t n;
} nsv_diag_msg_t;

static void nsv_diag_msg_c(nsv_diag_msg_t *m, char c) {
  if (m->n == sizeof(m->buf)) {
    nsv_diag_print_fn(nsv_diag_print_ctx, m->buf, m->n);
    m->n = 0;
  }
  m->buf[m->n++] = c;
}

static void nsv_diag_msg_s(nsv_diag_msg_t *m, char const *s) {
  while (*s) {
    nsv_diag_msg_c(m, *s++);
  }
}

/* A byte of the input: printable ASCII as itself, a tab too unless quoted, else \xHH. BLANK
   is a space instead, under bytes before the fault, which are all printable or tabs. */
enum { NSV_DIAG_ECHO, NSV_DIAG_QUOTE, NSV_DIAG_BLANK };

static void nsv_diag_msg_byte(nsv_diag_msg_t *m, char c, unsigned mode) {
  unsigned const u = (unsigned char)c;
  if ((u == '\t') && (mode != NSV_DIAG_QUOTE)) {
    nsv_diag_msg_c(m, '\t');
  } else if ((u - 0x20u) < 0x5Fu) {
    nsv_diag_msg_c(m, (mode == NSV_DIAG_BLANK) ? ' ' : c);
  } else {
    nsv_diag_msg_s(m, "\\x");
    nsv_diag_msg_c(m, "0123456789abcdef"[u >> 4]);
    nsv_diag_msg_c(m, "0123456789abcdef"[u & 15u]);
  }
}

#define NSV_DIAG_SPAN 32 /* bytes of the input shown either side of the fault */

/* Prints "bad <what> at column N: <why>", then the input around the fault, with a caret
   under it. */
static void nsv_diag_say(char const *what, char const *s, char const *e,
                         nsv_diag_bad_t const *b) {
  nsv_diag_msg_t m;
  char col[24]; /* the column in decimal, least significant digit first */
  size_t nd = 1, i;
  char const *const lo = ((b->at - s) > NSV_DIAG_SPAN) ? (b->at - NSV_DIAG_SPAN) : s;
  char const *const hi = ((e - b->at) > NSV_DIAG_SPAN) ? (b->at + NSV_DIAG_SPAN) : e;
  char const *p;

  col[0] = '1';
  for (p = s; p != b->at; ++p) { /* counted up, which needs no division */
    for (i = 0; (i < nd) && (col[i] == '9'); ++i) {
      col[i] = '0';
    }
    if (i == nd) {
      col[nd++] = '1';
    } else {
      ++col[i];
    }
  }
  m.n = 0;
  nsv_diag_msg_s(&m, "bad ");
  nsv_diag_msg_s(&m, what);
  nsv_diag_msg_s(&m, " at column ");
  while (nd) {
    nsv_diag_msg_c(&m, col[--nd]);
  }
  nsv_diag_msg_s(&m, ": ");
  nsv_diag_msg_s(&m, b->why);
  if (b->quote) {
    nsv_diag_msg_c(&m, '\'');
    for (p = b->quote; p != (b->quote + b->quote_len); ++p) {
      nsv_diag_msg_byte(&m, *p, NSV_DIAG_QUOTE);
    }
    nsv_diag_msg_c(&m, '\'');
  }
  nsv_diag_msg_s(&m, (lo != s) ? "\n  ..." : "\n  ");
  for (p = lo; p != hi; ++p) {
    nsv_diag_msg_byte(&m, *p, NSV_DIAG_ECHO);
  }
  nsv_diag_msg_s(&m, (hi != e) ? "...\n  " : "\n  ");
  nsv_diag_msg_s(&m, (lo != s) ? "   " : "");
  for (p = lo; p != b->at; ++p) {
    nsv_diag_msg_byte(&m, *p, NSV_DIAG_BLANK);
  }
  nsv_diag_msg_s(&m, "^\n");
  nsv_diag_print_fn(nsv_diag_print_ctx, m.buf, m.n);
}

/* nsv_parse's or nsv_parse_prefix's rc on s: SYNTAX, OVERFLOW or TOO_LONG, explained. */
static NSV_NOINLINE int nsv_diag_print_bad_version(int rc, char const *s, size_t len) {
  char const *const e = s + len;
  nsv_diag_bad_t b;
  nsv_semver_t c;
  size_t n;

  if (!nsv_diag_print_fn) {
    return rc;
  }
  b.quote = NULL;
  if (rc != NSV_RET_ERR_SYNTAX) {
    nsv_diag_bad_limit(rc, s, &b);
  } else {
    b.at = nsv_diag_bad_core(s, e, &b.why);
    if (!b.at) { /* the numbers are fine, so the version is cut short */
      nsv_parse_to(&c, s, len, &n);
      nsv_diag_bad_after(s, s + n, 0, &b);
    }
  }
  nsv_diag_say("version", s, e, &b);
  return rc;
}

/* nsv_range_match's rc on s, explained the same way: the grammar walked as nsv_range_to
   walks it, to the first syntax error, or else the first version beyond the limits. */
static NSV_NOINLINE int nsv_diag_print_bad_range(int rc, char const *s, size_t len) {
  char const *p = s, *const e = s + len, *t = s, *lim = NULL;
  nsv_diag_bad_t b;

  if (!nsv_diag_print_fn) {
    return rc;
  }
  b.quote = NULL;
  for (;;) {
    char const *const set = nsv_skip_ws(p, e);
    unsigned glued = 0, partial = 0;
    p = set;
    do {
      char const *const o = p, *q;
      unsigned how = 0; /* only the bits that make a partial version an error matter here */
      nsv_semver_t c;
      size_t n;
      int r;

      q = nsv_range_op(p, e, glued, &how);
      if (!q) {
        if (*p == '!') {
          b.at = p + 1;
          b.why = "expected '=' after '!'";
        } else {
          nsv_diag_bad_after(t, p, partial, &b);
        }
        goto say;
      }
      p = nsv_skip_ws(q, e);
      if ((p != e) && (*p == 'v')) {
        ++p;
      }
      t = p;
      r = nsv_parse_to(&c, p, (size_t)(e - p), &n);
      partial = (r == NSV_RET_ERR_SYNTAX);
      if (partial) {
        q = nsv_parse_partial(&c, p, e, &how);
        if (!q) {
          if ((o != set) && (o == t) && (*t == '-') && (t[-1] != ',')) { /* after a space */
            b.at = t;
            b.why = "hyphen ranges are not supported; use >= and <=";
          } else if (nsv_skip_ws(s, e) == e) {
            b.at = s;
            b.why = "empty range; use * for any version";
          } else {
            nsv_diag_bad_partial(t, e, o, how, &b);
          }
          goto say;
        }
        n = (size_t)(q - p);
        r = (how & NSV_HOW_OVF) ? NSV_RET_ERR_OVERFLOW : NSV_RET_OK;
      }
      if (r && !lim) {
        lim = t;
      }
      p += n;
      q = nsv_range_sep(p, e);
      if (!q) {
        b.at = nsv_skip_ws(nsv_skip_ws(p, e) + 1, e);
        b.why = "expected a comparator after ','";
        goto say;
      }
      glued = (q == p);
      p = q;
    } while ((p != e) && (*p != '|'));
    if (p == e) {
      break;
    }
    if ((++p == e) || (*p != '|')) {
      b.at = p - 1;
      b.why = "expected '||'";
      goto say;
    }
    ++p;
  }
  nsv_diag_bad_limit(rc, lim, &b);
say:
  nsv_diag_say("range", s, e, &b);
  return rc;
}
#endif
