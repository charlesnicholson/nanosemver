#include "corpus.h"
#include "nsv_test.h"

#include <cstring>
#include <string>

using nsv_test::parse_ok;

TEST_CASE("nsv_format: NULL version") {
  char buf[32];
  REQUIRE(nsv_format(nullptr, 0, nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_format(buf, sizeof(buf), nullptr) == NSV_RET_ERR_ARG);
}

TEST_CASE("nsv_format: round-trips the corpus") {
  for (char const *p : corpus::k_valid) {
    std::string const s = p;
    INFO(s);
    REQUIRE(nsv_test::format(parse_ok(s)) == s);
  }
}

TEST_CASE("nsv_format: sizing call") {
  std::string const s = "1.22.333-rc.1+b";
  nsv_semver_t const v = parse_ok(s);
  REQUIRE(nsv_format(nullptr, 0, &v) == static_cast<int>(s.size()));
  REQUIRE(nsv_format(nullptr, 999, &v) == static_cast<int>(s.size()));  // n is ignored
}

TEST_CASE("nsv_format: buffer sizes around the exact fit") {
  std::string const s = "1.2.3-alpha+exp";
  nsv_semver_t const v = parse_ok(s);
  size_t const len = s.size();
  char buf[64];

  SUBCASE("exact fit") {
    std::memset(buf, 0x5C, sizeof(buf));
    REQUIRE(nsv_format(buf, len + 1, &v) == static_cast<int>(len));
    REQUIRE(std::string(buf) == s);
    REQUIRE(buf[len + 1] == 0x5C);
  }

  SUBCASE("roomier than needed") {
    std::memset(buf, 0x5C, sizeof(buf));
    REQUIRE(nsv_format(buf, sizeof(buf), &v) == static_cast<int>(len));
    REQUIRE(std::string(buf) == s);
    REQUIRE(buf[len + 1] == 0x5C);
  }

  SUBCASE("one short: no truncation, just an empty string") {
    for (size_t n = 1; n <= len; ++n) {
      INFO("n = ", n);
      std::memset(buf, 0x5C, sizeof(buf));
      REQUIRE(nsv_format(buf, n, &v) == NSV_RET_ERR_BUF_TOO_SMALL);
      REQUIRE(buf[0] == '\0');
      for (size_t i = 1; i < sizeof(buf); ++i) {
        REQUIRE(buf[i] == 0x5C);
      }
    }
  }

  SUBCASE("zero capacity writes nothing") {
    std::memset(buf, 0x5C, sizeof(buf));
    REQUIRE(nsv_format(buf, 0, &v) == NSV_RET_ERR_BUF_TOO_SMALL);
    for (char const c : buf) {
      REQUIRE(c == 0x5C);
    }
  }
}

TEST_CASE("nsv_format: every digit count in every position") {
  uint32_t edges[21];
  int n = 0;
  for (uint64_t p = 1; p <= 1000000000u; p *= 10) {
    edges[n++] = static_cast<uint32_t>(p - 1);  // 0, 9, 99, ...
    edges[n++] = static_cast<uint32_t>(p);      // 1, 10, 100, ...
  }
  edges[n++] = 4294967295u;
  for (int pos = 0; pos < 3; ++pos) {
    for (int i = 0; i < n; ++i) {
      nsv_semver_t v{};
      uint32_t *const f[3] = { &v.major, &v.minor, &v.patch };
      *f[pos] = edges[i];
      std::string want[3] = { "0", "0", "0" };
      want[pos] = std::to_string(edges[i]);
      INFO(edges[i]);
      REQUIRE(nsv_test::format(v) == want[0] + "." + want[1] + "." + want[2]);
    }
  }
  nsv_semver_t zero{};
  REQUIRE(nsv_test::format(zero) == "0.0.0");
}

TEST_CASE("nsv_format: pre-release and build in any combination") {
  for (char const *p : { "1.2.3", "1.2.3-a", "1.2.3+b", "1.2.3-a+b", "1.2.3-a.b.c+d.e.f" }) {
    std::string const s = p;
    INFO(s);
    REQUIRE(nsv_test::format(parse_ok(s)) == s);
  }
}

TEST_CASE("nsv_format: literals and copies") {
  static nsv_semver_t const lit = NSV_SEMVER_LIT(3, 1, 4, "", "g1234");
  REQUIRE(nsv_test::format(lit) == "3.1.4+g1234");
  static nsv_semver_t const lit2 = NSV_SEMVER_LIT(3, 1, 4, "rc", "");
  REQUIRE(nsv_test::format(lit2) == "3.1.4-rc");
  nsv_test::blob const c = nsv_test::copy(lit);
  REQUIRE(nsv_test::format(*c) == "3.1.4+g1234");
}

TEST_CASE("nsv_format: the longest parseable version") {
  std::string const s = "4294967295.4294967295.4294967295-" + std::string(30000, 'p') + "+" +
                        std::string(35535 - 34, 'b');
  REQUIRE(s.size() == 65535);
  nsv_semver_t const v = parse_ok(s);
  if (!nsv_test::fits_int(s.size())) {
    REQUIRE(nsv_format(nullptr, 0, &v) == NSV_RET_ERR_TOO_LONG);
    return;
  }
  REQUIRE(nsv_test::format(v) == s);
}
