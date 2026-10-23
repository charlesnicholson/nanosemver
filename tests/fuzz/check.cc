#include "check.h"

#include "../guarded.h"
#include "../int_max.h"
#include "../ref_semver.h"
#include "nanosemver.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace nsv_fuzz {
namespace {

#define NSV_STR2(x) #x
#define NSV_STR(x) NSV_STR2(x)
// A complete if/else, so it nests safely without do/while(0), which MSVC flags.
#define FUZZ_REQUIRE(c) \
  if (c) {              \
  } else                \
    return "check.cc:" NSV_STR(__LINE__) ": " #c

struct free_deleter {
  void operator()(void *p) const { std::free(p); }
};
using blob = std::unique_ptr<nsv_semver_t, free_deleter>;

blob alloc_blob(size_t n) {
  blob b(static_cast<nsv_semver_t *>(std::malloc(n)));
  if (!b) {
    std::abort();
  }
  return b;
}

int expected_rc(ref::status s) {
  switch (s) {
    case ref::status::ok: return NSV_RET_OK;
    case ref::status::syntax: return NSV_RET_ERR_SYNTAX;
    case ref::status::overflow: return NSV_RET_ERR_OVERFLOW;
    case ref::status::too_long: return NSV_RET_ERR_TOO_LONG;
  }
  return 1;  // unreachable
}

bool span_eq(char const *p, int n, std::string const &want) {
  return (n == static_cast<int>(want.size())) && p &&
         (std::string_view(p, static_cast<size_t>(n)) == want);
}

// One accessor: the span in place, or TOO_LONG if its length cannot be an int.
char const *check_get(int (*get)(nsv_semver_t const *, char const **), nsv_semver_t const &v,
                      std::string const &want, char const *in_place) {
  char const *s = nullptr;
  int const len = get(&v, &s);
  if (!nsv_test::fits_int(want.size())) {
    FUZZ_REQUIRE(len == NSV_RET_ERR_TOO_LONG);
    return nullptr;
  }
  FUZZ_REQUIRE(span_eq(s, len, want));
  FUZZ_REQUIRE(len ? (s == in_place) : (*s == '\0'));
  return nullptr;
}

// Format, accessors, copy and relocation of a version parsed from p[0..n).
char const *check_parsed(nsv_semver_t const &v, ref::version const &rv, char const *p,
                         size_t n) {
  if (char const *f = check_get(nsv_get_pre, v, rv.pre, p + v.pre_ofs)) {
    return f;
  }
  if (char const *f = check_get(nsv_get_build, v, rv.build, p + v.build_ofs)) {
    return f;
  }

  // Parse only accepts canonical spellings, so formatting gives back the input.
  std::string buf(n + 1, '\x7f');
  if (!nsv_test::fits_int(n)) {
    FUZZ_REQUIRE(nsv_format(nullptr, 0, &v) == NSV_RET_ERR_TOO_LONG);
    FUZZ_REQUIRE(nsv_format(&buf[0], n + 1, &v) == NSV_RET_ERR_TOO_LONG);
  } else {
    int const ni = static_cast<int>(n);
    FUZZ_REQUIRE(nsv_format(nullptr, 0, &v) == ni);
    FUZZ_REQUIRE(nsv_format(&buf[0], n + 1, &v) == ni);
    FUZZ_REQUIRE(buf[n] == '\0');
    FUZZ_REQUIRE(!std::memcmp(buf.data(), p, n));
    FUZZ_REQUIRE(nsv_format(&buf[0], n, &v) == NSV_RET_ERR_BUF_TOO_SMALL);
    FUZZ_REQUIRE(buf[0] == '\0');
  }

  size_t const un = sizeof(nsv_semver_t) + rv.pre.size() + rv.build.size() + 2;
  if (!nsv_test::fits_int(un)) {
    FUZZ_REQUIRE(nsv_copy(nullptr, 0, &v) == NSV_RET_ERR_TOO_LONG);
    return nullptr;
  }
  int const need = static_cast<int>(un);
  FUZZ_REQUIRE(nsv_copy(nullptr, 0, &v) == need);
  blob b1 = alloc_blob(un);
  FUZZ_REQUIRE(nsv_copy(b1.get(), un - 1, &v) == NSV_RET_ERR_BUF_TOO_SMALL);
  FUZZ_REQUIRE(nsv_copy(b1.get(), un, &v) == need);
  FUZZ_REQUIRE(b1->src == nullptr);
  FUZZ_REQUIRE(nsv_cmp(b1.get(), &v) == NSV_CMP_EQ);
  char const *const tail = reinterpret_cast<char const *>(b1.get()) + sizeof(nsv_semver_t);
  FUZZ_REQUIRE(!std::memcmp(tail, rv.pre.c_str(), rv.pre.size() + 1));
  FUZZ_REQUIRE(!std::memcmp(tail + rv.pre.size() + 1, rv.build.c_str(), rv.build.size() + 1));
  if (char const *f = check_get(nsv_get_pre, *b1, rv.pre, tail)) {
    return f;
  }
  if (char const *f = check_get(nsv_get_build, *b1, rv.build, tail + rv.pre.size() + 1)) {
    return f;
  }

  // A copy is relocatable: move the bytes, destroy the original.
  blob b2 = alloc_blob(un);
  std::memcpy(b2.get(), b1.get(), un);
  std::memset(static_cast<void *>(b1.get()), 0xEE, un);
  b1.reset();
  FUZZ_REQUIRE(nsv_cmp(b2.get(), &v) == NSV_CMP_EQ);
  if (nsv_test::fits_int(n)) {
    std::fill(buf.begin(), buf.end(), '\x7f');
    FUZZ_REQUIRE(nsv_format(&buf[0], n + 1, b2.get()) == static_cast<int>(n));
    FUZZ_REQUIRE(!std::memcmp(buf.data(), p, n));
  }
  return nullptr;
}

char const *check_one(char const *p, size_t n, nsv_semver_t *v, ref::version *rv,
                      bool *parsed) {
  nsv_semver_t out;
  unsigned char before[sizeof(out)];
  std::memset(&out, 0xA5, sizeof(out));
  std::memcpy(before, &out, sizeof(out));

  int const rc = nsv_parse(&out, p, n);
  FUZZ_REQUIRE(rc == expected_rc(ref::parse(std::string_view(p, n), rv)));
  *parsed = (rc == NSV_RET_OK);
  if (!*parsed) {
    FUZZ_REQUIRE(!std::memcmp(&out, before, sizeof(out)));
    return nullptr;
  }
  FUZZ_REQUIRE(out.src == p);
  FUZZ_REQUIRE(out.major == rv->major);
  FUZZ_REQUIRE(out.minor == rv->minor);
  FUZZ_REQUIRE(out.patch == rv->patch);
  *v = out;
  return check_parsed(out, *rv, p, n);
}

}  // namespace

char const *check(std::uint8_t const *data, std::size_t size) {
  static nsv_test::guarded ga, gb;
  std::string_view const in(reinterpret_cast<char const *>(data), size);
  size_t const nl = in.find('\n');
  std::string_view const a = in.substr(0, nl);
  std::string_view const b = (nl == std::string_view::npos) ? a : in.substr(nl + 1);

  // Each string sits against its own guard page, so an over-read faults.
  char const *const pa = ga.place(a);
  char const *const pb = gb.place(b);
  nsv_semver_t va, vb;
  ref::version ra, rb;
  bool oka = false, okb = false;
  if (char const *f = check_one(pa, a.size(), &va, &ra, &oka)) {
    return f;
  }
  if (char const *f = check_one(pb, b.size(), &vb, &rb, &okb)) {
    return f;
  }
  if (oka && okb) {
    int const c = nsv_cmp(&va, &vb);
    FUZZ_REQUIRE(c == ref::cmp(ra, rb) + 1);
    FUZZ_REQUIRE(nsv_cmp(&vb, &va) == 2 - c);
    FUZZ_REQUIRE(nsv_cmp(&va, &va) == NSV_CMP_EQ);
  }
  return nullptr;
}

}  // namespace nsv_fuzz
