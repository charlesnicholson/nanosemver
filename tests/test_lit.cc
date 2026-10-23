#include "lit_c.h"
#include "nsv_test.h"

#include <cstring>
#include <string>

// At namespace scope with static storage: what build stamping looks like.
static nsv_semver_t const k_stamp = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");

TEST_CASE("NSV_SEMVER_LIT: fields") {
  REQUIRE(k_stamp.major == 1);
  REQUIRE(k_stamp.minor == 4);
  REQUIRE(k_stamp.patch == 0);
  REQUIRE(k_stamp.src != nullptr);
  REQUIRE(k_stamp.pre_ofs == 0);
  REQUIRE(k_stamp.pre_len == 4);
  REQUIRE(k_stamp.build_ofs == 5);
  REQUIRE(k_stamp.build_len == 8);
  REQUIRE(std::memcmp(k_stamp.src, "rc.2\0g3f9a1c2\0", 14) == 0);
}

TEST_CASE("NSV_SEMVER_LIT: behaves like the parsed string") {
  std::string const s = "1.4.0-rc.2+g3f9a1c2";
  REQUIRE(nsv_test::format(k_stamp) == s);
  REQUIRE(nsv_test::pre(k_stamp) == "rc.2");
  REQUIRE(nsv_test::build(k_stamp) == "g3f9a1c2");
  nsv_semver_t const v = nsv_test::parse_ok(s);
  REQUIRE(nsv_cmp(&k_stamp, &v) == NSV_CMP_EQ);
}

TEST_CASE("NSV_SEMVER_LIT: absent parts") {
  static nsv_semver_t const none = NSV_SEMVER_LIT(5, 6, 7, "", "");
  static nsv_semver_t const pre = NSV_SEMVER_LIT(5, 6, 7, "a.1", "");
  static nsv_semver_t const build = NSV_SEMVER_LIT(5, 6, 7, "", "b.2");
  REQUIRE(nsv_test::format(none) == "5.6.7");
  REQUIRE(nsv_test::format(pre) == "5.6.7-a.1");
  REQUIRE(nsv_test::format(build) == "5.6.7+b.2");
  REQUIRE(build.build_ofs == 1);
}

TEST_CASE("NSV_SEMVER_LIT: the documented round-trip check") {
  // The header says: literals are not validated, so format and re-parse in a test.
  char buf[64];
  int const n = nsv_format(buf, sizeof(buf), &k_stamp);
  REQUIRE(n > 0);
  nsv_semver_t v;
  REQUIRE(nsv_parse(&v, buf, static_cast<size_t>(n)) == NSV_RET_OK);
  REQUIRE(nsv_cmp(&v, &k_stamp) == NSV_CMP_EQ);

  static nsv_semver_t const bad = NSV_SEMVER_LIT(1, 0, 0, "rc.01", "");
  int const nb = nsv_format(buf, sizeof(buf), &bad);
  REQUIRE(nb > 0);
  REQUIRE(nsv_parse(&v, buf, static_cast<size_t>(nb)) == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("C: NSV_SEMVER_LIT compiled as C99") {
  REQUIRE(nsv_test::format(*lit_c_full()) == "1.4.0-rc.2+g3f9a1c2");
  REQUIRE(nsv_test::format(*lit_c_bare()) == "2.0.0");
  REQUIRE(nsv_cmp(lit_c_full(), &k_stamp) == NSV_CMP_EQ);
}

TEST_CASE("C: qsort with the documented wrapper") {
  REQUIRE(lit_c_sort_check() == 1);
}

TEST_CASE("C: copy into a static wrapper struct") {
  char buf[64];
  REQUIRE(lit_c_static_copy(buf, sizeof(buf)) == 19);
  REQUIRE(std::string(buf) == "1.4.0-rc.2+g3f9a1c2");
}
