// Linked against nanosemver.c built with -DNSV_INT_MAX=32767, as on a 16-bit-int
// target: the lengths nanosemver reports through int must fit, or be refused.
#include "../nsv_test.h"

#include <string>

using nsv_test::parse_ok;

namespace {

// pre_len such that a copy needs exactly `need` bytes.
std::string version_needing(size_t need) {
  return "1.0.0-" + std::string(need - sizeof(nsv_semver_t) - 2, 'p');
}

}  // namespace

TEST_CASE("int16: small versions are unaffected") {
  std::string const s = "1.2.3-rc.1+b";
  nsv_semver_t const v = parse_ok(s);
  REQUIRE(nsv_test::format(v) == s);
  REQUIRE(nsv_test::format(*nsv_test::copy(v)) == s);
  REQUIRE(nsv_get_pre(&v, nullptr) == 4);
  REQUIRE(nsv_get_build(&v, nullptr) == 1);
}

TEST_CASE("int16: nsv_get_pre and nsv_get_build refuse lengths over INT_MAX") {
  std::string const okp = "1.0.0-" + std::string(32767, 'p');
  nsv_semver_t const vokp = parse_ok(okp);
  REQUIRE(nsv_get_pre(&vokp, nullptr) == 32767);
  std::string const okb = "1.0.0+" + std::string(32767, 'b');
  nsv_semver_t const vokb = parse_ok(okb);
  REQUIRE(nsv_get_build(&vokb, nullptr) == 32767);

  std::string const big = "1.0.0-" + std::string(32768, 'p') + "+" + std::string(32760, 'b');
  nsv_semver_t const vbig = parse_ok(big);
  char const *p = "untouched";
  char const *const before = p;
  REQUIRE(nsv_get_pre(&vbig, &p) == NSV_RET_ERR_TOO_LONG);
  REQUIRE(p == before);

  std::string const bigb = "1.0.0+" + std::string(32768, 'b');
  nsv_semver_t const vbigb = parse_ok(bigb);
  REQUIRE(nsv_get_build(&vbigb, &p) == NSV_RET_ERR_TOO_LONG);
  REQUIRE(p == before);
}

TEST_CASE("int16: nsv_format refuses a length over INT_MAX") {
  std::string const ok = "1.0.0-" + std::string(32767 - 6, 'p');
  REQUIRE(ok.size() == 32767);
  nsv_semver_t const vok = parse_ok(ok);
  REQUIRE(nsv_format(nullptr, 0, &vok) == 32767);
  REQUIRE(nsv_test::format(vok) == ok);

  std::string const big = ok + "p";
  nsv_semver_t const vbig = parse_ok(big);
  REQUIRE(nsv_format(nullptr, 0, &vbig) == NSV_RET_ERR_TOO_LONG);
  std::string buf(big.size() + 1, 'x');
  REQUIRE(nsv_format(&buf[0], buf.size(), &vbig) == NSV_RET_ERR_TOO_LONG);
}

TEST_CASE("int16: nsv_copy refuses a size over INT_MAX") {
  std::string const ok = version_needing(32767);
  nsv_semver_t const vok = parse_ok(ok);
  REQUIRE(nsv_copy(nullptr, 0, &vok) == 32767);
  REQUIRE(nsv_test::format(*nsv_test::copy(vok)) == ok);

  std::string const big = version_needing(32768);
  nsv_semver_t const vbig = parse_ok(big);
  REQUIRE(nsv_copy(nullptr, 0, &vbig) == NSV_RET_ERR_TOO_LONG);
  nsv_test::blob const b = nsv_test::alloc_blob(40000);
  REQUIRE(nsv_copy(b.get(), 40000, &vbig) == NSV_RET_ERR_TOO_LONG);
}
