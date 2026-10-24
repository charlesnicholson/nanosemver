#include "corpus.h"
#include "nsv_test.h"
#include "ref_semver.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

struct prefix_result {
  int rc;
  size_t consumed;
  nsv_semver_t v;
};

// Parses a prefix of s, which sits against a guard page so any over-read faults.
prefix_result parse_prefix(std::string_view s) {
  static nsv_test::guarded g;
  prefix_result r{ 0, 12345, {} };
  r.rc = nsv_parse_prefix(&r.v, g.place(s), s.size(), &r.consumed);
  return r;
}

}  // namespace

TEST_CASE("nsv_parse_prefix: NULL arguments") {
  nsv_semver_t v{};
  size_t n = 7;
  REQUIRE(nsv_parse_prefix(nullptr, "1.2.3", 5, &n) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_parse_prefix(&v, nullptr, 5, &n) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_parse_prefix(&v, "1.2.3", 5, nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(n == 7);
}

TEST_CASE("nsv_parse_prefix: a whole version") {
  for (char const *s : corpus::k_valid) {
    INFO(s);
    prefix_result const r = parse_prefix(s);
    REQUIRE(r.rc == NSV_RET_OK);
    REQUIRE(r.consumed == std::strlen(s));
  }
  for (char const *s : corpus::k_overflow) {
    INFO(s);
    prefix_result const r = parse_prefix(s);
    REQUIRE(r.rc == NSV_RET_ERR_OVERFLOW);
    REQUIRE(r.consumed == std::strlen(s));
  }
}

TEST_CASE("nsv_parse_prefix: the longest prefix that is a version") {
  struct {
    char const *s, *version;
  } const cases[] = {
    { "1.2.3 ", "1.2.3" },
    { "1.2.3, 2.0.0", "1.2.3" },
    { "1.2.3 || 2.0.0", "1.2.3" },
    { "1.2.3<2.0.0", "1.2.3" },
    { "1.2.3.", "1.2.3" },  // the end of a sentence
    { "1.2.3-rc.1.", "1.2.3-rc.1" },
    { "1.2.3+b.", "1.2.3+b" },
    { "1.2.3.4", "1.2.3" },
    { "1.2.3x", "1.2.3" },
    { "1.2.34", "1.2.34" },
    { "1.2.03", "1.2.0" },
    { "1.2.3-", "1.2.3" },
    { "1.2.3+", "1.2.3" },
    { "1.2.3-+b", "1.2.3" },
    { "1.2.3-.a", "1.2.3" },
    { "1.2.3+.a", "1.2.3" },
    { "1.2.3-a.", "1.2.3-a" },
    { "1.2.3-a..b", "1.2.3-a" },
    { "1.2.3-a.b.", "1.2.3-a.b" },
    { "1.2.3-a.+b", "1.2.3-a" },
    { "1.2.3-a+", "1.2.3-a" },
    { "1.2.3-a+.b", "1.2.3-a" },
    { "1.2.3-a+b.", "1.2.3-a+b" },
    { "1.2.3-a+b..c", "1.2.3-a+b" },
    { "1.2.3+b+c", "1.2.3+b" },
    { "1.2.3+b-c+d", "1.2.3+b-c" },
    { "1.2.3-a+b-c+d", "1.2.3-a+b-c" },
    // A numeric identifier with a leading zero: "0" is as far as it goes.
    { "1.2.3-01", "1.2.3-0" },
    { "1.2.3-007", "1.2.3-0" },
    { "1.2.3-a.01", "1.2.3-a.0" },
    { "1.2.3-01+b", "1.2.3-0" },
    { "1.2.3-01.a", "1.2.3-0" },
    { "1.2.3-0123abc", "1.2.3-0123abc" },  // alphanumeric, so valid
    { "1.2.3+01", "1.2.3+01" },            // leading zeros are fine in build metadata
  };
  for (auto const &c : cases) {
    INFO(c.s);
    prefix_result const r = parse_prefix(c.s);
    REQUIRE(r.rc == NSV_RET_OK);
    REQUIRE(r.consumed == std::strlen(c.version));
    REQUIRE(nsv_test::format(r.v) == c.version);
  }
}

TEST_CASE("nsv_parse_prefix: no prefix is a version") {
  for (char const *s : { "", "1", "1.", "1.2", "1.2.", "1.2 .3", "v1.2.3", " 1.2.3", "01.2.3",
                         "1.02.3", "-1.2.3", "1..2.3", "x" }) {
    INFO("\"", s, "\"");
    prefix_result const r = parse_prefix(s);
    REQUIRE(r.rc == NSV_RET_ERR_SYNTAX);
    REQUIRE(r.consumed == 0);
  }
}

TEST_CASE("nsv_parse_prefix: limits apply to the prefix alone") {
  SUBCASE("overflow spans the version") {
    prefix_result r = parse_prefix("4294967296.0.0 and more");
    REQUIRE(r.rc == NSV_RET_ERR_OVERFLOW);
    REQUIRE(r.consumed == 14);
    r = parse_prefix("1.4294967296.0-");
    REQUIRE(r.rc == NSV_RET_ERR_OVERFLOW);
    REQUIRE(r.consumed == 14);
  }

  SUBCASE("too long spans the version") {
    std::string const v = "1.2.3-" + std::string(65530, 'a');
    prefix_result const r = parse_prefix(v + " tail");
    REQUIRE(r.rc == NSV_RET_ERR_TOO_LONG);
    REQUIRE(r.consumed == v.size());
  }

  SUBCASE("a 65535-byte version followed by more") {
    std::string const v = "1.2.3-" + std::string(65529, 'a');
    prefix_result const r = parse_prefix(v + std::string(10000, ' '));
    REQUIRE(r.rc == NSV_RET_OK);
    REQUIRE(r.consumed == 65535);
    REQUIRE(r.v.pre_len == 65529);
  }

  SUBCASE("a short version at the start of a long string") {
    prefix_result const r = parse_prefix("1.2.3 " + std::string(70000, 'x'));
    REQUIRE(r.rc == NSV_RET_OK);
    REQUIRE(r.consumed == 5);
  }
}

TEST_CASE("nsv_parse_prefix: *out is untouched on error, *consumed always set") {
  nsv_semver_t v{};
  unsigned char before[sizeof(v)];
  std::memset(&v, 0xA5, sizeof(v));
  std::memcpy(before, &v, sizeof(v));
  size_t n = 99;
  REQUIRE(nsv_parse_prefix(&v, "1.2", 3, &n) == NSV_RET_ERR_SYNTAX);
  REQUIRE(n == 0);
  n = 99;
  REQUIRE(nsv_parse_prefix(&v, "4294967296.0.0", 14, &n) == NSV_RET_ERR_OVERFLOW);
  REQUIRE(n == 14);
  REQUIRE(!std::memcmp(&v, before, sizeof(v)));
}

TEST_CASE("nsv_parse_prefix: agrees with nsv_parse on the whole string") {
  std::vector<char const *> all;
  all.insert(all.end(), std::begin(corpus::k_valid), std::end(corpus::k_valid));
  all.insert(all.end(), std::begin(corpus::k_invalid), std::end(corpus::k_invalid));
  all.insert(all.end(), std::begin(corpus::k_overflow), std::end(corpus::k_overflow));
  for (char const *full : all) {
    std::string const s = full;
    INFO("\"", s, "\"");
    prefix_result const r = parse_prefix(s);
    int const whole = nsv_test::parse_rc(s);
    if (r.consumed == s.size()) {
      REQUIRE(r.rc == whole);
    } else {
      REQUIRE(whole == NSV_RET_ERR_SYNTAX);
    }
  }
}

TEST_CASE("nsv_parse_prefix: every prefix of every corpus string, with a tail") {
  std::vector<char const *> all;
  all.insert(all.end(), std::begin(corpus::k_valid), std::end(corpus::k_valid));
  all.insert(all.end(), std::begin(corpus::k_invalid), std::end(corpus::k_invalid));
  all.insert(all.end(), std::begin(corpus::k_overflow), std::end(corpus::k_overflow));
  for (char const *full : all) {
    std::string const s = full;
    for (size_t n = 0; n <= s.size(); ++n) {
      for (char const *tail : { "", " ", ".", "-", "+", "0", "a", ".0", "-0", "+0" }) {
        std::string const p = s.substr(0, n) + tail;
        ref::version rv;
        size_t want = 0;
        ref::status const st = ref::parse_prefix(p, &rv, &want);
        prefix_result const r = parse_prefix(p);
        INFO("\"", p, "\"");
        REQUIRE(r.consumed == want);
        switch (st) {
          case ref::status::ok: REQUIRE(r.rc == NSV_RET_OK); break;
          case ref::status::syntax: REQUIRE(r.rc == NSV_RET_ERR_SYNTAX); break;
          case ref::status::overflow: REQUIRE(r.rc == NSV_RET_ERR_OVERFLOW); break;
          case ref::status::too_long: REQUIRE(r.rc == NSV_RET_ERR_TOO_LONG); break;
        }
      }
    }
  }
}

TEST_CASE("nsv_parse_prefix: every string of up to six bytes, one of each kind") {
  // A byte's kind is all that matters: '0', another digit, a letter, '-', '.', '+', or
  // anything else. Every string of up to six of them, alone and after a version core.
  static char const k_kinds[] = { '0', '1', 'a', '-', '.', '+', '_' };
  std::string s;
  auto check = [](std::string const &str) {
    ref::version rv;
    size_t want = 0;
    ref::status const st = ref::parse_prefix(str, &rv, &want);
    prefix_result const r = parse_prefix(str);
    if ((r.consumed != want) ||
        (r.rc != ((st == ref::status::ok) ? NSV_RET_OK : NSV_RET_ERR_SYNTAX))) {
      INFO("\"", str, "\"");
      REQUIRE(r.consumed == want);
      REQUIRE(r.rc == ((st == ref::status::ok) ? NSV_RET_OK : NSV_RET_ERR_SYNTAX));
    }
  };
  for (char const *head : { "", "1.2.3", "0.0.0" }) {
    for (size_t len = 0; len <= 6; ++len) {
      size_t total = 1;
      for (size_t i = 0; i < len; ++i) {
        total *= sizeof(k_kinds);
      }
      for (size_t n = 0; n < total; ++n) {
        s = head;
        for (size_t i = 0, m = n; i < len; ++i, m /= sizeof(k_kinds)) {
          s += k_kinds[m % sizeof(k_kinds)];
        }
        check(s);
      }
    }
  }
}
