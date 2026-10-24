#include "nsv_test.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using nsv_test::parse_ok;

namespace {

size_t need_for(size_t pre_len, size_t build_len) {
  return sizeof(nsv_semver_t) + pre_len + 1 + build_len + 1;
}

// Storage aligned for nsv_semver_t, with room to shift the start off alignment.
struct arena {
  explicit arena(size_t n) : words((n + 2 * alignof(nsv_semver_t)) / sizeof(words[0]) + 1) {}
  unsigned char *at(size_t ofs) { return reinterpret_cast<unsigned char *>(words.data()) + ofs; }
  std::vector<std::max_align_t> words;
};

}  // namespace

TEST_CASE("nsv_copy: NULL source") {
  unsigned char buf[256];
  REQUIRE(nsv_copy(nullptr, 0, nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_copy(buf, sizeof(buf), nullptr) == NSV_RET_ERR_ARG);
}

TEST_CASE("nsv_copy: sizing call") {
  struct {
    char const *s;
    size_t pre, build;
  } const cases[] = {
    { "1.2.3", 0, 0 },
    { "1.2.3-rc.1", 4, 0 },
    { "1.2.3+b.5", 0, 3 },
    { "1.2.3-alpha+exp.sha.5114f85", 5, 15 },
  };
  for (auto const &c : cases) {
    INFO(c.s);
    std::string const s = c.s;
    nsv_semver_t const v = parse_ok(s);
    int const want = static_cast<int>(need_for(c.pre, c.build));
    REQUIRE(nsv_copy(nullptr, 0, &v) == want);
    REQUIRE(nsv_copy(nullptr, 12345, &v) == want);  // cap is ignored
  }
}

TEST_CASE("nsv_copy: layout of a copy") {
  std::string const s = "10.20.30-rc.1+build.7";
  nsv_semver_t const v = parse_ok(s);
  nsv_test::blob const c = nsv_test::copy(v);
  REQUIRE(c->major == 10);
  REQUIRE(c->minor == 20);
  REQUIRE(c->patch == 30);
  REQUIRE(c->src == nullptr);
  REQUIRE(c->pre_ofs == 0);
  REQUIRE(c->pre_len == 4);
  REQUIRE(c->build_ofs == 5);
  REQUIRE(c->build_len == 7);
  char const *const tail = reinterpret_cast<char const *>(c.get()) + sizeof(nsv_semver_t);
  REQUIRE(std::memcmp(tail, "rc.1\0build.7\0", 13) == 0);

  char const *p = nullptr;
  REQUIRE(nsv_get_pre(c.get(), &p) == 4);
  REQUIRE(p == tail);
  REQUIRE(nsv_get_build(c.get(), &p) == 7);
  REQUIRE(p == tail + 5);
  REQUIRE(nsv_test::format(*c) == s);
  REQUIRE(nsv_cmp(c.get(), &v) == NSV_CMP_EQ);
}

TEST_CASE("nsv_copy: absent strings are still terminated") {
  for (char const *p : { "1.2.3", "1.2.3-a", "1.2.3+b" }) {
    std::string const s = p;
    INFO(s);
    nsv_semver_t const v = parse_ok(s);
    nsv_test::blob const c = nsv_test::copy(v);
    char const *tail = reinterpret_cast<char const *>(c.get()) + sizeof(nsv_semver_t);
    REQUIRE(tail[c->pre_len] == '\0');
    REQUIRE(c->build_ofs == c->pre_len + 1);
    REQUIRE(tail[c->build_ofs + c->build_len] == '\0');
    REQUIRE(nsv_test::format(*c) == s);
  }
}

TEST_CASE("nsv_copy: independent of the source string") {
  std::string s = "3.2.1-beta.4+sha.abc";
  nsv_test::blob c;
  {
    nsv_semver_t const v = parse_ok(s);
    c = nsv_test::copy(v);
  }
  std::fill(s.begin(), s.end(), 'X');
  s = "garbage";
  REQUIRE(nsv_test::format(*c) == "3.2.1-beta.4+sha.abc");
  REQUIRE(nsv_test::pre(*c) == "beta.4");
  REQUIRE(nsv_test::build(*c) == "sha.abc");
}

TEST_CASE("nsv_copy: from a copy, a literal, and zero-init") {
  std::string const s = "1.0.0-x.y+z";
  nsv_test::blob const c1 = nsv_test::copy(parse_ok(s));
  nsv_test::blob const c2 = nsv_test::copy(*c1);
  REQUIRE(c2->src == nullptr);
  REQUIRE(nsv_test::format(*c2) == s);

  static nsv_semver_t const lit = NSV_SEMVER_LIT(9, 8, 7, "pre.1", "b");
  nsv_test::blob const cl = nsv_test::copy(lit);
  REQUIRE(nsv_test::format(*cl) == "9.8.7-pre.1+b");

  nsv_semver_t zero{};
  REQUIRE(nsv_copy(nullptr, 0, &zero) == static_cast<int>(need_for(0, 0)));
  nsv_test::blob const cz = nsv_test::copy(zero);
  REQUIRE(nsv_test::format(*cz) == "0.0.0");
}

TEST_CASE("nsv_copy: a copy is relocatable") {
  std::string const s = "4.5.6-rc.7+meta.8";
  nsv_semver_t const v = parse_ok(s);
  size_t const n = static_cast<size_t>(nsv_copy(nullptr, 0, &v));
  nsv_test::blob a = nsv_test::alloc_blob(n);
  REQUIRE(nsv_copy(a.get(), n, &v) == static_cast<int>(n));

  nsv_test::blob b = nsv_test::alloc_blob(n);
  std::memcpy(b.get(), a.get(), n);
  std::memset(static_cast<void *>(a.get()), 0xEE, n);
  a.reset();
  REQUIRE(nsv_test::format(*b) == s);

  // And again, into an arena at a different address.
  arena ar(n);
  std::memcpy(ar.at(0), b.get(), n);
  std::memset(static_cast<void *>(b.get()), 0xEE, n);
  b.reset();
  REQUIRE(nsv_test::format(*reinterpret_cast<nsv_semver_t *>(ar.at(0))) == s);
}

TEST_CASE("nsv_copy: terminates both strings over junk, present or absent") {
  for (char const *cs : { "1.2.3", "1.2.3-a", "1.2.3+b", "1.2.3-a+b", "1.2.3-alpha.1+x.y.z" }) {
    std::string const s = cs;  // v views it
    nsv_semver_t const v = parse_ok(s);
    size_t const n = static_cast<size_t>(nsv_copy(nullptr, 0, &v));
    for (int junk : { 0x00, 0x01, 0x5C, 0xFF }) {
      arena ar(n);
      std::memset(ar.at(0), junk, n);
      REQUIRE(nsv_copy(ar.at(0), n, &v) == static_cast<int>(n));
      char const *const tail = reinterpret_cast<char const *>(ar.at(0)) + sizeof(nsv_semver_t);
      INFO(s, " over ", junk);
      REQUIRE(tail[v.pre_len] == '\0');
      REQUIRE(tail[v.pre_len + 1u + v.build_len] == '\0');
    }
  }
}

TEST_CASE("nsv_copy: misaligned destination") {
  std::string const s = "1.2.3-a+b";
  nsv_semver_t const v = parse_ok(s);
  size_t const n = static_cast<size_t>(nsv_copy(nullptr, 0, &v));
  arena ar(n + alignof(nsv_semver_t));
  for (size_t ofs = 1; ofs < alignof(nsv_semver_t); ++ofs) {
    INFO("offset ", ofs);
    std::memset(ar.at(0), 0x5C, n + alignof(nsv_semver_t));
    REQUIRE(nsv_copy(ar.at(ofs), n * 2, &v) == NSV_RET_ERR_ALIGN);
    for (size_t i = 0; i < n + alignof(nsv_semver_t); ++i) {
      REQUIRE(*ar.at(i) == 0x5C);
    }
  }
  REQUIRE(nsv_copy(ar.at(alignof(nsv_semver_t)), n, &v) == static_cast<int>(n));
}

TEST_CASE("nsv_copy: destination too small writes nothing") {
  std::string const s = "1.2.3-alpha.1+build.99";
  nsv_semver_t const v = parse_ok(s);
  size_t const n = static_cast<size_t>(nsv_copy(nullptr, 0, &v));
  arena ar(n);
  for (size_t cap : { size_t(0), size_t(1), sizeof(nsv_semver_t), n - 1 }) {
    INFO("cap ", cap);
    std::memset(ar.at(0), 0x5C, n);
    REQUIRE(nsv_copy(ar.at(0), cap, &v) == NSV_RET_ERR_BUF_TOO_SMALL);
    for (size_t i = 0; i < n; ++i) {
      REQUIRE(*ar.at(i) == 0x5C);
    }
  }
}

TEST_CASE("nsv_copy: writes exactly the bytes it reports") {
  std::string const s = "1.2.3-alpha.1+build.99";
  nsv_semver_t const v = parse_ok(s);
  size_t const n = static_cast<size_t>(nsv_copy(nullptr, 0, &v));
  arena ar(n + 64);
  std::memset(ar.at(0), 0x5C, n + 64);
  REQUIRE(nsv_copy(ar.at(0), n + 64, &v) == static_cast<int>(n));
  for (size_t i = n; i < n + 64; ++i) {
    REQUIRE(*ar.at(i) == 0x5C);
  }
  REQUIRE(nsv_test::format(*reinterpret_cast<nsv_semver_t *>(ar.at(0))) == s);
  // Both strings are terminated, whatever was there before.
  char const *const tail = reinterpret_cast<char const *>(ar.at(0)) + sizeof(nsv_semver_t);
  REQUIRE(std::memcmp(tail, "alpha.1\0build.99\0", 17) == 0);
}

TEST_CASE("nsv_copy: into a static wrapper struct, as the header documents") {
  static struct {
    nsv_semver_t v{};
    char strs[64];
  } g_ver;
  std::string const s = "2.0.0-rc.3+exp";
  nsv_semver_t const view = parse_ok(s);
  REQUIRE(nsv_copy(&g_ver, sizeof(g_ver), &view) == static_cast<int>(need_for(4, 3)));
  REQUIRE(nsv_test::format(g_ver.v) == s);
  REQUIRE(std::memcmp(g_ver.strs, "rc.3\0exp\0", 9) == 0);
}

TEST_CASE("nsv_copy: the longest strings") {
  SUBCASE("the longest parseable version") {
    std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
    nsv_semver_t const v = parse_ok(s);
    // No early return: it would end the test case before doctest saw later subcases.
    if (!nsv_test::fits_int(need_for(30000, 35528))) {
      REQUIRE(nsv_copy(nullptr, 0, &v) == NSV_RET_ERR_TOO_LONG);
    } else {
      nsv_test::blob const c = nsv_test::copy(v);
      REQUIRE(c->build_ofs == 30001);
      REQUIRE(nsv_test::format(*c) == s);
    }
  }

  SUBCASE("a 65534-byte pre-release still fits") {
    std::string const pre(65534, 'p');
    nsv_semver_t v{};
    v.src = pre.data();
    v.pre_len = 65534;
    if (!nsv_test::fits_int(need_for(65534, 0))) {
      REQUIRE(nsv_copy(nullptr, 0, &v) == NSV_RET_ERR_TOO_LONG);
    } else {
      nsv_test::blob const c = nsv_test::copy(v);
      REQUIRE(c->build_ofs == 65535);
      REQUIRE(nsv_test::pre(*c) == pre);
      REQUIRE(nsv_test::build(*c).empty());
    }
  }

  SUBCASE("a 65535-byte pre-release leaves no room for the build offset") {
    std::string const pre(65535, 'p');
    nsv_semver_t v{};
    v.src = pre.data();
    v.pre_len = 65535;
    REQUIRE(nsv_copy(nullptr, 0, &v) == NSV_RET_ERR_TOO_LONG);
    unsigned char buf[sizeof(nsv_semver_t)];
    REQUIRE(nsv_copy(buf, sizeof(buf), &v) == NSV_RET_ERR_TOO_LONG);
  }
}
