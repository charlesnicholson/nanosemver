#include "nsv_test.h"

#include <string>

using nsv_test::parse_ok;

TEST_CASE("nsv_get_pre / nsv_get_build: NULL version") {
  char const *p = "untouched";
  char const *const before = p;
  REQUIRE(nsv_get_pre(nullptr, &p) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_get_build(nullptr, &p) == NSV_RET_ERR_ARG);
  REQUIRE(p == before);
  REQUIRE(nsv_get_pre(nullptr, nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_get_build(nullptr, nullptr) == NSV_RET_ERR_ARG);
}

TEST_CASE("nsv_get_pre / nsv_get_build: a view points into the source") {
  std::string const s = "1.2.3-rc.1+build.5";
  nsv_semver_t const v = parse_ok(s);
  char const *p = nullptr;
  REQUIRE(nsv_get_pre(&v, &p) == 4);
  REQUIRE(p == s.data() + 6);
  REQUIRE(p[4] == '+');  // not NUL-terminated: use the length
  REQUIRE(nsv_get_build(&v, &p) == 7);
  REQUIRE(p == s.data() + 11);
}

TEST_CASE("nsv_get_pre / nsv_get_build: absent is \"\" and 0") {
  std::string const s = "1.2.3";
  nsv_semver_t const view = parse_ok(s);
  nsv_test::blob const c = nsv_test::copy(view);
  static nsv_semver_t const lit = NSV_SEMVER_LIT(1, 2, 3, "", "");
  nsv_semver_t zero{};
  for (nsv_semver_t const *v : { &view, static_cast<nsv_semver_t const *>(c.get()), &lit,
                                 static_cast<nsv_semver_t const *>(&zero) }) {
    char const *p = nullptr;
    REQUIRE(nsv_get_pre(v, &p) == 0);
    REQUIRE(p != nullptr);
    REQUIRE(*p == '\0');
    p = nullptr;
    REQUIRE(nsv_get_build(v, &p) == 0);
    REQUIRE(p != nullptr);
    REQUIRE(*p == '\0');
  }
}

TEST_CASE("nsv_get_pre / nsv_get_build: NULL out just returns the length") {
  std::string const s = "1.2.3-alpha.1+exp";
  nsv_semver_t const v = parse_ok(s);
  REQUIRE(nsv_get_pre(&v, nullptr) == 7);
  REQUIRE(nsv_get_build(&v, nullptr) == 3);
  std::string const r = "1.2.3";
  nsv_semver_t const vr = parse_ok(r);
  REQUIRE(nsv_get_pre(&vr, nullptr) == 0);
  REQUIRE(nsv_get_build(&vr, nullptr) == 0);
}

TEST_CASE("nsv_get_pre / nsv_get_build: copies and literals are NUL-terminated") {
  std::string const s = "1.2.3-alpha.1+exp";
  nsv_test::blob const c = nsv_test::copy(parse_ok(s));
  static nsv_semver_t const lit = NSV_SEMVER_LIT(1, 2, 3, "alpha.1", "exp");
  for (nsv_semver_t const *v : { static_cast<nsv_semver_t const *>(c.get()), &lit }) {
    char const *p = nullptr;
    REQUIRE(nsv_get_pre(v, &p) == 7);
    REQUIRE(std::string(p) == "alpha.1");
    REQUIRE(nsv_get_build(v, &p) == 3);
    REQUIRE(std::string(p) == "exp");
  }
}

TEST_CASE("nsv_get_pre / nsv_get_build: the longest lengths") {
  std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
  nsv_semver_t const v = parse_ok(s);
  std::string const p = "1.0.0-" + std::string(65529, 'p');
  nsv_semver_t const vp = parse_ok(p);
  auto want = [](int n) { return nsv_test::fits_int(size_t(n)) ? n : NSV_RET_ERR_TOO_LONG; };
  REQUIRE(nsv_get_pre(&v, nullptr) == want(30000));
  REQUIRE(nsv_get_build(&v, nullptr) == want(35528));
  REQUIRE(nsv_get_pre(&vp, nullptr) == want(65529));
}
