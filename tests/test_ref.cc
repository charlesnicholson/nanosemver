// Checks the oracle itself against the regular expression semver.org publishes, so
// the differential tests and fuzzers rest on something independent of both.
#include "corpus.h"
#include "fuzz/gen.h"
#include "nsv_doctest.h"
#include "range_cases.h"
#include "ref_semver.h"

#include <iterator>
#include <regex>
#include <string>

namespace {

// https://semver.org/#is-there-a-suggested-regular-expression-regex-to-check-a-semver-string
std::regex const k_semver_re(
    R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))"
    R"((?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?)"
    R"((?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)");

void check_against_regex(std::string const &s) {
  std::smatch m;
  bool const re_ok = std::regex_match(s, m, k_semver_re);
  ref::version v;
  ref::status const st = ref::parse(s, &v);
  INFO("\"", s, "\"");
  // The regex has no numeric limits, so an overflow is still a match.
  REQUIRE(re_ok == ((st == ref::status::ok) || (st == ref::status::overflow)));
  if (st == ref::status::ok) {
    REQUIRE(m[1].str() == std::to_string(v.major));
    REQUIRE(m[2].str() == std::to_string(v.minor));
    REQUIRE(m[3].str() == std::to_string(v.patch));
    REQUIRE(m[4].matched == v.has_pre);
    REQUIRE(m[4].str() == v.pre);
    REQUIRE(m[5].matched == v.has_build);
    REQUIRE(m[5].str() == v.build);
  }
}

}  // namespace

TEST_CASE("reference: agrees with the semver.org regex on the corpus") {
  for (char const *s : corpus::k_valid) {
    check_against_regex(s);
  }
  for (char const *s : corpus::k_overflow) {
    check_against_regex(s);
  }
  for (char const *s : corpus::k_invalid) {
    check_against_regex(s);
  }
}

TEST_CASE("reference: agrees with the semver.org regex on generated inputs") {
  nsv_fuzz::rng r(0x5EB7E5);
  for (int i = 0; i < 20000; ++i) {
    std::string const s = nsv_fuzz::gen_version(r);
    if (s.size() < 200) {  // std::regex recurses per character
      check_against_regex(s);
    }
  }
}

TEST_CASE("reference: the length limit") {
  ref::version v;
  REQUIRE(ref::parse("1.0.0-" + std::string(65529, 'a'), &v) == ref::status::ok);
  REQUIRE(ref::parse("1.0.0-" + std::string(65530, 'a'), &v) == ref::status::too_long);
  REQUIRE(ref::parse("1.0.0-" + std::string(65530, 'a') + ".", &v) == ref::status::syntax);
}

TEST_CASE("reference: precedence examples from the spec") {
  char const *const order[] = { "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                                "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
                                "1.0.0-rc.1", "1.0.0", "2.0.0", "2.1.0", "2.1.1" };
  for (size_t i = 0; i < std::size(order); ++i) {
    for (size_t j = 0; j < std::size(order); ++j) {
      ref::version a, b;
      REQUIRE(ref::parse(order[i], &a) == ref::status::ok);
      REQUIRE(ref::parse(order[j], &b) == ref::status::ok);
      INFO(order[i], " vs ", order[j]);
      REQUIRE(ref::cmp(a, b) == ((i < j) ? -1 : (i > j) ? 1 : 0));
    }
  }
}

TEST_CASE("reference: every range case, as nsv_range_match is held to") {
  // The tables npm answered, but for its own special cases, and the != ones: the
  // reference has to give the same answers, or it cannot judge anything else.
  for (range_cases::table const &t : range_cases::k_all) {
    for (unsigned i = 0; i < t.n; ++i) {
      range_cases::match_case const &c = t.rows[i];
      ref::version v;
      REQUIRE(ref::parse(c.version, &v) == ref::status::ok);
      for (bool excl : { false, true }) {
        bool match = false;
        INFO("\"", c.range, "\" against ", c.version, excl ? ", excluding pre-releases" : "");
        REQUIRE(ref::range_match(c.range, v, excl, &match) == ref::status::ok);
        REQUIRE(int(match) == (excl ? c.exclude_pre : c.plain));
      }
    }
  }
}
