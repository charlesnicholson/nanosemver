// nsv::SemVer, from nanosemver.hpp, against the C API it wraps.
#include "nanosemver.hpp"  // first, so it is shown to need nothing before it

#include "corpus.h"
#include "fuzz/gen.h"
#include "nsv_test.h"
#include "range_cases.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using nsv::SemVer;
using nsv_test::parse_ok;

namespace {

using V = SemVer<>;
// Room for every version the tests parse but the very long: the corpus, the range cases
// and the generated versions need at most 114 bytes.
using Wide = SemVer<256>;
// Room for any copy nsv_copy makes: a 65534-byte pre-release and two NULs. 64 KiB, so
// only ever on the heap.
using Huge = SemVer<65536>;

// What the class promises about itself.
static_assert(std::is_same<V, SemVer<48>>::value, "48 bytes by default");
static_assert(sizeof(V) == sizeof(nsv_semver_t) + 48, "a header and its array, no more");
static_assert(sizeof(Wide) == sizeof(nsv_semver_t) + 256, "");
static_assert(sizeof(SemVer<2>) == sizeof(SemVer<alignof(nsv_semver_t)>), "tail padding");
static_assert(alignof(SemVer<13>) == alignof(nsv_semver_t), "");
static_assert(std::is_trivially_copyable<V>::value, "plain bytes");
static_assert(std::is_trivially_destructible<V>::value, "nothing to free");
static_assert(std::is_standard_layout<V>::value, "");
static_assert(std::is_nothrow_default_constructible<V>::value, "");
static_assert(!std::is_constructible<V, nsv_semver_t const &>::value, "only from()");
static_assert(!std::is_convertible<nsv_semver_t const &, V>::value, "only from()");
static_assert(!std::is_constructible<SemVer<8>, V const &>::value, "only assign()");
static_assert(!std::is_assignable<SemVer<8> &, V const &>::value, "only assign()");
static_assert(noexcept(std::declval<V &>().parse("", 0)), "nothing allocates");
static_assert(noexcept(std::declval<V &>().parse("")), "");
static_assert(noexcept(std::declval<V &>().parse_prefix("", 0, std::declval<size_t &>())), "");
static_assert(noexcept(std::declval<V &>().assign(nsv_semver_t{})), "");
static_assert(noexcept(V::from(nsv_semver_t{}, std::declval<int &>())), "");
static_assert(noexcept(std::declval<V const &>().matches("", 0)), "");
static_assert(noexcept(std::declval<V const &>().format(nullptr, 0)), "");
static_assert(noexcept(std::declval<V const &>() < std::declval<SemVer<8> const &>()), "");

// Constant-initialized: a global version costs nothing at startup.
constinit V g_version;
constinit SemVer<2> g_small;

// The bytes of a copy's strings: its pre-release and build, and a NUL after each. A
// SemVer<N> holds c when this is at most N.
size_t need(nsv_semver_t const &c) { return c.pre_len + 1u + c.build_len + 1u; }

// v as a string, through SemVer::format.
template <size_t N>
std::string str(SemVer<N> const &v) {
  int const n = v.format(nullptr, 0);
  REQUIRE(n >= 0);
  std::string s(static_cast<size_t>(n) + 1, '\x7f');
  REQUIRE(v.format(&s[0], s.size()) == n);
  REQUIRE(s[static_cast<size_t>(n)] == '\0');
  s.resize(static_cast<size_t>(n));
  return s;
}

// s, parsed into a version, or a failed test.
template <size_t N = 48>
SemVer<N> ver(std::string const &s) {
  SemVer<N> v;
  INFO("parsing \"", s, "\" into SemVer<", N, ">");
  REQUIRE(v.parse(s.data(), s.size()) == NSV_RET_OK);
  return v;
}

// Whether n bytes from p lie within v.
template <size_t N>
bool within(SemVer<N> const &v, void const *p, size_t n) {
  char const *const lo = reinterpret_cast<char const *>(&v);
  char const *const q = static_cast<char const *>(p);
  std::less_equal<char const *> const le{};
  return le(lo, q) && le(q + n, lo + sizeof v);
}

// All of v that means anything: its header's fields and its whole array. Not its raw
// bytes: the header's padding is nobody's to compare, and MSan would object.
template <size_t N>
std::string state(SemVer<N> const &v) {
  nsv_semver_t const &h = v.semver();
  REQUIRE(h.src == nullptr);
  REQUIRE(h.pre_ofs == 0);  // so pre() is the start of the array
  std::string s(v.pre(), N);
  for (unsigned long x : { h.major, h.minor, h.patch }) {
    s += "," + std::to_string(x);
  }
  for (unsigned x : { h.pre_ofs, h.pre_len, h.build_ofs, h.build_len }) {
    s += "," + std::to_string(x);
  }
  return s;
}

// SemVer<N>::from(c), which has to succeed.
template <size_t N = 48>
SemVer<N> from_ok(nsv_semver_t const &c) {
  int rc = 1;
  SemVer<N> v = SemVer<N>::from(c, rc);
  REQUIRE(rc == NSV_RET_OK);
  return v;
}

// SemVer<N>::from(c), which fails with want: 0.0.0.
template <size_t N>
void from_refused(nsv_semver_t const &c, int want) {
  int rc = 1;
  SemVer<N> const v = SemVer<N>::from(c, rc);
  REQUIRE(rc == want);
  REQUIRE(state(v) == state(SemVer<N>()));
}

// Places s and its NUL so the NUL is the last byte before a guard page.
char const *place_nul(nsv_test::guarded &g, std::string const &s) {
  return g.place(std::string_view(s.c_str(), s.size() + 1));
}

// Every part of v agrees with c, a view, literal or copy.
template <size_t N>
void same(SemVer<N> const &v, nsv_semver_t const &c) {
  REQUIRE(need(c) <= N);
  REQUIRE(v.major() == c.major);
  REQUIRE(v.minor() == c.minor);
  REQUIRE(v.patch() == c.patch);
  // NUL-terminated exactly where the C API's length ends.
  REQUIRE(std::string(v.pre()) == nsv_test::pre(c));
  REQUIRE(std::string(v.build()) == nsv_test::build(c));
  REQUIRE(str(v) == nsv_test::format(c));
  REQUIRE(nsv_cmp(&v.semver(), &c) == NSV_CMP_EQ);
  REQUIRE(nsv_cmp(&c, &v.semver()) == NSV_CMP_EQ);
  // A copy's header, its strings after it, all in v.
  REQUIRE(v.semver().src == nullptr);
  REQUIRE(static_cast<void const *>(&v.semver()) == static_cast<void const *>(&v));
  REQUIRE(v.pre() == reinterpret_cast<char const *>(&v) + sizeof(nsv_semver_t));
  REQUIRE(within(v, v.pre(), std::strlen(v.pre()) + 1));
  REQUIRE(within(v, v.build(), std::strlen(v.build()) + 1));
  // Where the C API finds them, but for "", which it returns as a literal.
  char const *p = nullptr;
  int const pn = nsv_get_pre(&v.semver(), &p);
  REQUIRE(pn == static_cast<int>(std::strlen(v.pre())));
  REQUIRE(((pn == 0) || (p == v.pre())));
  int const bn = nsv_get_build(&v.semver(), &p);
  REQUIRE(bn == static_cast<int>(std::strlen(v.build())));
  REQUIRE(((bn == 0) || (p == v.build())));
}

// f fails with want, on a version holding strings and on 0.0.0, and changes neither.
template <size_t N, class F>
void fails_unchanged(int want, F const &f) {
  SemVer<N> v = ver<N>((N >= 8) ? "9.8.7-keep+me" : "9.8.7");
  std::string const before = state(v);
  REQUIRE(f(v) == want);
  REQUIRE(state(v) == before);
  SemVer<N> d;
  REQUIRE(f(d) == want);
  REQUIRE(state(d) == state(SemVer<N>()));
}

// What a SemVer<N> makes of what the C API parses as c with rc: rc's error, or the
// error for c not fitting, or OK.
template <size_t N>
int expect(int rc, nsv_semver_t const &c) {
  return (rc != NSV_RET_OK) ? rc : (need(c) <= N) ? NSV_RET_OK : NSV_RET_ERR_BUF_TOO_SMALL;
}

// "1.2.3", with a pre-release of p bytes and build metadata of b, where nonzero.
std::string shaped(char const *numbers, size_t p, size_t b) {
  std::string s = numbers;
  if (p) {
    s += "-" + std::string(p, 'p');
  }
  if (b) {
    s += "+" + std::string(b, 'b');
  }
  return s;
}

// semver.org's precedence example, ascending.
char const *const k_semver_org[] = {
  "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0",
};

template <size_t N>
void append(std::vector<std::string> &out, char const *const (&list)[N]) {
  out.insert(out.end(), std::begin(list), std::end(list));
}

template <size_t N>
void default_is_zero() {
  INFO("SemVer<", N, ">");
  SemVer<N> const v;
  same(v, nsv_semver_t{});
  REQUIRE(str(v) == "0.0.0");
  REQUIRE(v.pre() != nullptr);
  REQUIRE(*v.pre() == '\0');
  REQUIRE(v.build() != nullptr);
  REQUIRE(*v.build() == '\0');
  REQUIRE(state(v) == std::string(N, '\0') + ",0,0,0,0,0,0,0");
  SemVer<N> const w;
  REQUIRE(&v.semver() != &w.semver());  // each its own
  REQUIRE(v == w);
  REQUIRE(v == ver("0.0.0"));
  REQUIRE(v == ver("0.0.0+b"));
  REQUIRE(v > ver("0.0.0-0"));
  REQUIRE(v < ver("0.0.1"));
}

template <size_t N>
void parse_corpus() {
  INFO("SemVer<", N, ">");
  static nsv_test::guarded g;
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    nsv_semver_t const view = parse_ok(s);
    int const want = expect<N>(NSV_RET_OK, view);
    if (want == NSV_RET_OK) {
      SemVer<N> v;
      REQUIRE(v.parse(g.place(s), s.size()) == NSV_RET_OK);
      same(v, view);
      REQUIRE(str(v) == s);
      SemVer<N> w;
      REQUIRE(w.parse(place_nul(g, s)) == NSV_RET_OK);
      same(w, view);
      // Over a version that already has strings.
      REQUIRE(w.parse(s.data(), s.size()) == NSV_RET_OK);
      same(w, view);
    } else {
      fails_unchanged<N>(want, [&](auto &v) { return v.parse(g.place(s), s.size()); });
      fails_unchanged<N>(want, [&](auto &v) { return v.parse(place_nul(g, s)); });
    }
  }
  std::vector<std::string> bad;
  append(bad, corpus::k_invalid);
  append(bad, corpus::k_overflow);
  for (std::string const &s : bad) {
    INFO("\"", s, "\"");
    int const want = nsv_test::parse_rc(s);
    REQUIRE(want < 0);
    fails_unchanged<N>(want, [&](auto &v) { return v.parse(g.place(s), s.size()); });
    fails_unchanged<N>(want, [&](auto &v) { return v.parse(place_nul(g, s)); });
  }
}

// Every shape of version whose strings need N - 1, N or N + 1 bytes, and every one in
// between that a short N reaches: whether it fits, through every way in.
template <size_t N>
void capacity() {
  INFO("SemVer<", N, ">");
  for (size_t p = 0; p <= N; ++p) {
    for (size_t b = 0; b <= N; ++b) {
      size_t const strings = p + b + 2;
      if ((N > 16) && ((strings + 1 < N) || (strings > N + 1))) {
        continue;
      }
      std::string const s = shaped("1.2.3", p, b);
      INFO(s);
      nsv_semver_t const view = parse_ok(s);
      REQUIRE(need(view) == strings);
      int const want = (strings <= N) ? NSV_RET_OK : NSV_RET_ERR_BUF_TOO_SMALL;
      REQUIRE(expect<N>(NSV_RET_OK, view) == want);
      std::string const tail = s + " rest";
      size_t n = 0;
      if (want == NSV_RET_OK) {
        SemVer<N> v;
        REQUIRE(v.parse(s.data(), s.size()) == NSV_RET_OK);
        same(v, view);
        REQUIRE(v.parse(s.c_str()) == NSV_RET_OK);
        same(v, view);
        REQUIRE(v.parse_prefix(tail.data(), tail.size(), n) == NSV_RET_OK);
        REQUIRE(n == s.size());
        same(v, view);
        REQUIRE(v.assign(view) == NSV_RET_OK);
        same(v, view);
        same(from_ok<N>(view), view);
      } else {
        fails_unchanged<N>(want, [&](auto &v) { return v.parse(s.data(), s.size()); });
        fails_unchanged<N>(want, [&](auto &v) { return v.parse(s.c_str()); });
        fails_unchanged<N>(want, [&](auto &v) {
          n = 0;
          int const rc = v.parse_prefix(tail.data(), tail.size(), n);
          REQUIRE(n == s.size());  // spanning the version that does not fit
          return rc;
        });
        fails_unchanged<N>(want, [&](auto &v) { return v.assign(view); });
        from_refused<N>(view, want);
      }
    }
  }
}

// Every version of up to N + 4 characters fits, whatever its shape, and N + 5 need not:
// the shortest that does not is a bare pre-release or build of N - 1 bytes.
template <size_t N>
void fits_n_plus_4() {
  INFO("SemVer<", N, ">");
  for (char const *numbers : { "1.2.3", "10.20.30", "4294967295.4294967295.4294967295" }) {
    size_t const k = std::strlen(numbers);
    for (size_t len = k; len <= N + 4; ++len) {
      size_t const r = len - k;  // for '-', '+' and the strings
      std::vector<std::string> shapes;
      if (!r) {
        shapes.push_back(numbers);
      }
      if (r >= 2) {
        shapes.push_back(shaped(numbers, r - 1, 0));
        shapes.push_back(shaped(numbers, 0, r - 1));
      }
      for (size_t p = 1; p + 3 <= r; ++p) {
        shapes.push_back(shaped(numbers, p, r - 2 - p));
      }
      for (std::string const &s : shapes) {
        INFO(s);
        REQUIRE(s.size() == len);
        SemVer<N> v;
        REQUIRE(v.parse(s.data(), s.size()) == NSV_RET_OK);
        REQUIRE(str(v) == s);
      }
    }
  }
  for (std::string const &s : { shaped("1.2.3", N - 1, 0), shaped("1.2.3", 0, N - 1) }) {
    INFO(s);
    REQUIRE(s.size() == N + 5);
    fails_unchanged<N>(NSV_RET_ERR_BUF_TOO_SMALL,
                       [&](auto &v) { return v.parse(s.data(), s.size()); });
  }
}

template <size_t N>
void parse_prefix_corpus() {
  INFO("SemVer<", N, ">");
  static nsv_test::guarded g;
  std::vector<std::string> inputs;
  append(inputs, corpus::k_valid);
  append(inputs, corpus::k_invalid);
  append(inputs, corpus::k_overflow);
  for (char const *p : corpus::k_valid) {
    for (char const *tail : { " ", ", 2.0.0", " || 1", ".4", "x", "-", "+", "..", "-+" }) {
      inputs.push_back(std::string(p) + tail);
    }
  }
  for (std::string const &s : inputs) {
    INFO("\"", s, "\"");
    nsv_semver_t c{};
    size_t cn = 12345;
    int const rc = nsv_parse_prefix(&c, s.data(), s.size(), &cn);
    REQUIRE(rc != NSV_RET_ERR_ARG);
    int const want = expect<N>(rc, c);
    size_t n = 54321;
    if (want == NSV_RET_OK) {
      SemVer<N> v = ver<N>((N >= 8) ? "9.8.7-keep+me" : "9.8.7");
      REQUIRE(v.parse_prefix(g.place(s), s.size(), n) == NSV_RET_OK);
      REQUIRE(n == cn);
      same(v, c);
    } else {
      fails_unchanged<N>(want, [&](auto &v) {
        n = 54321;
        int const r = v.parse_prefix(g.place(s), s.size(), n);
        REQUIRE(n == cn);
        return r;
      });
    }
  }
}

}  // namespace

TEST_CASE("nsv::SemVer: a header and its array, in place") {
  V v;
  char const *const base = reinterpret_cast<char const *>(&v);
  REQUIRE(static_cast<void const *>(&v.semver()) == static_cast<void const *>(&v));
  REQUIRE(v.pre() == base + sizeof(nsv_semver_t));
  REQUIRE(v.parse("1.2.3-rc.1+b.7") == NSV_RET_OK);
  REQUIRE(v.pre() == base + sizeof(nsv_semver_t));
  REQUIRE(v.build() == base + sizeof(nsv_semver_t) + 5);
  REQUIRE(std::memcmp(base + sizeof(nsv_semver_t), "rc.1\0b.7", 9) == 0);
}

TEST_CASE("nsv::SemVer: default is 0.0.0") {
  default_is_zero<2>();
  default_is_zero<13>();
  default_is_zero<48>();
  default_is_zero<256>();
  REQUIRE(state(g_version) == state(V()));
  REQUIRE(state(g_small) == state(SemVer<2>()));
}

TEST_CASE("nsv::SemVer: parse, against nsv_parse") {
  parse_corpus<2>();
  parse_corpus<8>();
  parse_corpus<13>();
  parse_corpus<48>();
  parse_corpus<256>();
}

TEST_CASE("nsv::SemVer: capacity, at every size up to 16 and around 48 and 256") {
  capacity<2>();
  capacity<3>();
  capacity<4>();
  capacity<5>();
  capacity<7>();
  capacity<8>();
  capacity<9>();
  capacity<13>();  // tail padding, which nothing may use
  capacity<16>();
  capacity<48>();
  capacity<256>();
}

TEST_CASE("nsv::SemVer: every version of up to N + 4 characters fits") {
  fits_n_plus_4<2>();
  fits_n_plus_4<3>();
  fits_n_plus_4<8>();
  fits_n_plus_4<13>();
  fits_n_plus_4<48>();
}

TEST_CASE("nsv::SemVer: parse errors") {
  fails_unchanged<48>(NSV_RET_ERR_ARG, [](auto &v) { return v.parse(nullptr, 5); });
  fails_unchanged<48>(NSV_RET_ERR_ARG, [](auto &v) { return v.parse(nullptr, 0); });
  fails_unchanged<48>(NSV_RET_ERR_ARG, [](auto &v) { return v.parse(nullptr); });
  fails_unchanged<48>(NSV_RET_ERR_SYNTAX, [](auto &v) { return v.parse(""); });
  fails_unchanged<48>(NSV_RET_ERR_SYNTAX, [](auto &v) { return v.parse("v1.2.3"); });
  fails_unchanged<48>(NSV_RET_ERR_SYNTAX, [](auto &v) { return v.parse("1.2.3", 4); });
  fails_unchanged<48>(NSV_RET_ERR_OVERFLOW, [](auto &v) { return v.parse("4294967296.0.0"); });
  // Syntax first, then nsv_parse's limits, then whether it fits.
  std::string const big = "1.0.0-" + std::string(47, 'p');
  fails_unchanged<48>(NSV_RET_ERR_BUF_TOO_SMALL, [&](auto &v) { return v.parse(big.c_str()); });
  fails_unchanged<48>(NSV_RET_ERR_SYNTAX, [&](auto &v) { return v.parse((big + "!").c_str()); });
  fails_unchanged<48>(NSV_RET_ERR_OVERFLOW,
                      [&](auto &v) { return v.parse(("4294967296" + big.substr(1)).c_str()); });
  std::string const huge = "1.0.0-" + std::string(65530, 'p');  // 65536 bytes
  fails_unchanged<48>(NSV_RET_ERR_TOO_LONG, [&](auto &v) { return v.parse(huge.c_str()); });
  fails_unchanged<48>(NSV_RET_ERR_TOO_LONG,
                      [&](auto &v) { return v.parse(huge.data(), huge.size()); });
}

TEST_CASE("nsv::SemVer: parse reads only what it is given") {
  V v;
  REQUIRE(v.parse("1.2.3-rc.1junk", 10) == NSV_RET_OK);
  REQUIRE(str(v) == "1.2.3-rc.1");
  char const nul[] = "2.0.0\0-junk";
  REQUIRE(v.parse(nul) == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0");
  REQUIRE(v.parse(nul, sizeof(nul) - 1) == NSV_RET_ERR_SYNTAX);
  REQUIRE(str(v) == "2.0.0");
}

TEST_CASE("nsv::SemVer: parse_prefix, against nsv_parse_prefix") {
  parse_prefix_corpus<2>();
  parse_prefix_corpus<8>();
  parse_prefix_corpus<48>();
  parse_prefix_corpus<256>();
}

TEST_CASE("nsv::SemVer: parse_prefix") {
  struct {
    char const *s;
    int rc;
    size_t consumed;
    char const *version;
  } const cases[] = {
    { "1.2.3, 2.0.0", NSV_RET_OK, 5, "1.2.3" },
    { "1.2.3-rc.1.", NSV_RET_OK, 10, "1.2.3-rc.1" },
    { "1.2.3+b.7 rest", NSV_RET_OK, 9, "1.2.3+b.7" },
    { "1.2.3.4", NSV_RET_OK, 5, "1.2.3" },
    { "1.2.03", NSV_RET_OK, 5, "1.2.0" },
    { "x1.2.3", NSV_RET_ERR_SYNTAX, 0, nullptr },
    { "", NSV_RET_ERR_SYNTAX, 0, nullptr },
    { "4294967296.0.0 rest", NSV_RET_ERR_OVERFLOW, 14, nullptr },
  };
  for (auto const &c : cases) {
    INFO("\"", c.s, "\"");
    V v = ver("9.8.7-keep+me");
    size_t n = 12345;
    REQUIRE(v.parse_prefix(c.s, std::strlen(c.s), n) == c.rc);
    REQUIRE(n == c.consumed);
    REQUIRE(str(v) == (c.version ? c.version : "9.8.7-keep+me"));
  }

  size_t n = 7;
  fails_unchanged<48>(NSV_RET_ERR_ARG, [&](auto &v) { return v.parse_prefix(nullptr, 5, n); });
  REQUIRE(n == 7);

  std::string const big = "1.2.3-" + std::string(47, 'p') + " rest";
  fails_unchanged<48>(NSV_RET_ERR_BUF_TOO_SMALL, [&](auto &v) {
    n = 0;
    int const rc = v.parse_prefix(big.data(), big.size(), n);
    REQUIRE(n == big.size() - 5);  // spanning the version that does not fit
    return rc;
  });

  std::string const huge = "1.0.0-" + std::string(65530, 'p') + " rest";
  fails_unchanged<48>(NSV_RET_ERR_TOO_LONG,
                      [&](auto &v) { return v.parse_prefix(huge.data(), huge.size(), n); });
  REQUIRE(n == huge.size() - 5);
}

TEST_CASE("nsv::SemVer: parsing its own strings") {
  V v = ver("1.0.0-3.0.0+2.0.0");
  REQUIRE(v.parse(v.build()) == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0");

  v = ver("1.0.0-3.0.0+2.0.0");
  size_t n = 0;
  REQUIRE(v.parse_prefix(v.pre(), std::strlen(v.pre()), n) == NSV_RET_OK);
  REQUIRE(n == 5);
  REQUIRE(str(v) == "3.0.0");

  // Strings that nsv_copy, straight into v, would copy over themselves: the new
  // pre-release, 6 bytes into the old, to the start of the array.
  v = ver("1.0.0-2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbb+cccccccc");
  REQUIRE(v.parse(v.pre()) == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbb");
  v = ver("1.0.0+2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbb");
  REQUIRE(v.parse(v.build()) == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbb");
  v = ver("1.0.0-3.0.0-rc.1.longer.tail+x");
  REQUIRE(v.parse_prefix(v.pre(), std::strlen(v.pre()), n) == NSV_RET_OK);
  REQUIRE(n == 22);
  REQUIRE(str(v) == "3.0.0-rc.1.longer.tail");

  v = ver("1.0.0+2.0.0");
  REQUIRE(v.assign(v.semver()) == NSV_RET_OK);
  REQUIRE(str(v) == "1.0.0+2.0.0");

  // A view of v's own strings, and one with them swapped.
  v = ver("1.0.0-4.0.0-rc.9+meta");
  nsv_semver_t view{};
  REQUIRE(nsv_parse(&view, v.pre(), std::strlen(v.pre())) == NSV_RET_OK);
  REQUIRE(v.assign(view) == NSV_RET_OK);
  REQUIRE(str(v) == "4.0.0-rc.9");
  v = ver("1.2.3-abcdefghijklmnop+qrstuvwx");
  nsv_semver_t swapped = v.semver();
  swapped.src = v.pre();
  swapped.pre_ofs = v.semver().build_ofs;
  swapped.pre_len = v.semver().build_len;
  swapped.build_ofs = 0;
  swapped.build_len = v.semver().pre_len;
  REQUIRE(v.assign(swapped) == NSV_RET_OK);
  REQUIRE(str(v) == "1.2.3-qrstuvwx+abcdefghijklmnop");

  // And errors from one: none touches v.
  nsv_semver_t over = v.semver();
  over.src = v.pre();
  over.pre_len = 47;
  over.build_ofs = 0;
  over.build_len = 1;
  fails_unchanged<48>(NSV_RET_ERR_BUF_TOO_SMALL, [&](auto &w) {
    over.src = w.pre();
    return w.assign(over);
  });
  over.pre_len = 65535;
  fails_unchanged<48>(NSV_RET_ERR_TOO_LONG, [&](auto &w) {
    over.src = w.pre();
    return w.assign(over);
  });
}

TEST_CASE("nsv::SemVer: a copy outlives the string it was parsed from") {
  std::string const s = "3.2.1-beta.4+sha.abc";
  auto const scratch = [&] {
    char *const buf = static_cast<char *>(std::malloc(s.size() + 1));
    REQUIRE(buf != nullptr);
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
  };
  auto const scrap = [&](char *buf) {
    std::memset(buf, 'X', s.size());
    std::free(buf);
  };

  V a, b, c, d;
  size_t n = 0;
  char *buf = scratch();
  REQUIRE(a.parse(buf, s.size()) == NSV_RET_OK);
  REQUIRE(b.parse(buf) == NSV_RET_OK);
  REQUIRE(c.parse_prefix(buf, s.size(), n) == NSV_RET_OK);
  nsv_semver_t view{};
  REQUIRE(nsv_parse(&view, buf, s.size()) == NSV_RET_OK);
  REQUIRE(d.assign(view) == NSV_RET_OK);
  V e = from_ok(view);
  scrap(buf);

  for (V const *v : { &a, &b, &c, &d, &e }) {
    REQUIRE(str(*v) == s);
    REQUIRE(std::string(v->pre()) == "beta.4");
    REQUIRE(std::string(v->build()) == "sha.abc");
  }
}

TEST_CASE("nsv::SemVer: from an nsv_semver_t") {
  SUBCASE("a view") {
    std::string const s = "1.2.3-rc.1+b.2";
    nsv_semver_t const view = parse_ok(s);
    V const v = from_ok(view);
    same(v, view);
    V w;
    REQUIRE(w.assign(view) == NSV_RET_OK);
    same(w, view);
  }

  SUBCASE("a copy, freed after") {
    std::string const s = "4.5.6-rc.7+meta.8";
    nsv_test::blob blob = nsv_test::copy(parse_ok(s));
    V const v = from_ok(*blob);
    V w;
    REQUIRE(w.assign(*blob) == NSV_RET_OK);
    std::memset(static_cast<void *>(blob.get()), 0xEE, sizeof(nsv_semver_t));
    blob.reset();
    REQUIRE(str(v) == s);
    REQUIRE(str(w) == s);
  }

  SUBCASE("literals") {
    static nsv_semver_t const full = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
    static nsv_semver_t const bare = NSV_SEMVER_LIT(5, 6, 7, "", "");
    V const v = from_ok(full);
    same(v, full);
    REQUIRE(str(v) == "1.4.0-rc.2+g3f9a1c2");
    REQUIRE(std::string(v.pre()) == "rc.2");
    REQUIRE(std::string(v.build()) == "g3f9a1c2");
    V const w = from_ok(bare);
    same(w, bare);
    REQUIRE(str(w) == "5.6.7");
    same(from_ok<2>(bare), bare);
    from_refused<13>(full, NSV_RET_ERR_BUF_TOO_SMALL);
    same(from_ok<14>(full), full);
  }

  SUBCASE("another version, and zero") {
    V const v = ver("7.8.9-a.b+c");
    V const w = from_ok(v.semver());
    same(w, v.semver());
    REQUIRE(&w.semver() != &v.semver());
    V const z = from_ok(nsv_semver_t{});
    same(z, nsv_semver_t{});
    REQUIRE(z == V());
  }

  SUBCASE("too big for its size: 0.0.0") {
    std::string const s = "1.2.3-abcdefgh";
    nsv_semver_t const view = parse_ok(s);
    from_refused<9>(view, NSV_RET_ERR_BUF_TOO_SMALL);
    same(from_ok<10>(view), view);
  }
}

TEST_CASE("nsv::SemVer: one size from another") {
  Wide const wide = ver<256>("1.2.3-" + std::string(100, 'p') + "+b");  // needs 103
  SemVer<103> fit;
  REQUIRE(fit.assign(wide.semver()) == NSV_RET_OK);
  same(fit, wide.semver());
  REQUIRE(fit == wide);
  fails_unchanged<102>(NSV_RET_ERR_BUF_TOO_SMALL,
                       [&](auto &v) { return v.assign(wide.semver()); });
  fails_unchanged<48>(NSV_RET_ERR_BUF_TOO_SMALL,
                      [&](auto &v) { return v.assign(wide.semver()); });
  Wide back;
  REQUIRE(back.assign(fit.semver()) == NSV_RET_OK);
  REQUIRE(str(back) == str(wide));

  // And compared: any size against any other.
  SemVer<2> const small = ver<2>("1.2.3");
  V const mid = ver("1.2.3-rc.1");
  REQUIRE(mid < small);
  REQUIRE(small > mid);
  REQUIRE(wide < small);
  REQUIRE(mid > wide);
  REQUIRE(small != wide);
  REQUIRE(small == ver<256>("1.2.3+any.build"));
  REQUIRE(small <= ver<13>("1.2.3"));
  REQUIRE(small >= ver<13>("1.2.3"));
}

TEST_CASE("nsv::SemVer: what nsv_copy refuses, before whether it fits") {
  auto huge = std::make_unique<Huge>();
  SUBCASE("a 65535-byte pre-release") {
    std::string const pre(65535, 'p');
    nsv_semver_t big{};
    big.src = pre.data();
    big.pre_len = 65535;
    REQUIRE(nsv_copy(nullptr, 0, &big) == NSV_RET_ERR_TOO_LONG);
    from_refused<48>(big, NSV_RET_ERR_TOO_LONG);
    fails_unchanged<48>(NSV_RET_ERR_TOO_LONG, [&](auto &w) { return w.assign(big); });
    REQUIRE(huge->assign(big) == NSV_RET_ERR_TOO_LONG);
    REQUIRE(state(*huge) == std::string(65536, '\0') + ",0,0,0,0,0,0,0");
  }

  SUBCASE("a 65534-byte pre-release, which only 65536 bytes hold, but for a 16-bit int") {
    std::string const pre(65534, 'p');
    nsv_semver_t big{};
    big.src = pre.data();
    big.pre_len = 65534;
    REQUIRE(need(big) == 65536);
    if (nsv_test::fits_int(sizeof(nsv_semver_t) + 65536)) {
      REQUIRE(huge->assign(big) == NSV_RET_OK);
      REQUIRE(std::string(huge->pre()) == pre);
      REQUIRE(*huge->build() == '\0');
      auto shorter = std::make_unique<SemVer<65535>>();
      REQUIRE(shorter->assign(big) == NSV_RET_ERR_BUF_TOO_SMALL);
      fails_unchanged<48>(NSV_RET_ERR_BUF_TOO_SMALL, [&](auto &w) { return w.assign(big); });
    } else {
      REQUIRE(huge->assign(big) == NSV_RET_ERR_TOO_LONG);
      from_refused<48>(big, NSV_RET_ERR_TOO_LONG);
      fails_unchanged<48>(NSV_RET_ERR_TOO_LONG, [&](auto &w) { return w.assign(big); });
    }
  }

  SUBCASE("the longest parseable version, but for a 16-bit int") {
    std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
    REQUIRE(s.size() == 65535);
    nsv_semver_t const view = parse_ok(s);
    size_t n = 0;
    if (nsv_test::fits_int(sizeof(nsv_semver_t) + 30000 + 35528 + 2)) {
      REQUIRE(huge->assign(view) == NSV_RET_OK);
      REQUIRE(str(*huge) == s);
      REQUIRE(huge->parse(s.data(), s.size()) == NSV_RET_OK);
      REQUIRE(str(*huge) == s);
      REQUIRE(huge->parse_prefix(s.data(), s.size(), n) == NSV_RET_OK);
      REQUIRE(n == s.size());
      REQUIRE(str(*huge) == s);
      fails_unchanged<48>(NSV_RET_ERR_BUF_TOO_SMALL,
                          [&](auto &w) { return w.parse(s.data(), s.size()); });
    } else {
      // nsv_parse accepts it, but its copy is longer than INT_MAX bytes.
      from_refused<48>(view, NSV_RET_ERR_TOO_LONG);
      fails_unchanged<48>(NSV_RET_ERR_TOO_LONG, [&](auto &w) { return w.assign(view); });
      fails_unchanged<48>(NSV_RET_ERR_TOO_LONG,
                          [&](auto &w) { return w.parse(s.data(), s.size()); });
      fails_unchanged<48>(NSV_RET_ERR_TOO_LONG, [&](auto &w) {
        n = 0;
        return w.parse_prefix(s.data(), s.size(), n);
      });
      REQUIRE(n == s.size());  // spanning the version that does not fit
      REQUIRE(huge->parse(s.data(), s.size()) == NSV_RET_ERR_TOO_LONG);
    }
  }
}

TEST_CASE("nsv::SemVer: copies") {
  V const a = ver("1.2.3-rc.1+b.7");
  V b(a);
  REQUIRE(str(b) == "1.2.3-rc.1+b.7");
  REQUIRE(&b.semver() != &a.semver());
  REQUIRE(b.pre() != a.pre());  // its own strings
  REQUIRE(within(b, b.pre(), 5));
  REQUIRE(within(b, b.build(), 4));
  REQUIRE(state(b) == state(a));

  // A copy outlives its source.
  V c;
  {
    V const src = ver("4.0.0-x+y");
    c = src;
    REQUIRE(&c.semver() != &src.semver());
  }
  REQUIRE(str(c) == "4.0.0-x+y");
  V const d(c);
  c = ver("0.1.0");
  REQUIRE(str(d) == "4.0.0-x+y");

  // Assignment replaces, strings and all.
  b = c;
  REQUIRE(str(b) == "0.1.0");
  REQUIRE(*b.pre() == '\0');
  REQUIRE(*b.build() == '\0');
  V const none;
  b = none;
  REQUIRE(state(b) == state(none));

  // Self-assignment, through a reference so the compiler does not object.
  V &rb = b, &rc = c;
  b = rb;
  REQUIRE(state(b) == state(none));
  c = rc;
  REQUIRE(str(c) == "0.1.0");

  V x, y;
  x = y = a;
  REQUIRE(str(x) == "1.2.3-rc.1+b.7");
  REQUIRE(str(y) == "1.2.3-rc.1+b.7");
  REQUIRE(&x.semver() != &y.semver());

  // Plain bytes: memcpy copies a version too.
  V z;
  std::memcpy(static_cast<void *>(&z), &a, sizeof a);
  REQUIRE(str(z) == "1.2.3-rc.1+b.7");
  REQUIRE(within(z, z.pre(), 5));
}

TEST_CASE("nsv::SemVer: a move is a copy") {
  V a = ver("1.2.3-rc.1+b.7");
  V b(static_cast<V &&>(a));
  REQUIRE(str(b) == "1.2.3-rc.1+b.7");
  REQUIRE(str(a) == "1.2.3-rc.1+b.7");  // as it was
  REQUIRE(b.pre() != a.pre());

  V c = ver("5.5.5");
  c = static_cast<V &&>(b);
  REQUIRE(str(c) == "1.2.3-rc.1+b.7");
  REQUIRE(str(b) == "1.2.3-rc.1+b.7");

  V &rc = c;
  c = static_cast<V &&>(rc);
  REQUIRE(str(c) == "1.2.3-rc.1+b.7");

  REQUIRE(a.parse("2.0.0") == NSV_RET_OK);
  std::swap(a, c);
  REQUIRE(str(a) == "1.2.3-rc.1+b.7");
  REQUIRE(str(c) == "2.0.0");
  REQUIRE(within(a, a.pre(), 5));

  c = V();
  REQUIRE(state(c) == state(V()));
}

TEST_CASE("nsv::SemVer: in a growing vector, and sorted") {
  std::vector<V> vs;
  for (unsigned i = 0; i < 100; ++i) {
    vs.push_back(ver(std::to_string(i) + ".0.0-rc." + std::to_string(i)));
  }
  for (unsigned i = 0; i < 100; ++i) {
    REQUIRE(str(vs[i]) == std::to_string(i) + ".0.0-rc." + std::to_string(i));
    REQUIRE(within(vs[i], vs[i].pre(), std::strlen(vs[i].pre()) + 1));
  }

  std::vector<V> sorted;
  for (char const *s : k_semver_org) {
    sorted.push_back(ver(s));
  }
  sorted.emplace_back();
  std::rotate(sorted.begin(), sorted.begin() + 3, sorted.end());
  std::reverse(sorted.begin(), sorted.end());
  std::sort(sorted.begin(), sorted.end());
  REQUIRE(str(sorted[0]) == "0.0.0");
  for (size_t i = 0; i < std::size(k_semver_org); ++i) {
    REQUIRE(str(sorted[i + 1]) == k_semver_org[i]);
  }
}

TEST_CASE("nsv::SemVer: comparisons agree with nsv_cmp") {
  std::vector<std::string> strs = { "0.0.0-0", "0.0.0", "0.0.0+b", "0.0.1", "1.0.0-0",
                                    "1.0.0-alpha+x", "1.0.0-alpha+y", "1.0.0-rc.1+build.1",
                                    "1.0.0+a", "1.0.0+b", "1.0.1", "1.9.0", "1.10.0",
                                    "2.0.0-rc.1", "4294967295.4294967295.4294967295" };
  append(strs, corpus::k_valid);
  append(strs, k_semver_org);
  std::vector<nsv_semver_t> views;
  std::vector<Wide> vs;
  std::vector<SemVer<64>> ws;  // another size, to compare across
  for (std::string const &s : strs) {
    views.push_back(parse_ok(s));
    vs.push_back(ver<256>(s));
    ws.push_back(ver<64>(s));
  }
  views.push_back(nsv_semver_t{});
  vs.emplace_back();
  ws.emplace_back();

  for (size_t i = 0; i < vs.size(); ++i) {
    for (size_t j = 0; j < vs.size(); ++j) {
      INFO(str(vs[i]), " against ", str(vs[j]));
      Wide const &a = vs[i], &b = vs[j];
      SemVer<64> const &bw = ws[j];
      int const c = nsv_cmp(&views[i], &views[j]);
      REQUIRE(nsv_cmp(&a.semver(), &b.semver()) == c);
      REQUIRE((a == b) == (c == NSV_CMP_EQ));
      REQUIRE((a != b) == (c != NSV_CMP_EQ));
      REQUIRE((a < b) == (c == NSV_CMP_LT));
      REQUIRE((a <= b) == (c != NSV_CMP_GT));
      REQUIRE((a > b) == (c == NSV_CMP_GT));
      REQUIRE((a >= b) == (c != NSV_CMP_LT));
      REQUIRE((a == bw) == (c == NSV_CMP_EQ));
      REQUIRE((a != bw) == (c != NSV_CMP_EQ));
      REQUIRE((a < bw) == (c == NSV_CMP_LT));
      REQUIRE((a <= bw) == (c != NSV_CMP_GT));
      REQUIRE((a > bw) == (c == NSV_CMP_GT));
      REQUIRE((a >= bw) == (c != NSV_CMP_LT));
      REQUIRE((bw < a) == (a > bw));
    }
  }
}

TEST_CASE("nsv::SemVer: comparisons on generated versions") {
  nsv_fuzz::rng r(0x6e737668707021ull);
  std::vector<Wide> vs;
  std::vector<std::string> strs;
  size_t too_big = 0;
  for (int i = 0; i < 2000; ++i) {  // about a fifth are versions
    std::string const s = nsv_fuzz::gen_version(r);
    nsv_semver_t c{};
    int const rc = nsv_parse(&c, s.data(), s.size());
    Wide v;
    REQUIRE(v.parse(s.data(), s.size()) == expect<256>(rc, c));
    V d;
    REQUIRE(d.parse(s.data(), s.size()) == expect<48>(rc, c));
    if (rc == NSV_RET_OK) {
      vs.push_back(v);
      strs.push_back(s);
      too_big += (need(c) > 48);
    }
  }
  REQUIRE(vs.size() > 300);
  REQUIRE(too_big > 10);  // so the default size's refusals are tested too
  for (size_t i = 0; i < vs.size(); ++i) {
    for (size_t j = i; j < vs.size(); ++j) {
      INFO(strs[i], " against ", strs[j]);
      int const c = nsv_test::cmp(parse_ok(strs[i]), parse_ok(strs[j])) + 1;
      Wide const &a = vs[i], &b = vs[j];
      REQUIRE((a == b) == (c == NSV_CMP_EQ));
      REQUIRE((a != b) == (c != NSV_CMP_EQ));
      REQUIRE((a < b) == (c == NSV_CMP_LT));
      REQUIRE((a <= b) == (c != NSV_CMP_GT));
      REQUIRE((a > b) == (c == NSV_CMP_GT));
      REQUIRE((a >= b) == (c != NSV_CMP_LT));
      REQUIRE((b < a) == (a > b));
      REQUIRE((b == a) == (a == b));
    }
  }
}

TEST_CASE("nsv::SemVer: == is equal precedence, not identity") {
  V const a = ver("1.0.0+a"), b = ver("1.0.0+b");
  REQUIRE(a == b);
  REQUIRE_FALSE(a != b);
  REQUIRE_FALSE(a < b);
  REQUIRE_FALSE(a > b);
  REQUIRE(a <= b);
  REQUIRE(a >= b);
  REQUIRE(str(a) != str(b));
  REQUIRE(ver("1.0.0-rc.1") < ver("1.0.0"));
  REQUIRE(ver("1.0.0-beta.2") < ver("1.0.0-beta.11"));
  REQUIRE(ver("1.0.0-alpha") < ver("1.0.0-alpha.1"));
  REQUIRE(ver("1.0.0-1") < ver("1.0.0-alpha"));
  REQUIRE(ver("1.0.0-RC") < ver("1.0.0-rc"));
  REQUIRE(ver("1.9.0") < ver("1.10.0"));
}

TEST_CASE("nsv::SemVer: matches, against nsv_range_match") {
  static nsv_test::guarded g;
  for (range_cases::table const &t : range_cases::k_all) {
    for (unsigned i = 0; i < t.n; ++i) {
      range_cases::match_case const &c = t.rows[i];
      INFO("\"", c.range, "\" against ", c.version);
      std::string const range = c.range, vs = c.version;
      nsv_semver_t const view = parse_ok(vs);
      V const v = ver(vs);
      size_t const n = range.size();
      REQUIRE(v.matches(g.place(range), n) == c.plain);
      REQUIRE(v.matches(g.place(range), n, 0) == c.plain);
      REQUIRE(v.matches(g.place(range), n, NSV_RANGE_EXCLUDE_PRE) == c.exclude_pre);
      REQUIRE(v.matches(place_nul(g, range)) == c.plain);
      REQUIRE(nsv_range_match(range.data(), n, &view, 0) == c.plain);
      REQUIRE(nsv_range_match(range.data(), n, &view, NSV_RANGE_EXCLUDE_PRE) ==
              c.exclude_pre);
    }
  }
}

TEST_CASE("nsv::SemVer: matches errors") {
  V const v = ver("1.5.0");
  for (char const *r : { ">>>bad", "1.2.3.4", "alpha", ">=1.0.0 alpha", "1.0.0 || beta" }) {
    INFO("\"", r, "\"");
    REQUIRE(v.matches(r) == NSV_RET_ERR_SYNTAX);
    REQUIRE(v.matches(r, std::strlen(r)) == NSV_RET_ERR_SYNTAX);
    REQUIRE(v.matches(r, std::strlen(r), NSV_RANGE_EXCLUDE_PRE) == NSV_RET_ERR_SYNTAX);
  }
  REQUIRE(v.matches(nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(v.matches(nullptr, 0) == NSV_RET_ERR_ARG);
  REQUIRE(v.matches(nullptr, 5, NSV_RANGE_EXCLUDE_PRE) == NSV_RET_ERR_ARG);
  REQUIRE(v.matches("<4294967296.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(v.matches(">=1.0.0 <2 || >=4294967296") == NSV_RET_ERR_OVERFLOW);
  std::string const huge = "=1.0.0-" + std::string(65530, 'p');
  REQUIRE(v.matches(huge.c_str()) == NSV_RET_ERR_TOO_LONG);
  REQUIRE(v.matches(huge.data(), huge.size(), NSV_RANGE_EXCLUDE_PRE) == NSV_RET_ERR_TOO_LONG);
}

TEST_CASE("nsv::SemVer: matches reads only what it is given") {
  V const v = ver("1.5.0");
  char const r[] = ">=1.0.0 <2.0.0 junk";
  REQUIRE(v.matches(r, 14) == 1);
  REQUIRE(v.matches(r, 15) == 1);
  REQUIRE(v.matches(r) == NSV_RET_ERR_SYNTAX);
  char const nul[] = ">=1.0.0 <2.0.0\0junk";
  REQUIRE(v.matches(nul) == 1);
  REQUIRE(v.matches(nul, sizeof(nul) - 1) == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("nsv::SemVer: matches on 0.0.0") {
  V const v;
  REQUIRE(v.matches("0.0.0") == 1);
  REQUIRE(v.matches("*") == 1);
  REQUIRE(v.matches(">=0.0.0-0 <0.0.1") == 1);
  REQUIRE(v.matches(">0.0.0") == 0);
  REQUIRE(v.matches("<0.0.0") == 0);
  REQUIRE(v.matches("^0.0.1") == 0);
  REQUIRE(v.matches("0.0.0", 5, NSV_RANGE_EXCLUDE_PRE) == 1);
}

TEST_CASE("nsv::SemVer: format") {
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    Wide const v = ver<256>(s);
    int const n = v.format(nullptr, 0);
    REQUIRE(n == static_cast<int>(s.size()));
    REQUIRE(v.format(nullptr, 99) == n);  // n is ignored
    size_t const len = s.size();

    std::string buf(len + 8, '\x7f');
    REQUIRE(v.format(&buf[0], len + 1) == n);
    REQUIRE(buf.compare(0, len, s) == 0);
    REQUIRE(buf[len] == '\0');
    REQUIRE(buf[len + 1] == '\x7f');

    // Never truncated: one byte short is no version at all.
    buf.assign(len + 8, '\x7f');
    REQUIRE(v.format(&buf[0], len) == NSV_RET_ERR_BUF_TOO_SMALL);
    REQUIRE(buf[0] == '\0');
    REQUIRE(buf.find_first_not_of('\x7f', 1) == std::string::npos);
    buf.assign(len + 8, '\x7f');
    REQUIRE(v.format(&buf[0], 0) == NSV_RET_ERR_BUF_TOO_SMALL);
    REQUIRE(buf.find_first_not_of('\x7f') == std::string::npos);

    // And back.
    Wide const w = ver<256>(str(v));
    REQUIRE(w == v);
    REQUIRE(str(w) == s);
  }
}

TEST_CASE("nsv::SemVer: format refuses a length over INT_MAX") {
  // A copy of exactly INT_MAX bytes with the longest numbers formats to
  // INT_MAX + 32 - sizeof(nsv_semver_t) bytes, too long where the header is under 32 bytes.
  // Only a 16-bit int makes such a version short enough to parse.
  if (!nsv_test::fits_int(65536)) {
    size_t const max = static_cast<size_t>(nsv_test::k_int_max);
    std::string const s = "4294967295.4294967295.4294967295-" +
                          std::string(max - sizeof(nsv_semver_t) - 3, 'p') + "+b";
    auto const v = std::make_unique<SemVer<32768>>();
    REQUIRE(v->parse(s.data(), s.size()) == NSV_RET_OK);
    REQUIRE(nsv_copy(nullptr, 0, &v->semver()) == nsv_test::k_int_max);
    int const want = nsv_test::fits_int(s.size()) ? static_cast<int>(s.size())
                                                  : NSV_RET_ERR_TOO_LONG;
    REQUIRE(v->format(nullptr, 0) == want);
    std::string buf(s.size() + 1, '\x7f');
    REQUIRE(v->format(&buf[0], buf.size()) == want);
  }
}

TEST_CASE("nsv::SemVer: accessors") {
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    same(ver<256>(s), parse_ok(s));
  }

  V const max = ver("4294967295.4294967295.4294967295");
  REQUIRE(max.major() == UINT32_MAX);
  REQUIRE(max.minor() == UINT32_MAX);
  REQUIRE(max.patch() == UINT32_MAX);

  V const v = ver("10.20.30-rc.1+build.7");
  REQUIRE(v.major() == 10);
  REQUIRE(v.minor() == 20);
  REQUIRE(v.patch() == 30);
  REQUIRE(std::strcmp(v.pre(), "rc.1") == 0);
  REQUIRE(std::strcmp(v.build(), "build.7") == 0);
  struct {
    char const *s, *pre, *build;
  } const parts[] = {
    { "1.2.3", "", "" },
    { "1.2.3-a", "a", "" },
    { "1.2.3+b", "", "b" },
    { "1.2.3-a.b+c.d", "a.b", "c.d" },
  };
  for (auto const &p : parts) {
    INFO(p.s);
    V const w = ver(p.s);
    REQUIRE(w.pre() != nullptr);
    REQUIRE(std::strcmp(w.pre(), p.pre) == 0);
    REQUIRE(w.build() != nullptr);
    REQUIRE(std::strcmp(w.build(), p.build) == 0);
  }
}

TEST_CASE("nsv::SemVer: README example") {
  // README.md's C++ example, line for line.
  nsv::SemVer<> v, w;  // 0.0.0; 48 bytes for the pre-release and build, in place
  if (v.parse("1.4.0-rc.2+g3f9a1c2") != NSV_RET_OK) { /* not a version: v is unchanged */ }
  w.parse("1.4.0");
  bool const older = v < w;  // true: a pre-release ranks below its release
  int const in = v.matches(">=1.2.0 <2.0.0 || ^3.1.0");  // 1 if so, 0 if not, < 0 on error
  char buf[32];
  int const len = v.format(buf, sizeof buf);  // 19: "1.4.0-rc.2+g3f9a1c2"
  char const *pre = v.pre(), *build = v.build();  // "rc.2" and "g3f9a1c2"
  nsv::SemVer<8> small;  // 8 bytes: too few for these two strings and their NULs
  int const rc = small.parse("1.4.0-rc.2+g3f9a1c2");  // NSV_RET_ERR_BUF_TOO_SMALL

  REQUIRE(str(v) == "1.4.0-rc.2+g3f9a1c2");
  REQUIRE(str(w) == "1.4.0");
  REQUIRE(older);
  REQUIRE(in == 1);
  REQUIRE(len == 19);
  REQUIRE(std::string(buf) == "1.4.0-rc.2+g3f9a1c2");
  REQUIRE(std::string(pre) == "rc.2");
  REQUIRE(std::string(build) == "g3f9a1c2");
  REQUIRE(rc == NSV_RET_ERR_BUF_TOO_SMALL);
  REQUIRE(str(small) == "0.0.0");
}

TEST_CASE("nsv::SemVer: semver() for the C API") {
  std::string const s = "1.2.3-rc.1+b.2";
  V const v = ver(s);
  nsv_semver_t const view = parse_ok(s);
  nsv_semver_t const &c = v.semver();
  REQUIRE(c.src == nullptr);
  REQUIRE(nsv_copy(nullptr, 0, &c) == nsv_copy(nullptr, 0, &view));
  REQUIRE(nsv_test::format(c) == s);
  REQUIRE(nsv_range_match("^1.2.3-rc.0", 11, &c, NSV_RANGE_EXCLUDE_PRE) == 1);
  nsv_test::blob const blob = nsv_test::copy(c);
  REQUIRE(nsv_test::format(*blob) == s);
  REQUIRE(from_ok(*blob) == v);
}
