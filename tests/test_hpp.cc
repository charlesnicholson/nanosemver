// nsv::SemVer and nsv::SemVerView, from nanosemver.hpp, against the C API they wrap.
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
using nsv::SemVerInplace;
using nsv::SemVerView;
using nsv_test::parse_ok;

namespace {

using V = SemVer;
// Room for every version the tests parse but the very long: the corpus, the range cases
// and the generated versions need at most 114 bytes.
using Wide = SemVerInplace<256>;
// Room for any copy nsv_copy makes: a 65534-byte pre-release and two NULs. 64 KiB, so
// only ever on the heap.
using Huge = SemVerInplace<65536>;

// What the classes promise about themselves.
static_assert(std::is_same<V, SemVerInplace<64>>::value, "SemVer is SemVerInplace<64>");
static_assert(sizeof(V) == sizeof(nsv_semver_t) + 64, "a header and its array, no more");
static_assert(sizeof(Wide) == sizeof(nsv_semver_t) + 256, "");
static_assert(sizeof(SemVerInplace<2>) == sizeof(SemVerInplace<alignof(nsv_semver_t)>),
              "tail padding");
static_assert(alignof(SemVerInplace<13>) == alignof(nsv_semver_t), "");
static_assert(sizeof(SemVerView) == sizeof(nsv_semver_t), "one header");

template <class T>
constexpr bool plain() {
  return std::is_trivially_copyable<T>::value && std::is_trivially_destructible<T>::value &&
         std::is_standard_layout<T>::value && std::is_nothrow_default_constructible<T>::value;
}
static_assert(plain<V>() && plain<SemVerInplace<2>>() && plain<SemVerView>(), "plain bytes");

// Made only by from and from_prefix, which report errors; never converted.
static_assert(!std::is_constructible<V, char const *>::value, "");
static_assert(!std::is_constructible<V, nsv_semver_t const &>::value, "");
static_assert(!std::is_convertible<nsv_semver_t const &, V>::value, "");
static_assert(!std::is_constructible<SemVerInplace<8>, V const &>::value, "");
static_assert(!std::is_assignable<SemVerInplace<8> &, V const &>::value, "");
static_assert(!std::is_constructible<SemVerView, V const &>::value, "");
static_assert(!std::is_constructible<V, SemVerView const &>::value, "");

template <class T>
constexpr bool all_noexcept() {
  return noexcept(T::from("", 0, std::declval<int &>())) &&
         noexcept(T::from("", std::declval<int &>())) &&
         noexcept(T::from_prefix("", 0, std::declval<size_t &>(), std::declval<int &>())) &&
         noexcept(T::from(nsv_semver_t{}, std::declval<int &>())) &&
         noexcept(std::declval<T const &>().pre(std::declval<char const *&>())) &&
         noexcept(std::declval<T const &>().format(nullptr, 0)) &&
         noexcept(std::declval<T const &>().matches("", 0)) &&
         noexcept(std::declval<T const &>() < std::declval<V const &>());
}
static_assert(all_noexcept<V>() && all_noexcept<SemVerView>(), "nothing throws");

// Constant-initialized: a global version costs nothing at startup.
constinit V g_version;
constinit SemVerInplace<2> g_small;
constinit SemVerView g_view;

// The bytes of a copy's strings: its pre-release and build, and a NUL after each. A
// SemVerInplace<N> holds c when this is at most N.
size_t need(nsv_semver_t const &c) { return c.pre_len + 1u + c.build_len + 1u; }

// What a SemVerInplace<N> makes of what the C API parses as c with rc: rc's error, or the error
// for c not fitting, or OK.
template <size_t N>
int expect(int rc, nsv_semver_t const &c) {
  return (rc != NSV_RET_OK) ? rc : (need(c) <= N) ? NSV_RET_OK : NSV_RET_ERR_BUF_TOO_SMALL;
}

// v as a string, through its format.
template <class T>
std::string str(T const &v) {
  int const n = v.format(nullptr, 0);
  REQUIRE(n >= 0);
  std::string s(static_cast<size_t>(n) + 1, '\x7f');
  REQUIRE(v.format(&s[0], s.size()) == n);
  REQUIRE(s[static_cast<size_t>(n)] == '\0');
  s.resize(static_cast<size_t>(n));
  return s;
}

// s, made into a T, or a failed test. A view views s, which must outlive it.
template <class T = V>
T ver(std::string const &s) {
  INFO("making \"", s, "\"");
  int rc = 1;
  T const v = T::from(s.data(), s.size(), rc);
  REQUIRE(rc == NSV_RET_OK);
  return v;
}

// v's pre-release and build, through pre(p) and build(p).
template <class T>
std::string pre(T const &v) {
  char const *p = nullptr;
  int const n = v.pre(p);
  REQUIRE(n >= 0);
  REQUIRE(p != nullptr);
  return std::string(p, static_cast<size_t>(n));
}
template <class T>
std::string build(T const &v) {
  char const *p = nullptr;
  int const n = v.build(p);
  REQUIRE(n >= 0);
  REQUIRE(p != nullptr);
  return std::string(p, static_cast<size_t>(n));
}

// 0.0.0 and nothing more: a default version, and what from leaves on an error.
template <class T>
bool zero(T const &v) {
  nsv_semver_t const &h = v.semver();
  return !h.major && !h.minor && !h.patch && !h.src && !h.pre_ofs && !h.pre_len &&
         !h.build_ofs && !h.build_len;
}

// Whether n bytes from p lie within v.
template <class T>
bool within(T const &v, void const *p, size_t n) {
  char const *const lo = reinterpret_cast<char const *>(&v);
  char const *const q = static_cast<char const *>(p);
  std::less_equal<char const *> const le{};
  return le(lo, q) && le(q + n, lo + sizeof v);
}

// Every part of v agrees with c, a view, literal or copy, and its strings are its own:
// a copy's header first, then each string, NUL-terminated where its length ends.
template <size_t N>
void same(SemVerInplace<N> const &v, nsv_semver_t const &c) {
  REQUIRE(need(c) <= N);
  REQUIRE(v.major() == c.major);
  REQUIRE(v.minor() == c.minor);
  REQUIRE(v.patch() == c.patch);
  REQUIRE(pre(v) == nsv_test::pre(c));
  REQUIRE(build(v) == nsv_test::build(c));
  REQUIRE(str(v) == nsv_test::format(c));
  REQUIRE(nsv_cmp(&v.semver(), &c) == NSV_CMP_EQ);
  REQUIRE(nsv_cmp(&c, &v.semver()) == NSV_CMP_EQ);
  REQUIRE(v.semver().src == nullptr);
  REQUIRE(static_cast<void const *>(&v.semver()) == static_cast<void const *>(&v));
  char const *p = nullptr;
  size_t const pn = static_cast<size_t>(v.pre(p));
  REQUIRE(p[pn] == '\0');
  REQUIRE(((pn == 0) || within(v, p, pn + 1)));
  size_t const bn = static_cast<size_t>(v.build(p));
  REQUIRE(p[bn] == '\0');
  REQUIRE(((bn == 0) || within(v, p, bn + 1)));
}

// Every part of w agrees with c, a view of the same string: it views just what c does.
void same_view(SemVerView const &w, nsv_semver_t const &c) {
  nsv_semver_t const &h = w.semver();
  REQUIRE(h.src == c.src);
  REQUIRE(h.pre_ofs == c.pre_ofs);
  REQUIRE(h.pre_len == c.pre_len);
  REQUIRE(h.build_ofs == c.build_ofs);
  REQUIRE(h.build_len == c.build_len);
  REQUIRE(w.major() == c.major);
  REQUIRE(w.minor() == c.minor);
  REQUIRE(w.patch() == c.patch);
  REQUIRE(pre(w) == nsv_test::pre(c));
  REQUIRE(build(w) == nsv_test::build(c));
  REQUIRE(str(w) == nsv_test::format(c));
  REQUIRE(nsv_cmp(&h, &c) == NSV_CMP_EQ);
}

// Places s and its NUL so the NUL is the last byte before a guard page.
char const *place_nul(nsv_test::guarded &g, std::string const &s) {
  return g.place(std::string_view(s.c_str(), s.size() + 1));
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

template <size_t N>
void append(std::vector<std::string> &out, char const *const (&list)[N]) {
  out.insert(out.end(), std::begin(list), std::end(list));
}

// Every corpus string, valid or not, and each valid one with something after it.
std::vector<std::string> corpus_inputs() {
  std::vector<std::string> inputs;
  append(inputs, corpus::k_valid);
  append(inputs, corpus::k_invalid);
  append(inputs, corpus::k_overflow);
  for (char const *p : corpus::k_valid) {
    for (char const *tail : { " ", ", 2.0.0", " || 1", ".4", "x", "-", "+", "..", "-+" }) {
      inputs.push_back(std::string(p) + tail);
    }
  }
  return inputs;
}

// semver.org's precedence example, ascending.
char const *const k_semver_org[] = {
  "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0",
};

template <size_t N>
void default_is_zero() {
  INFO("SemVerInplace<", N, ">");
  SemVerInplace<N> const v;
  REQUIRE(zero(v));
  same(v, nsv_semver_t{});
  REQUIRE(str(v) == "0.0.0");
  REQUIRE(v == ver("0.0.0+b"));
  REQUIRE(v > ver("0.0.0-0"));
  REQUIRE(v < ver("0.0.1"));
}

// SemVerInplace<N>::from and from_prefix on every corpus input, against nsv_parse and
// nsv_parse_prefix: the version, or 0.0.0 and the error.
template <size_t N>
void factories() {
  INFO("SemVerInplace<", N, ">");
  static nsv_test::guarded g, gn;
  for (std::string const &s : corpus_inputs()) {
    INFO("\"", s, "\"");
    char const *const at = g.place(s);
    char const *const nul = place_nul(gn, s);
    nsv_semver_t c{};
    int const want = expect<N>(nsv_parse(&c, at, s.size()), c);
    for (int with_len = 0; with_len < 2; ++with_len) {
      int rc = 1;
      SemVerInplace<N> const v =
          with_len ? SemVerInplace<N>::from(at, s.size(), rc) : SemVerInplace<N>::from(nul, rc);
      REQUIRE(rc == want);
      if (want == NSV_RET_OK) {
        same(v, c);
        REQUIRE(str(v) == s);
      } else {
        REQUIRE(zero(v));
      }
    }
    nsv_semver_t pc{};
    size_t cn = 12345, n = 54321;
    int const pwant = expect<N>(nsv_parse_prefix(&pc, at, s.size(), &cn), pc);
    int rc = 1;
    SemVerInplace<N> const p = SemVerInplace<N>::from_prefix(at, s.size(), n, rc);
    REQUIRE(rc == pwant);
    REQUIRE(n == cn);  // spanning the version, even one that does not fit
    if (pwant == NSV_RET_OK) {
      same(p, pc);
    } else {
      REQUIRE(zero(p));
    }
  }
}

// Every shape of version whose strings need N - 1, N or N + 1 bytes, and every one in
// between that a short N reaches: whether it fits, through every way in.
template <size_t N>
void capacity() {
  INFO("SemVerInplace<", N, ">");
  for (size_t p = 0; p <= N; ++p) {
    for (size_t b = 0; b <= N; ++b) {
      size_t const strings = p + b + 2;
      if ((N > 16) && ((strings + 1 < N) || (strings > N + 1))) {
        continue;
      }
      std::string const s = shaped("1.2.3", p, b), tail = s + " rest";
      INFO(s);
      nsv_semver_t const view = parse_ok(s);
      REQUIRE(need(view) == strings);
      int const want = (strings <= N) ? NSV_RET_OK : NSV_RET_ERR_BUF_TOO_SMALL;
      int rc[4] = { 1, 1, 1, 1 };
      size_t n = 0;
      SemVerInplace<N> const made[] = {
        SemVerInplace<N>::from(s.data(), s.size(), rc[0]),
        SemVerInplace<N>::from(s.c_str(), rc[1]),
        SemVerInplace<N>::from_prefix(tail.data(), tail.size(), n, rc[2]),
        SemVerInplace<N>::from(view, rc[3]),
      };
      REQUIRE(n == s.size());
      for (size_t i = 0; i < 4; ++i) {
        INFO("way ", i);
        REQUIRE(rc[i] == want);
        if (want == NSV_RET_OK) {
          same(made[i], view);
        } else {
          REQUIRE(zero(made[i]));
        }
      }
    }
  }
}

template <size_t... I>
void capacity_from_2(std::index_sequence<I...>) {
  (capacity<I + 2>(), ...);
}

// Every version of up to N + 4 characters fits, whatever its shape, and N + 5 need not:
// the shortest that does not is a bare pre-release or build of N - 1 bytes.
template <size_t N>
void fits_n_plus_4() {
  INFO("SemVerInplace<", N, ">");
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
        REQUIRE(str(ver<SemVerInplace<N>>(s)) == s);
      }
    }
  }
  for (std::string const &s : { shaped("1.2.3", N - 1, 0), shaped("1.2.3", 0, N - 1) }) {
    INFO(s);
    REQUIRE(s.size() == N + 5);
    int rc = 1;
    REQUIRE(zero(SemVerInplace<N>::from(s.c_str(), rc)));
    REQUIRE(rc == NSV_RET_ERR_BUF_TOO_SMALL);
  }
}

// T::from(s, len) fails with want, and leaves 0.0.0.
template <class T>
void refused(int want, char const *s, size_t len) {
  INFO("\"", (s ? std::string(s, std::min<size_t>(len, 40)) : "(null)"), "\"");
  int rc = 1;
  REQUIRE(zero(T::from(s, len, rc)));
  REQUIRE(rc == want);
}

}  // namespace

TEST_CASE("nsv::SemVer: a header and its array, in place") {
  V const v = ver("1.2.3-rc.1+b.7");
  char const *const base = reinterpret_cast<char const *>(&v);
  char const *p = nullptr;
  REQUIRE(v.pre(p) == 4);
  REQUIRE(p == base + sizeof(nsv_semver_t));
  REQUIRE(v.build(p) == 3);
  REQUIRE(p == base + sizeof(nsv_semver_t) + 5);
  REQUIRE(std::memcmp(base + sizeof(nsv_semver_t), "rc.1\0b.7", 9) == 0);
}

TEST_CASE("nsv::SemVer: default is 0.0.0") {
  default_is_zero<2>();
  default_is_zero<13>();
  default_is_zero<64>();
  default_is_zero<256>();
  REQUIRE(zero(g_version));
  REQUIRE(zero(g_small));
  SemVerView const w;
  REQUIRE(zero(w));
  REQUIRE(zero(g_view));
  REQUIRE(str(w) == "0.0.0");
  REQUIRE(pre(w).empty());
  REQUIRE(w == V());
}

TEST_CASE("nsv::SemVer: from and from_prefix, against nsv_parse and nsv_parse_prefix") {
  factories<2>();
  factories<8>();
  factories<13>();
  factories<48>();
  factories<64>();
  factories<256>();
}

TEST_CASE("nsv::SemVer: capacity, at every size from 2 to 16, and around 48, 64 and 256") {
  capacity_from_2(std::make_index_sequence<15>());  // 13 has tail padding, which nothing may use
  capacity<48>();
  capacity<64>();
  capacity<256>();
}

TEST_CASE("nsv::SemVer: every version of up to N + 4 characters fits") {
  fits_n_plus_4<2>();
  fits_n_plus_4<3>();
  fits_n_plus_4<8>();
  fits_n_plus_4<13>();
  fits_n_plus_4<64>();
}

TEST_CASE("nsv::SemVer: errors, syntax first, then nsv_parse's limits, then the fit") {
  refused<V>(NSV_RET_ERR_ARG, nullptr, 5);
  refused<V>(NSV_RET_ERR_ARG, nullptr, 0);
  int rc = 1;
  REQUIRE(zero(V::from(nullptr, rc)));
  REQUIRE(rc == NSV_RET_ERR_ARG);
  size_t n = 7;
  REQUIRE(zero(V::from_prefix(nullptr, 5, n, rc)));
  REQUIRE(rc == NSV_RET_ERR_ARG);
  REQUIRE(n == 7);
  refused<V>(NSV_RET_ERR_SYNTAX, "", 0);
  refused<V>(NSV_RET_ERR_SYNTAX, "v1.2.3", 6);
  refused<V>(NSV_RET_ERR_SYNTAX, "1.2.3", 4);
  refused<V>(NSV_RET_ERR_OVERFLOW, "4294967296.0.0", 14);
  std::string const big = "1.0.0-" + std::string(63, 'p');
  refused<V>(NSV_RET_ERR_BUF_TOO_SMALL, big.data(), big.size());
  std::string const bad = big + "!";
  refused<V>(NSV_RET_ERR_SYNTAX, bad.data(), bad.size());
  std::string const over = "4294967296" + big.substr(1);
  refused<V>(NSV_RET_ERR_OVERFLOW, over.data(), over.size());
  std::string const huge = "1.0.0-" + std::string(65530, 'p');  // 65536 bytes
  refused<V>(NSV_RET_ERR_TOO_LONG, huge.data(), huge.size());
  refused<SemVerView>(NSV_RET_ERR_TOO_LONG, huge.data(), huge.size());
  n = 0;
  std::string const rest = "1.2.3-" + std::string(63, 'p') + " rest";
  REQUIRE(zero(V::from_prefix(rest.data(), rest.size(), n, rc)));
  REQUIRE(rc == NSV_RET_ERR_BUF_TOO_SMALL);
  REQUIRE(n == rest.size() - 5);  // spanning the version that does not fit
}

TEST_CASE("nsv::SemVer: from reads only what it is given") {
  int rc = 1;
  REQUIRE(str(V::from("1.2.3-rc.1junk", 10, rc)) == "1.2.3-rc.1");
  REQUIRE(rc == NSV_RET_OK);
  char const nul[] = "2.0.0\0-junk";
  REQUIRE(str(V::from(nul, rc)) == "2.0.0");
  REQUIRE(rc == NSV_RET_OK);
  REQUIRE(zero(V::from(nul, sizeof(nul) - 1, rc)));
  REQUIRE(rc == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("nsv::SemVer: a copy outlives the string it came from") {
  std::string const s = "3.2.1-beta.4+sha.abc";
  char *const buf = static_cast<char *>(std::malloc(s.size() + 1));
  REQUIRE(buf != nullptr);
  std::memcpy(buf, s.c_str(), s.size() + 1);
  int rc[4] = { 1, 1, 1, 1 };
  size_t n = 0;
  nsv_semver_t view{};
  REQUIRE(nsv_parse(&view, buf, s.size()) == NSV_RET_OK);
  V const made[] = { V::from(buf, s.size(), rc[0]), V::from(buf, rc[1]),
                     V::from_prefix(buf, s.size(), n, rc[2]), V::from(view, rc[3]) };
  std::memset(buf, 'X', s.size());
  std::free(buf);
  for (size_t i = 0; i < 4; ++i) {
    REQUIRE(rc[i] == NSV_RET_OK);
    REQUIRE(str(made[i]) == s);
    REQUIRE(pre(made[i]) == "beta.4");
    REQUIRE(build(made[i]) == "sha.abc");
  }
}

TEST_CASE("nsv::SemVer: from an nsv_semver_t") {
  int rc = 1;
  SUBCASE("a view") {
    std::string const s = "1.2.3-rc.1+b.2";
    nsv_semver_t const view = parse_ok(s);
    same(V::from(view, rc), view);
    REQUIRE(rc == NSV_RET_OK);
  }

  SUBCASE("a copy, freed after") {
    std::string const s = "4.5.6-rc.7+meta.8";
    nsv_test::blob blob = nsv_test::copy(parse_ok(s));
    V const v = V::from(*blob, rc);
    REQUIRE(rc == NSV_RET_OK);
    std::memset(static_cast<void *>(blob.get()), 0xEE, sizeof(nsv_semver_t));
    blob.reset();
    REQUIRE(str(v) == s);
  }

  SUBCASE("literals") {
    static nsv_semver_t const full = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
    static nsv_semver_t const bare = NSV_SEMVER_LIT(5, 6, 7, "", "");
    same(V::from(full, rc), full);
    same(SemVerInplace<2>::from(bare, rc), bare);
    same(SemVerInplace<14>::from(full, rc), full);
    REQUIRE(zero(SemVerInplace<13>::from(full, rc)));
    REQUIRE(rc == NSV_RET_ERR_BUF_TOO_SMALL);
  }

  SUBCASE("another version, of any size, and zero") {
    Wide const wide = ver<Wide>("1.2.3-" + std::string(100, 'p') + "+b");  // needs 103
    SemVerInplace<103> const fit = SemVerInplace<103>::from(wide.semver(), rc);
    REQUIRE(rc == NSV_RET_OK);
    same(fit, wide.semver());
    REQUIRE(zero(SemVerInplace<102>::from(wide.semver(), rc)));
    REQUIRE(rc == NSV_RET_ERR_BUF_TOO_SMALL);
    same(Wide::from(fit.semver(), rc), fit.semver());
    same(V::from(nsv_semver_t{}, rc), nsv_semver_t{});
  }
}

TEST_CASE("nsv::SemVer: from a version's own strings, into a new one") {
  // Where nsv_copy, straight into v, would copy the new pre-release over itself: the
  // source is v's own, and from always fills a fresh version, so it never is.
  V v = ver("1.0.0-2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbb+cccccccc");
  char const *p = nullptr;
  int rc = 1;
  int const n = v.pre(p);
  v = V::from(p, static_cast<size_t>(n), rc);
  REQUIRE(rc == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbb");
  v = ver("1.0.0+2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbb");
  v.build(p);
  v = V::from(p, rc);  // NUL-terminated, as a SemVer's strings are
  REQUIRE(rc == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbb");
  v = V::from(v.semver(), rc);
  REQUIRE(rc == NSV_RET_OK);
  REQUIRE(str(v) == "2.0.0-aaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbb");
}

TEST_CASE("nsv::SemVer: what nsv_copy refuses, before whether it fits") {
  int rc = 1;
  SUBCASE("a 65535-byte pre-release") {
    std::string const p(65535, 'p');
    nsv_semver_t big{};
    big.src = p.data();
    big.pre_len = 65535;
    REQUIRE(zero(V::from(big, rc)));
    REQUIRE(rc == NSV_RET_ERR_TOO_LONG);
    std::unique_ptr<Huge> const huge(new Huge(Huge::from(big, rc)));
    REQUIRE(rc == NSV_RET_ERR_TOO_LONG);
    REQUIRE(zero(*huge));
  }

  SUBCASE("a 65534-byte pre-release, which only 65536 bytes hold, but for a 16-bit int") {
    std::string const p(65534, 'p');
    nsv_semver_t big{};
    big.src = p.data();
    big.pre_len = 65534;
    REQUIRE(need(big) == 65536);
    bool const fits = nsv_test::fits_int(sizeof(nsv_semver_t) + 65536);
    std::unique_ptr<Huge> const huge(new Huge(Huge::from(big, rc)));
    REQUIRE(rc == (fits ? NSV_RET_OK : NSV_RET_ERR_TOO_LONG));
    if (fits) {
      REQUIRE(pre(*huge) == p);
    }
    std::unique_ptr<SemVerInplace<65535>> const shorter(
        new SemVerInplace<65535>(SemVerInplace<65535>::from(big, rc)));
    REQUIRE(rc == (fits ? NSV_RET_ERR_BUF_TOO_SMALL : NSV_RET_ERR_TOO_LONG));
    REQUIRE(zero(*shorter));
    REQUIRE(zero(V::from(big, rc)));
    REQUIRE(rc == (fits ? NSV_RET_ERR_BUF_TOO_SMALL : NSV_RET_ERR_TOO_LONG));
  }

  SUBCASE("the longest parseable version, but for a 16-bit int") {
    std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
    REQUIRE(s.size() == 65535);
    bool const fits = nsv_test::fits_int(sizeof(nsv_semver_t) + 30000 + 35528 + 2);
    std::unique_ptr<Huge> const huge(new Huge(Huge::from(s.data(), s.size(), rc)));
    REQUIRE(rc == (fits ? NSV_RET_OK : NSV_RET_ERR_TOO_LONG));
    if (fits) {
      REQUIRE(str(*huge) == s);
    }
    REQUIRE(zero(V::from(s.data(), s.size(), rc)));
    REQUIRE(rc == (fits ? NSV_RET_ERR_BUF_TOO_SMALL : NSV_RET_ERR_TOO_LONG));
  }
}

TEST_CASE("nsv::SemVer: copies and moves copy the bytes") {
  V const a = ver("1.2.3-rc.1+b.7");
  V b(a);
  REQUIRE(str(b) == "1.2.3-rc.1+b.7");
  char const *pa = nullptr, *pb = nullptr;
  a.pre(pa);
  b.pre(pb);
  REQUIRE(pb != pa);  // its own strings
  REQUIRE(within(b, pb, 5));

  V c;
  {
    V const src = ver("4.0.0-x+y");
    c = src;
  }
  REQUIRE(str(c) == "4.0.0-x+y");  // a copy outlives its source
  b = c;
  REQUIRE(str(b) == "4.0.0-x+y");
  b = V();
  REQUIRE(zero(b));
  V &rc = c;
  c = rc;  // through a reference, so the compiler does not object
  REQUIRE(str(c) == "4.0.0-x+y");

  V d(static_cast<V &&>(c));
  REQUIRE(str(d) == "4.0.0-x+y");
  REQUIRE(str(c) == "4.0.0-x+y");  // a move is a copy
  std::swap(b, d);
  REQUIRE(str(b) == "4.0.0-x+y");
  REQUIRE(zero(d));

  V z;
  std::memcpy(static_cast<void *>(&z), &a, sizeof a);  // plain bytes
  REQUIRE(str(z) == "1.2.3-rc.1+b.7");
  z.pre(pb);
  REQUIRE(within(z, pb, 5));
}

TEST_CASE("nsv::SemVer: in a growing vector, and sorted") {
  std::vector<V> vs;
  for (unsigned i = 0; i < 100; ++i) {
    vs.push_back(ver(std::to_string(i) + ".0.0-rc." + std::to_string(i)));
  }
  for (unsigned i = 0; i < 100; ++i) {
    REQUIRE(str(vs[i]) == std::to_string(i) + ".0.0-rc." + std::to_string(i));
    char const *p = nullptr;
    size_t const n = static_cast<size_t>(vs[i].pre(p));
    REQUIRE(within(vs[i], p, n + 1));
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

TEST_CASE("nsv::SemVer: comparisons agree with nsv_cmp, across sizes and views") {
  std::vector<std::string> strs = { "0.0.0-0", "0.0.0", "0.0.0+b", "0.0.1", "1.0.0-0",
                                    "1.0.0-alpha+x", "1.0.0-alpha+y", "1.0.0-rc.1+build.1",
                                    "1.0.0+a", "1.0.0+b", "1.0.1", "1.9.0", "1.10.0",
                                    "2.0.0-rc.1", "4294967295.4294967295.4294967295" };
  append(strs, corpus::k_valid);
  append(strs, k_semver_org);
  std::vector<nsv_semver_t> views;
  std::vector<Wide> ws;
  std::vector<V> vs;  // the corpus fits in 64 bytes
  std::vector<SemVerView> svs;
  for (std::string const &s : strs) {
    views.push_back(parse_ok(s));
    ws.push_back(ver<Wide>(s));
    vs.push_back(ver(s));
    svs.push_back(ver<SemVerView>(s));
  }
  views.push_back(nsv_semver_t{});
  ws.emplace_back();
  vs.emplace_back();
  svs.emplace_back();

  auto const agree = [](auto const &a, auto const &b, int c) {
    REQUIRE((a == b) == (c == NSV_CMP_EQ));
    REQUIRE((a != b) == (c != NSV_CMP_EQ));
    REQUIRE((a < b) == (c == NSV_CMP_LT));
    REQUIRE((a <= b) == (c != NSV_CMP_GT));
    REQUIRE((a > b) == (c == NSV_CMP_GT));
    REQUIRE((a >= b) == (c != NSV_CMP_LT));
  };
  for (size_t i = 0; i < ws.size(); ++i) {
    for (size_t j = 0; j < ws.size(); ++j) {
      INFO(str(ws[i]), " against ", str(ws[j]));
      int const c = nsv_cmp(&views[i], &views[j]);
      agree(ws[i], ws[j], c);
      agree(ws[i], vs[j], c);
      agree(vs[i], ws[j], c);
      agree(svs[i], svs[j], c);
      agree(svs[i], vs[j], c);
      agree(vs[i], svs[j], c);
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
    int const want = nsv_parse(&c, s.data(), s.size());
    int rc = 1;
    Wide const v = Wide::from(s.data(), s.size(), rc);
    REQUIRE(rc == expect<256>(want, c));
    V::from(s.data(), s.size(), rc);
    REQUIRE(rc == expect<64>(want, c));
    SemVerInplace<16>::from(s.data(), s.size(), rc);
    REQUIRE(rc == expect<16>(want, c));
    if (want == NSV_RET_OK) {
      vs.push_back(v);
      strs.push_back(s);
      too_big += (need(c) > 16);
    }
  }
  REQUIRE(vs.size() > 300);
  // So refusals are tested too, which at the default size are few, and how few depends on
  // the order the compiler calls the generator's arguments in.
  REQUIRE(too_big > 50);
  for (size_t i = 0; i < vs.size(); ++i) {
    for (size_t j = i; j < vs.size(); ++j) {
      INFO(strs[i], " against ", strs[j]);
      int const c = nsv_test::cmp(parse_ok(strs[i]), parse_ok(strs[j])) + 1;
      Wide const &a = vs[i], &b = vs[j];
      REQUIRE((a == b) == (c == NSV_CMP_EQ));
      REQUIRE((a < b) == (c == NSV_CMP_LT));
      REQUIRE((a > b) == (c == NSV_CMP_GT));
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
  REQUIRE(a <= b);
  REQUIRE(a >= b);
  REQUIRE(str(a) != str(b));
  REQUIRE(ver("1.0.0-rc.1") < ver("1.0.0"));
  REQUIRE(ver("1.0.0-beta.2") < ver("1.0.0-beta.11"));
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
      size_t const n = range.size();
      V const v = ver(vs);
      SemVerView const w = ver<SemVerView>(vs);
      REQUIRE(v.matches(g.place(range), n) == c.plain);
      REQUIRE(v.matches(g.place(range), n, NSV_RANGE_EXCLUDE_PRE) == c.exclude_pre);
      REQUIRE(v.matches(place_nul(g, range)) == c.plain);
      REQUIRE(w.matches(g.place(range), n) == c.plain);
      REQUIRE(w.matches(g.place(range), n, NSV_RANGE_EXCLUDE_PRE) == c.exclude_pre);
      REQUIRE(w.matches(place_nul(g, range)) == c.plain);
    }
  }
}

TEST_CASE("nsv::SemVer: matches errors, and what it reads") {
  V const v = ver("1.5.0");
  for (char const *r : { ">>>bad", "1.2.3.4", "alpha", ">=1.0.0 alpha", "1.0.0 || beta" }) {
    INFO("\"", r, "\"");
    REQUIRE(v.matches(r) == NSV_RET_ERR_SYNTAX);
    REQUIRE(v.matches(r, std::strlen(r), NSV_RANGE_EXCLUDE_PRE) == NSV_RET_ERR_SYNTAX);
  }
  REQUIRE(v.matches(nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(v.matches(nullptr, 5, NSV_RANGE_EXCLUDE_PRE) == NSV_RET_ERR_ARG);
  REQUIRE(v.matches("<4294967296.0.0") == NSV_RET_ERR_OVERFLOW);
  std::string const huge = "=1.0.0-" + std::string(65530, 'p');
  REQUIRE(v.matches(huge.c_str()) == NSV_RET_ERR_TOO_LONG);
  char const r[] = ">=1.0.0 <2.0.0 junk";
  REQUIRE(v.matches(r, 14) == 1);
  REQUIRE(v.matches(r) == NSV_RET_ERR_SYNTAX);
  char const nul[] = ">=1.0.0 <2.0.0\0junk";
  REQUIRE(v.matches(nul) == 1);
  V const z;
  REQUIRE(z.matches("0.0.0") == 1);
  REQUIRE(z.matches(">0.0.0") == 0);
  REQUIRE(z.matches("^0.0.1") == 0);
}

TEST_CASE("nsv::SemVer: format") {
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    Wide const v = ver<Wide>(s);
    SemVerView const w = ver<SemVerView>(s);
    size_t const len = s.size();
    for (int which = 0; which < 2; ++which) {
      auto const format = [&](char *dst, size_t n) {
        return which ? w.format(dst, n) : v.format(dst, n);
      };
      REQUIRE(format(nullptr, 0) == static_cast<int>(len));
      std::string buf(len + 8, '\x7f');
      REQUIRE(format(&buf[0], len + 1) == static_cast<int>(len));
      REQUIRE(buf.compare(0, len, s) == 0);
      REQUIRE(buf[len] == '\0');
      buf.assign(len + 8, '\x7f');
      REQUIRE(format(&buf[0], len) == NSV_RET_ERR_BUF_TOO_SMALL);  // never truncated
      REQUIRE(buf[0] == '\0');
    }
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
    int rc = 1;
    std::unique_ptr<SemVerInplace<32768>> const v(
        new SemVerInplace<32768>(SemVerInplace<32768>::from(s.data(), s.size(), rc)));
    REQUIRE(rc == NSV_RET_OK);
    REQUIRE(nsv_copy(nullptr, 0, &v->semver()) == nsv_test::k_int_max);
    int const want = nsv_test::fits_int(s.size()) ? static_cast<int>(s.size())
                                                  : NSV_RET_ERR_TOO_LONG;
    REQUIRE(v->format(nullptr, 0) == want);
  }
}

TEST_CASE("nsv::SemVer: pre and build") {
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
    V const v = ver(p.s);
    REQUIRE(pre(v) == p.pre);
    REQUIRE(build(v) == p.build);
    std::string const s = p.s;
    SemVerView const w = ver<SemVerView>(s);
    REQUIRE(pre(w) == p.pre);
    REQUIRE(build(w) == p.build);
  }
  V const max = ver("4294967295.4294967295.4294967295");
  REQUIRE(max.major() == UINT32_MAX);
  REQUIRE(max.minor() == UINT32_MAX);
  REQUIRE(max.patch() == UINT32_MAX);
}

TEST_CASE("nsv::SemVerView: from and from_prefix, against nsv_parse and nsv_parse_prefix") {
  static nsv_test::guarded g, gn;
  for (std::string const &s : corpus_inputs()) {
    INFO("\"", s, "\"");
    char const *const at = g.place(s);
    char const *const nul = place_nul(gn, s);
    nsv_semver_t c{}, cnul{};
    int const want = nsv_parse(&c, at, s.size());
    REQUIRE(nsv_parse(&cnul, nul, s.size()) == want);
    int rc = 1;
    SemVerView const v = SemVerView::from(at, s.size(), rc);
    REQUIRE(rc == want);
    SemVerView const w = SemVerView::from(nul, rc);
    REQUIRE(rc == want);
    if (want == NSV_RET_OK) {
      same_view(v, c);
      same_view(w, cnul);
    } else {
      REQUIRE(zero(v));
      REQUIRE(zero(w));
    }
    nsv_semver_t pc{};
    size_t cn = 12345, n = 54321;
    int const pwant = nsv_parse_prefix(&pc, at, s.size(), &cn);
    SemVerView const p = SemVerView::from_prefix(at, s.size(), n, rc);
    REQUIRE(rc == pwant);
    REQUIRE(n == cn);
    if (pwant == NSV_RET_OK) {
      same_view(p, pc);
    } else {
      REQUIRE(zero(p));
    }
  }
  refused<SemVerView>(NSV_RET_ERR_ARG, nullptr, 5);
  int rc = 1;
  char const junk[] = "1.2.3-rc.1junk";
  SemVerView const v = SemVerView::from(junk, 10, rc);  // only what it is given
  REQUIRE(rc == NSV_RET_OK);
  REQUIRE(str(v) == "1.2.3-rc.1");
  REQUIRE(v.semver().src == junk);
}

TEST_CASE("nsv::SemVerView: no length cap") {
  // The longest version nsv_parse takes, which no SemVer holds, nor any SemVerInplace where int
  // is 16 bits.
  std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
  SemVerView const w = ver<SemVerView>(s);
  REQUIRE(w.matches(">=1.0.0-0 <1.0.0") == 1);
  REQUIRE(w > V());
  REQUIRE(w < ver("1.0.0"));
  if (nsv_test::fits_int(s.size())) {
    same_view(w, parse_ok(s));
    REQUIRE(str(w) == s);
  } else {  // the C API's int lengths, not the view's
    char const *p = nullptr;
    REQUIRE(w.pre(p) == 30000);
    REQUIRE(w.build(p) == NSV_RET_ERR_TOO_LONG);
    REQUIRE(w.format(nullptr, 0) == NSV_RET_ERR_TOO_LONG);
  }
}

TEST_CASE("nsv::SemVerView: pre and build are the string's own bytes") {
  char const s[] = "1.2.3-rc.1+b.7 and more";
  int rc = 1;
  SemVerView const w = SemVerView::from(s, 14, rc);
  REQUIRE(rc == NSV_RET_OK);
  char const *p = nullptr;
  REQUIRE(w.pre(p) == 4);
  REQUIRE(p == s + 6);  // followed by '+', not a NUL
  REQUIRE(w.build(p) == 3);
  REQUIRE(p == s + 11);  // followed by ' '
}

TEST_CASE("nsv::SemVerView: from an nsv_semver_t: a view or literal, never a copy") {
  int rc = 1;
  std::string const s = "1.2.3-rc.1+b.2";
  nsv_semver_t const view = parse_ok(s);
  same_view(SemVerView::from(view, rc), view);
  REQUIRE(rc == NSV_RET_OK);
  static nsv_semver_t const lit = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
  same_view(SemVerView::from(lit, rc), lit);
  REQUIRE(rc == NSV_RET_OK);
  REQUIRE(zero(SemVerView::from(nsv_semver_t{}, rc)));
  REQUIRE(rc == NSV_RET_OK);
  // A copy's header does not point at its strings: nothing to view.
  V const v = ver(s);
  REQUIRE(zero(SemVerView::from(v.semver(), rc)));
  REQUIRE(rc == NSV_RET_ERR_ARG);
  nsv_test::blob const blob = nsv_test::copy(view);
  REQUIRE(zero(SemVerView::from(*blob, rc)));
  REQUIRE(rc == NSV_RET_ERR_ARG);
  // But one without strings has nothing to lose.
  REQUIRE(str(SemVerView::from(ver("7.8.9").semver(), rc)) == "7.8.9");
  REQUIRE(rc == NSV_RET_OK);
}

TEST_CASE("nsv::SemVerView: copies view the same string, and a SemVer outlives it") {
  std::string s = "3.2.1-beta.4+sha.abc";
  SemVerView const a = ver<SemVerView>(s);
  SemVerView const b = a;
  REQUIRE(b.semver().src == s.c_str());
  int rc = 1;
  V const kept = V::from(b.semver(), rc);
  REQUIRE(rc == NSV_RET_OK);
  std::fill(s.begin(), s.end(), 'X');  // in place: assign could move the bytes
  REQUIRE(str(kept) == "3.2.1-beta.4+sha.abc");
  REQUIRE(b.semver().src == s.c_str());  // the view sees what its string now holds
  REQUIRE(pre(b) == std::string(6, 'X'));
}

TEST_CASE("nsv::SemVer: README examples") {
  {
    // README.md's SemVer example, line for line.
    int rc;
    nsv::SemVer const v = nsv::SemVer::from("1.4.0-rc.2+g3f9a1c2", rc);  // 64 B in place
    if (rc != NSV_RET_OK) { /* not a version: v is 0.0.0 */ }
    nsv::SemVer const w = nsv::SemVer::from("1.4.0", rc);
    bool const older = v < w;  // true: a pre-release ranks below its release
    int const in = v.matches(">=1.2.0 <2.0.0 || ^3.1.0");  // 1 if so, 0 if not, < 0 on error
    char buf[32];
    int const len = v.format(buf, sizeof buf);  // 19: "1.4.0-rc.2+g3f9a1c2"
    char const *pre;
    int const n = v.pre(pre);  // 4: "rc.2", NUL-terminated
    // 8 bytes: too few for these two strings and their NULs, so NSV_RET_ERR_BUF_TOO_SMALL
    nsv::SemVerInplace<8> const small = nsv::SemVerInplace<8>::from("1.4.0-rc.2+g3f9a1c2", rc);

    REQUIRE(str(v) == "1.4.0-rc.2+g3f9a1c2");
    REQUIRE(str(w) == "1.4.0");
    REQUIRE(older);
    REQUIRE(in == 1);
    REQUIRE(len == 19);
    REQUIRE(std::string(buf) == "1.4.0-rc.2+g3f9a1c2");
    REQUIRE(n == 4);
    REQUIRE(std::string(pre) == "rc.2");
    REQUIRE(rc == NSV_RET_ERR_BUF_TOO_SMALL);
    REQUIRE(zero(small));
  }
  {
    // README.md's SemVerView example, line for line.
    char const tag[] = "1.4.0-rc.2+g3f9a1c2";  // must outlive the view
    int rc;
    nsv::SemVerView const v = nsv::SemVerView::from(tag, rc);
    if (rc != NSV_RET_OK) { /* not a version: v is 0.0.0 */ }
    char const *pre;
    int const n = v.pre(pre);  // 4: "rc.2", then "+g3f9a1c2": tag's own bytes
    bool const newer = v > nsv::SemVer();  // true: views and SemVers compare
    nsv::SemVer const kept = nsv::SemVer::from(v.semver(), rc);  // a copy, to outlive tag

    REQUIRE(n == 4);
    REQUIRE(std::string(pre, 4) == "rc.2");
    REQUIRE(pre[n] == '+');
    REQUIRE(newer);
    REQUIRE(rc == NSV_RET_OK);
    REQUIRE(str(kept) == tag);
  }
}

TEST_CASE("nsv::SemVer: semver() for the C API") {
  std::string const s = "1.2.3-rc.1+b.2";
  V const v = ver(s);
  nsv_semver_t const view = parse_ok(s);
  nsv_semver_t const &c = v.semver();
  REQUIRE(nsv_copy(nullptr, 0, &c) == nsv_copy(nullptr, 0, &view));
  REQUIRE(nsv_test::format(c) == s);
  REQUIRE(nsv_range_match("^1.2.3-rc.0", 11, &c, NSV_RANGE_EXCLUDE_PRE) == 1);
  nsv_test::blob const blob = nsv_test::copy(c);
  int rc = 1;
  REQUIRE(V::from(*blob, rc) == v);
  REQUIRE(rc == NSV_RET_OK);
}
