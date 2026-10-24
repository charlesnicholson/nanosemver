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
}
