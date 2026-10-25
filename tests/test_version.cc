// NSV_VERSION_LIT, as checked in (0.0.0) or as release.sh stamps it.
#include "nsv_test.h"

#include <string>

TEST_CASE("NSV_VERSION_LIT: a version that formats and parses back the same") {
  nsv_semver_t const v = NSV_VERSION_LIT;
  std::string const s = nsv_test::format(v);
  nsv_semver_t const back = nsv_test::parse_ok(s);
  REQUIRE(nsv_cmp(&v, &back) == NSV_CMP_EQ);
  REQUIRE(nsv_test::build(v) == nsv_test::build(back));  // == ignores build metadata
}
