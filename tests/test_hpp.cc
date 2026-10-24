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
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using nsv::SemVer;
using nsv_test::parse_ok;

namespace {

// What the class promises about itself.
static_assert(sizeof(SemVer) == sizeof(void *), "one pointer");
static_assert(std::is_nothrow_default_constructible<SemVer>::value, "");
static_assert(std::is_nothrow_move_constructible<SemVer>::value, "");
static_assert(std::is_nothrow_move_assignable<SemVer>::value, "");
static_assert(std::is_nothrow_destructible<SemVer>::value, "");
static_assert(!std::is_nothrow_copy_constructible<SemVer>::value, "a copy allocates");
static_assert(!std::is_nothrow_copy_assignable<SemVer>::value, "a copy allocates");
static_assert(std::is_constructible<SemVer, nsv_semver_t const &>::value, "");
static_assert(!std::is_convertible<nsv_semver_t const &, SemVer>::value, "explicit");
static_assert(!noexcept(std::declval<SemVer &>().parse("", 0)), "parsing allocates");
static_assert(!noexcept(std::declval<SemVer &>().assign(nsv_semver_t{})), "so does this");
static_assert(noexcept(std::declval<SemVer const &>().matches("", 0)), "");
static_assert(noexcept(std::declval<SemVer const &>().format(nullptr, 0)), "");
static_assert(noexcept(std::declval<SemVer const &>() < std::declval<SemVer const &>()),
              "");

// Constant-initialized: a global version costs nothing at startup.
constinit SemVer g_version;

// v as a string, through version::format.
std::string str(SemVer const &v) {
  int const n = v.format(nullptr, 0);
  REQUIRE(n >= 0);
  std::string s(static_cast<size_t>(n) + 1, '\x7f');
  REQUIRE(v.format(&s[0], s.size()) == n);
  REQUIRE(s[static_cast<size_t>(n)] == '\0');
  s.resize(static_cast<size_t>(n));
  return s;
}

// s, parsed into a version, or a failed test.
SemVer ver(std::string const &s) {
  SemVer v;
  INFO("parsing \"", s, "\"");
  REQUIRE(v.parse(s.data(), s.size()) == NSV_RET_OK);
  return v;
}

// The header of every version without a blob.
nsv_semver_t const *zero_header() { return &SemVer().semver(); }

// Places s and its NUL so the NUL is the last byte before a guard page.
char const *place_nul(nsv_test::guarded &g, std::string const &s) {
  return g.place(std::string_view(s.c_str(), s.size() + 1));
}

// Every part of v agrees with c, a view, literal or copy.
void same(SemVer const &v, nsv_semver_t const &c) {
  REQUIRE(v.major() == c.major);
  REQUIRE(v.minor() == c.minor);
  REQUIRE(v.patch() == c.patch);
  // NUL-terminated exactly where the C API's length ends.
  REQUIRE(std::string(v.pre()) == nsv_test::pre(c));
  REQUIRE(std::string(v.build()) == nsv_test::build(c));
  REQUIRE(str(v) == nsv_test::format(c));
  REQUIRE(nsv_cmp(&v.semver(), &c) == NSV_CMP_EQ);
  REQUIRE(nsv_cmp(&c, &v.semver()) == NSV_CMP_EQ);
  REQUIRE(v.semver().src == nullptr);  // a copy's header, its strings after it
  char const *p = nullptr;
  REQUIRE(nsv_get_pre(&v.semver(), &p) == static_cast<int>(std::strlen(v.pre())));
  REQUIRE(p == v.pre());
  REQUIRE(nsv_get_build(&v.semver(), &p) == static_cast<int>(std::strlen(v.build())));
  REQUIRE(p == v.build());
}

// f fails with want, on a version holding a blob and on one without, and changes neither.
template <class F>
void fails_unchanged(int want, F const &f) {
  SemVer v = ver("9.8.7-keep+me");
  nsv_semver_t const *const h = &v.semver();
  REQUIRE(f(v) == want);
  REQUIRE(&v.semver() == h);
  REQUIRE(str(v) == "9.8.7-keep+me");
  SemVer d;
  REQUIRE(f(d) == want);
  REQUIRE(&d.semver() == zero_header());
}

// semver.org's precedence example, ascending.
char const *const k_semver_org[] = {
  "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0",
};

template <size_t N>
void append(std::vector<std::string> &out, char const *const (&list)[N]) {
  out.insert(out.end(), std::begin(list), std::end(list));
}

}  // namespace

TEST_CASE("nsv::SemVer: default is 0.0.0, with no blob") {
  SemVer const v;
  same(v, nsv_semver_t{});
  REQUIRE(str(v) == "0.0.0");
  REQUIRE(v.pre() != nullptr);
  REQUIRE(*v.pre() == '\0');
  REQUIRE(v.build() != nullptr);
  REQUIRE(*v.build() == '\0');
  // Every version without a blob shares one header.
  SemVer const w;
  REQUIRE(&v.semver() == &w.semver());
  REQUIRE(&g_version.semver() == &v.semver());
  REQUIRE(v == ver("0.0.0"));
  REQUIRE(v == ver("0.0.0+b"));
  REQUIRE(v > ver("0.0.0-0"));
  REQUIRE(v < ver("0.0.1"));
}

TEST_CASE("nsv::SemVer: parse, against nsv_parse") {
  static nsv_test::guarded g;
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    nsv_semver_t const view = parse_ok(s);
    SemVer v;
    REQUIRE(v.parse(g.place(s), s.size()) == NSV_RET_OK);
    same(v, view);
    REQUIRE(str(v) == s);
    SemVer w;
    REQUIRE(w.parse(place_nul(g, s)) == NSV_RET_OK);
    same(w, view);
    // Over a version that already has a blob.
    REQUIRE(w.parse(s.data(), s.size()) == NSV_RET_OK);
    same(w, view);
  }
  std::vector<std::string> bad;
  append(bad, corpus::k_invalid);
  append(bad, corpus::k_overflow);
  for (std::string const &s : bad) {
    INFO("\"", s, "\"");
    int const want = nsv_test::parse_rc(s);
    REQUIRE(want < 0);
    fails_unchanged(want, [&](SemVer &v) { return v.parse(g.place(s), s.size()); });
    fails_unchanged(want, [&](SemVer &v) { return v.parse(place_nul(g, s)); });
  }
}

TEST_CASE("nsv::SemVer: parse errors") {
  fails_unchanged(NSV_RET_ERR_ARG, [](SemVer &v) { return v.parse(nullptr, 5); });
  fails_unchanged(NSV_RET_ERR_ARG, [](SemVer &v) { return v.parse(nullptr, 0); });
  fails_unchanged(NSV_RET_ERR_ARG, [](SemVer &v) { return v.parse(nullptr); });
  fails_unchanged(NSV_RET_ERR_SYNTAX, [](SemVer &v) { return v.parse(""); });
  fails_unchanged(NSV_RET_ERR_SYNTAX, [](SemVer &v) { return v.parse("v1.2.3"); });
  fails_unchanged(NSV_RET_ERR_SYNTAX, [](SemVer &v) { return v.parse("1.2.3", 4); });
  fails_unchanged(NSV_RET_ERR_OVERFLOW, [](SemVer &v) { return v.parse("4294967296.0.0"); });
  std::string const huge = "1.0.0-" + std::string(65530, 'p');  // 65536 bytes
  fails_unchanged(NSV_RET_ERR_TOO_LONG, [&](SemVer &v) { return v.parse(huge.c_str()); });
  fails_unchanged(NSV_RET_ERR_TOO_LONG,
                  [&](SemVer &v) { return v.parse(huge.data(), huge.size()); });
}

TEST_CASE("nsv::SemVer: parse reads only what it is given") {
  SemVer v;
  REQUIRE(v.parse("1.2.3-rc.1junk", 10) == NSV_RET_OK);
  REQUIRE(str(v) == "1.2.3-rc.1");
  char const nul[] = "2.0.0\0-junk";
  REQUIRE(v.parse(nul) == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0");
  REQUIRE(v.parse(nul, sizeof(nul) - 1) == NSV_RET_ERR_SYNTAX);
  REQUIRE(str(v) == "2.0.0");
}

TEST_CASE("nsv::SemVer: parse_prefix, against nsv_parse_prefix") {
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
    int const want = nsv_parse_prefix(&c, s.data(), s.size(), &cn);
    REQUIRE(want != NSV_RET_ERR_ARG);
    size_t n = 54321;
    if (want == NSV_RET_OK) {
      SemVer v = ver("9.8.7-keep+me");
      REQUIRE(v.parse_prefix(g.place(s), s.size(), n) == NSV_RET_OK);
      REQUIRE(n == cn);
      same(v, c);
    } else {
      fails_unchanged(want, [&](SemVer &v) {
        n = 54321;
        int const rc = v.parse_prefix(g.place(s), s.size(), n);
        REQUIRE(n == cn);
        return rc;
      });
    }
  }
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
    SemVer v = ver("9.8.7-keep+me");
    size_t n = 12345;
    REQUIRE(v.parse_prefix(c.s, std::strlen(c.s), n) == c.rc);
    REQUIRE(n == c.consumed);
    REQUIRE(str(v) == (c.version ? c.version : "9.8.7-keep+me"));
  }

  size_t n = 7;
  fails_unchanged(NSV_RET_ERR_ARG, [&](SemVer &v) { return v.parse_prefix(nullptr, 5, n); });
  REQUIRE(n == 7);

  std::string const huge = "1.0.0-" + std::string(65530, 'p') + " rest";
  fails_unchanged(NSV_RET_ERR_TOO_LONG,
                  [&](SemVer &v) { return v.parse_prefix(huge.data(), huge.size(), n); });
  REQUIRE(n == huge.size() - 5);
}

TEST_CASE("nsv::SemVer: parsing its own strings") {
  SemVer v = ver("1.0.0-3.0.0+2.0.0");
  REQUIRE(v.parse(v.build()) == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0");

  v = ver("1.0.0-3.0.0+2.0.0");
  size_t n = 0;
  REQUIRE(v.parse_prefix(v.pre(), std::strlen(v.pre()), n) == NSV_RET_OK);
  REQUIRE(n == 5);
  REQUIRE(str(v) == "3.0.0");

  v = ver("1.0.0+2.0.0");
  REQUIRE(v.assign(v.semver()) == NSV_RET_OK);
  REQUIRE(str(v) == "1.0.0+2.0.0");
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

  SemVer a, b, c, d;
  size_t n = 0;
  char *buf = scratch();
  REQUIRE(a.parse(buf, s.size()) == NSV_RET_OK);
  REQUIRE(b.parse(buf) == NSV_RET_OK);
  REQUIRE(c.parse_prefix(buf, s.size(), n) == NSV_RET_OK);
  nsv_semver_t view{};
  REQUIRE(nsv_parse(&view, buf, s.size()) == NSV_RET_OK);
  REQUIRE(d.assign(view) == NSV_RET_OK);
  SemVer e(view);
  scrap(buf);

  for (SemVer const *v : { &a, &b, &c, &d, &e }) {
    REQUIRE(str(*v) == s);
    REQUIRE(std::string(v->pre()) == "beta.4");
    REQUIRE(std::string(v->build()) == "sha.abc");
  }
}

TEST_CASE("nsv::SemVer: from an nsv_semver_t") {
  SUBCASE("a view") {
    std::string const s = "1.2.3-rc.1+b.2";
    nsv_semver_t const view = parse_ok(s);
    SemVer const v(view);
    same(v, view);
    SemVer w;
    REQUIRE(w.assign(view) == NSV_RET_OK);
    same(w, view);
  }

  SUBCASE("a copy, freed after") {
    std::string const s = "4.5.6-rc.7+meta.8";
    nsv_test::blob blob = nsv_test::copy(parse_ok(s));
    SemVer const v(*blob);
    SemVer w;
    REQUIRE(w.assign(*blob) == NSV_RET_OK);
    std::memset(static_cast<void *>(blob.get()), 0xEE, sizeof(nsv_semver_t));
    blob.reset();
    REQUIRE(str(v) == s);
    REQUIRE(str(w) == s);
  }

  SUBCASE("literals") {
    static nsv_semver_t const full = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
    static nsv_semver_t const bare = NSV_SEMVER_LIT(5, 6, 7, "", "");
    SemVer const v(full);
    same(v, full);
    REQUIRE(str(v) == "1.4.0-rc.2+g3f9a1c2");
    REQUIRE(std::string(v.pre()) == "rc.2");
    REQUIRE(std::string(v.build()) == "g3f9a1c2");
    SemVer const w(bare);
    same(w, bare);
    REQUIRE(str(w) == "5.6.7");
  }

  SUBCASE("another version, and zero") {
    SemVer const v = ver("7.8.9-a.b+c");
    SemVer const w(v.semver());
    same(w, v.semver());
    REQUIRE(&w.semver() != &v.semver());
    SemVer const z(nsv_semver_t{});
    same(z, nsv_semver_t{});
    REQUIRE(z == SemVer());
  }
}

TEST_CASE("nsv::SemVer: what nsv_copy refuses") {
  SUBCASE("a 65535-byte pre-release") {
    std::string const pre(65535, 'p');
    nsv_semver_t big{};
    big.src = pre.data();
    big.pre_len = 65535;
    REQUIRE(nsv_copy(nullptr, 0, &big) == NSV_RET_ERR_TOO_LONG);
    SemVer const v(big);
    REQUIRE(&v.semver() == zero_header());
    fails_unchanged(NSV_RET_ERR_TOO_LONG, [&](SemVer &w) { return w.assign(big); });
  }

  SUBCASE("a 65534-byte pre-release still fits, but for a 16-bit int") {
    std::string const pre(65534, 'p');
    nsv_semver_t big{};
    big.src = pre.data();
    big.pre_len = 65534;
    if (nsv_test::fits_int(sizeof(nsv_semver_t) + 65534 + 2)) {
      SemVer const v(big);
      REQUIRE(std::string(v.pre()) == pre);
      REQUIRE(*v.build() == '\0');
    } else {
      SemVer const v(big);
      REQUIRE(&v.semver() == zero_header());
      fails_unchanged(NSV_RET_ERR_TOO_LONG, [&](SemVer &w) { return w.assign(big); });
    }
  }

  SUBCASE("the longest parseable version, but for a 16-bit int") {
    std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
    REQUIRE(s.size() == 65535);
    nsv_semver_t const view = parse_ok(s);
    size_t n = 0;
    if (nsv_test::fits_int(sizeof(nsv_semver_t) + 30000 + 35528 + 2)) {
      SemVer v(view);
      REQUIRE(str(v) == s);
      REQUIRE(v.parse(s.data(), s.size()) == NSV_RET_OK);
      REQUIRE(str(v) == s);
      REQUIRE(v.parse_prefix(s.data(), s.size(), n) == NSV_RET_OK);
      REQUIRE(n == s.size());
      REQUIRE(str(v) == s);
    } else {
      // nsv_parse accepts it, but its copy is longer than INT_MAX bytes.
      SemVer const v(view);
      REQUIRE(&v.semver() == zero_header());
      fails_unchanged(NSV_RET_ERR_TOO_LONG, [&](SemVer &w) { return w.assign(view); });
      fails_unchanged(NSV_RET_ERR_TOO_LONG,
                      [&](SemVer &w) { return w.parse(s.data(), s.size()); });
      fails_unchanged(NSV_RET_ERR_TOO_LONG, [&](SemVer &w) {
        n = 0;
        return w.parse_prefix(s.data(), s.size(), n);
      });
      REQUIRE(n == s.size());  // spanning the version that does not fit
    }
  }
}

TEST_CASE("nsv::SemVer: copies") {
  SemVer const a = ver("1.2.3-rc.1+b.7");
  SemVer b(a);
  REQUIRE(str(b) == "1.2.3-rc.1+b.7");
  REQUIRE(&b.semver() != &a.semver());
  REQUIRE(b.pre() != a.pre());  // its own strings

  // A copy outlives its source.
  SemVer c;
  {
    SemVer const src = ver("4.0.0-x+y");
    c = src;
    REQUIRE(&c.semver() != &src.semver());
  }
  REQUIRE(str(c) == "4.0.0-x+y");
  SemVer const d(c);
  c = ver("0.1.0");
  REQUIRE(str(d) == "4.0.0-x+y");

  // Assignment replaces, and from a version without a blob drops the blob.
  b = c;
  REQUIRE(str(b) == "0.1.0");
  SemVer const none;
  b = none;
  REQUIRE(&b.semver() == zero_header());
  SemVer const e(none);
  REQUIRE(&e.semver() == zero_header());

  // Self-assignment, through a reference so the compiler does not object.
  SemVer &rb = b, &rc = c;
  b = rb;
  REQUIRE(&b.semver() == zero_header());
  c = rc;
  REQUIRE(str(c) == "0.1.0");

  SemVer x, y;
  x = y = a;
  REQUIRE(str(x) == "1.2.3-rc.1+b.7");
  REQUIRE(str(y) == "1.2.3-rc.1+b.7");
  REQUIRE(&x.semver() != &y.semver());
}

TEST_CASE("nsv::SemVer: moves") {
  SemVer a = ver("1.2.3-rc.1+b.7");
  nsv_semver_t const *const h = &a.semver();
  char const *const pre = a.pre();

  SemVer b(static_cast<SemVer &&>(a));
  REQUIRE(&b.semver() == h);  // the same blob, handed over
  REQUIRE(b.pre() == pre);
  REQUIRE(&a.semver() == zero_header());
  REQUIRE(str(a) == "0.0.0");
  REQUIRE(a.parse("2.0.0") == NSV_RET_OK);  // and usable
  REQUIRE(str(a) == "2.0.0");

  SemVer c = ver("5.5.5");
  c = static_cast<SemVer &&>(b);
  REQUIRE(&c.semver() == h);
  REQUIRE(&b.semver() == zero_header());
  REQUIRE(str(b) == "0.0.0");

  SemVer &rc = c;
  c = static_cast<SemVer &&>(rc);
  REQUIRE(&c.semver() == h);
  REQUIRE(str(c) == "1.2.3-rc.1+b.7");

  std::swap(a, c);
  REQUIRE(&a.semver() == h);
  REQUIRE(str(c) == "2.0.0");

  c = SemVer();
  REQUIRE(&c.semver() == zero_header());
  SemVer d(static_cast<SemVer &&>(c));
  REQUIRE(&d.semver() == zero_header());
}

TEST_CASE("nsv::SemVer: in a growing vector, and sorted") {
  std::vector<SemVer> vs;
  for (unsigned i = 0; i < 100; ++i) {
    vs.push_back(ver(std::to_string(i) + ".0.0-rc." + std::to_string(i)));
  }
  for (unsigned i = 0; i < 100; ++i) {
    REQUIRE(str(vs[i]) == std::to_string(i) + ".0.0-rc." + std::to_string(i));
  }

  std::vector<SemVer> sorted;
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
  std::vector<SemVer> vs;
  for (std::string const &s : strs) {
    views.push_back(parse_ok(s));
    vs.push_back(ver(s));
  }
  views.push_back(nsv_semver_t{});
  vs.emplace_back();

  for (size_t i = 0; i < vs.size(); ++i) {
    for (size_t j = 0; j < vs.size(); ++j) {
      INFO(str(vs[i]), " against ", str(vs[j]));
      SemVer const &a = vs[i], &b = vs[j];
      int const c = nsv_cmp(&views[i], &views[j]);
      REQUIRE(nsv_cmp(&a.semver(), &b.semver()) == c);
      REQUIRE((a == b) == (c == NSV_CMP_EQ));
      REQUIRE((a != b) == (c != NSV_CMP_EQ));
      REQUIRE((a < b) == (c == NSV_CMP_LT));
      REQUIRE((a <= b) == (c != NSV_CMP_GT));
      REQUIRE((a > b) == (c == NSV_CMP_GT));
      REQUIRE((a >= b) == (c != NSV_CMP_LT));
    }
  }
}

TEST_CASE("nsv::SemVer: comparisons on generated versions") {
  nsv_fuzz::rng r(0x6e737668707021ull);
  std::vector<SemVer> vs;
  std::vector<std::string> strs;
  for (int i = 0; i < 2000; ++i) {  // about a fifth are versions
    std::string const s = nsv_fuzz::gen_version(r);
    SemVer v;
    if (v.parse(s.data(), s.size()) == NSV_RET_OK) {
      vs.push_back(static_cast<SemVer &&>(v));
      strs.push_back(s);
    }
  }
  REQUIRE(vs.size() > 300);
  for (size_t i = 0; i < vs.size(); ++i) {
    for (size_t j = i; j < vs.size(); ++j) {
      INFO(strs[i], " against ", strs[j]);
      int const c = nsv_test::cmp(parse_ok(strs[i]), parse_ok(strs[j])) + 1;
      SemVer const &a = vs[i], &b = vs[j];
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
  SemVer const a = ver("1.0.0+a"), b = ver("1.0.0+b");
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
      SemVer const v = ver(vs);
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
  SemVer const v = ver("1.5.0");
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
  SemVer const v = ver("1.5.0");
  char const r[] = ">=1.0.0 <2.0.0 junk";
  REQUIRE(v.matches(r, 14) == 1);
  REQUIRE(v.matches(r, 15) == 1);
  REQUIRE(v.matches(r) == NSV_RET_ERR_SYNTAX);
  char const nul[] = ">=1.0.0 <2.0.0\0junk";
  REQUIRE(v.matches(nul) == 1);
  REQUIRE(v.matches(nul, sizeof(nul) - 1) == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("nsv::SemVer: matches on 0.0.0") {
  SemVer const v;
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
    SemVer const v = ver(s);
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
    SemVer const w = ver(str(v));
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
    SemVer const v = ver(s);
    REQUIRE(nsv_copy(nullptr, 0, &v.semver()) == nsv_test::k_int_max);
    int const want = nsv_test::fits_int(s.size()) ? static_cast<int>(s.size())
                                                  : NSV_RET_ERR_TOO_LONG;
    REQUIRE(v.format(nullptr, 0) == want);
    std::string buf(s.size() + 1, '\x7f');
    REQUIRE(v.format(&buf[0], buf.size()) == want);
  }
}

TEST_CASE("nsv::SemVer: accessors") {
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    same(ver(s), parse_ok(s));
  }

  SemVer const max = ver("4294967295.4294967295.4294967295");
  REQUIRE(max.major() == UINT32_MAX);
  REQUIRE(max.minor() == UINT32_MAX);
  REQUIRE(max.patch() == UINT32_MAX);

  SemVer const v = ver("10.20.30-rc.1+build.7");
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
    SemVer const w = ver(p.s);
    REQUIRE(w.pre() != nullptr);
    REQUIRE(std::strcmp(w.pre(), p.pre) == 0);
    REQUIRE(w.build() != nullptr);
    REQUIRE(std::strcmp(w.build(), p.build) == 0);
  }
}

TEST_CASE("nsv::SemVer: README example") {
  // README.md's C++ example, line for line.
  nsv::SemVer v, w;  // 0.0.0, allocating nothing
  if (v.parse("1.4.0-rc.2+g3f9a1c2") != NSV_RET_OK) { /* not a version: v is unchanged */ }
  w.parse("1.4.0");
  bool const older = v < w;  // true: a pre-release ranks below its release
  int const in = v.matches(">=1.2.0 <2.0.0 || ^3.1.0");  // 1 if so, 0 if not, < 0 on error
  char buf[32];
  int const len = v.format(buf, sizeof buf);  // 19: "1.4.0-rc.2+g3f9a1c2"
  char const *pre = v.pre(), *build = v.build();  // "rc.2" and "g3f9a1c2"

  REQUIRE(str(v) == "1.4.0-rc.2+g3f9a1c2");
  REQUIRE(str(w) == "1.4.0");
  REQUIRE(older);
  REQUIRE(in == 1);
  REQUIRE(len == 19);
  REQUIRE(std::string(buf) == "1.4.0-rc.2+g3f9a1c2");
  REQUIRE(std::string(pre) == "rc.2");
  REQUIRE(std::string(build) == "g3f9a1c2");
}

TEST_CASE("nsv::SemVer: semver() for the C API") {
  std::string const s = "1.2.3-rc.1+b.2";
  SemVer const v = ver(s);
  nsv_semver_t const view = parse_ok(s);
  nsv_semver_t const &c = v.semver();
  REQUIRE(c.src == nullptr);
  REQUIRE(nsv_copy(nullptr, 0, &c) == nsv_copy(nullptr, 0, &view));
  REQUIRE(nsv_test::format(c) == s);
  REQUIRE(nsv_range_match("^1.2.3-rc.0", 11, &c, NSV_RANGE_EXCLUDE_PRE) == 1);
  nsv_test::blob const blob = nsv_test::copy(c);
  REQUIRE(nsv_test::format(*blob) == s);
  REQUIRE(SemVer(*blob) == v);
}
