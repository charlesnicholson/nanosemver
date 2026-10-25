// The version macros, as checked in (0.0.0) or as release.sh stamps them.
#include "nsv_test.h"

#include <string>

// Usable by the preprocessor, for feature checks.
#if !defined(NSV_VERSION_MAJOR) || !defined(NSV_VERSION_MINOR) || \
    !defined(NSV_VERSION_PATCH) || ((NSV_VERSION_MAJOR + NSV_VERSION_MINOR + NSV_VERSION_PATCH) < 0)
#error "the version macros must be integer constants"
#endif

TEST_CASE("NSV_VERSION: a version, agreeing with the numbers") {
  std::string const s = "" NSV_VERSION;  // a string literal
  nsv_semver_t const v = nsv_test::parse_ok(s);
  REQUIRE(v.major == NSV_VERSION_MAJOR);
  REQUIRE(v.minor == NSV_VERSION_MINOR);
  REQUIRE(v.patch == NSV_VERSION_PATCH);
  REQUIRE(nsv_test::pre(v) == "" NSV_VERSION_PRE);  // string literals too
  REQUIRE(nsv_test::build(v) == "" NSV_VERSION_BUILD);
}

TEST_CASE("NSV_VERSION_LIT: the same version, as a literal") {
  static nsv_semver_t const lit = NSV_VERSION_LIT;
  std::string const s = NSV_VERSION;
  REQUIRE(nsv_test::format(lit) == s);
  nsv_semver_t const v = nsv_test::parse_ok(s);
  REQUIRE(nsv_cmp(&lit, &v) == NSV_CMP_EQ);
  REQUIRE(nsv_test::build(lit) == nsv_test::build(v));
}
