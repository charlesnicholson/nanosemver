#include "corpus.h"
#include "nsv_test.h"
#include "ref_semver.h"

#include <cstring>
#include <string>
#include <vector>

using nsv_test::parse_ok;
using nsv_test::parse_rc;

namespace {

bool is_digit(int c) { return (c >= '0') && (c <= '9'); }

bool is_ident(int c) {
  return is_digit(c) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
         (c == '-');
}

std::string with_byte(char const *prefix, int c, char const *suffix) {
  return std::string(prefix) + static_cast<char>(c) + suffix;
}

}  // namespace

TEST_CASE("nsv_parse: NULL arguments") {
  nsv_semver_t v;
  REQUIRE(nsv_parse(nullptr, "1.2.3", 5) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_parse(&v, nullptr, 5) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_parse(&v, nullptr, 0) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_parse(nullptr, nullptr, 0) == NSV_RET_ERR_ARG);
}

TEST_CASE("nsv_parse: semver.org corpus") {
  for (char const *s : corpus::k_valid) {
    INFO(s);
    REQUIRE(parse_rc(s) == NSV_RET_OK);
  }
  for (char const *s : corpus::k_overflow) {
    INFO(s);
    REQUIRE(parse_rc(s) == NSV_RET_ERR_OVERFLOW);
  }
  for (char const *s : corpus::k_invalid) {
    INFO(s);
    REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
  }
}

TEST_CASE("nsv_parse: fields of a full version") {
  std::string const s = "12.345.6789-rc.1+build.42";
  nsv_semver_t const v = parse_ok(s);
  REQUIRE(v.major == 12);
  REQUIRE(v.minor == 345);
  REQUIRE(v.patch == 6789);
  REQUIRE(v.src == s.data());
  REQUIRE(v.pre_ofs == 12);
  REQUIRE(v.pre_len == 4);
  REQUIRE(v.build_ofs == 17);
  REQUIRE(v.build_len == 8);
  REQUIRE(s.substr(v.pre_ofs, v.pre_len) == "rc.1");
  REQUIRE(s.substr(v.build_ofs, v.build_len) == "build.42");
}

TEST_CASE("nsv_parse: absent pre-release and build are zero") {
  std::string const s = "1.2.3";
  nsv_semver_t const v = parse_ok(s);
  REQUIRE(v.src == s.data());
  REQUIRE(v.pre_ofs == 0);
  REQUIRE(v.pre_len == 0);
  REQUIRE(v.build_ofs == 0);
  REQUIRE(v.build_len == 0);

  std::string const b = "1.2.3+meta";
  nsv_semver_t const vb = parse_ok(b);
  REQUIRE(vb.pre_ofs == 0);
  REQUIRE(vb.pre_len == 0);
  REQUIRE(vb.build_ofs == 6);
  REQUIRE(vb.build_len == 4);

  std::string const p = "1.2.3-pre";
  nsv_semver_t const vp = parse_ok(p);
  REQUIRE(vp.pre_ofs == 6);
  REQUIRE(vp.pre_len == 3);
  REQUIRE(vp.build_ofs == 0);
  REQUIRE(vp.build_len == 0);
}

TEST_CASE("nsv_parse: version core") {
  SUBCASE("zero") {
    std::string const s = "0.0.0";
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(v.major == 0);
    REQUIRE(v.minor == 0);
    REQUIRE(v.patch == 0);
  }

  SUBCASE("each field lands in its own slot") {
    std::string const s = "7.8.9";
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(v.major == 7);
    REQUIRE(v.minor == 8);
    REQUIRE(v.patch == 9);
  }

  SUBCASE("leading zeros are rejected in every position") {
    for (char const *s : { "01.0.0", "0.01.0", "0.0.01", "00.0.0", "0.00.0", "0.0.00",
                           "007.0.0", "1.2.03-rc", "1.2.03+b" }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }

  SUBCASE("missing or extra fields") {
    for (char const *s : { "", "1", "1.", "1.2", "1.2.", ".1.2", "1..2", "..", "1.2.3.",
                           "1.2.3.4", ".", "-", "+", "-1.2.3", "+1.2.3" }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }

  SUBCASE("the largest numbers") {
    std::string const s = "4294967295.4294967295.4294967295";
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(v.major == 4294967295u);
    REQUIRE(v.minor == 4294967295u);
    REQUIRE(v.patch == 4294967295u);
  }

  SUBCASE("every last digit around UINT32_MAX, in every position") {
    for (int pos = 0; pos < 3; ++pos) {
      for (int d = 0; d <= 9; ++d) {
        std::string f[3] = { "1", "2", "3" };
        f[pos] = "429496729" + std::to_string(d);
        std::string const s = f[0] + "." + f[1] + "." + f[2];
        INFO(s);
        if (d <= 5) {
          nsv_semver_t const v = parse_ok(s);
          uint32_t const got[3] = { v.major, v.minor, v.patch };
          REQUIRE(got[pos] == 4294967290u + static_cast<uint32_t>(d));
        } else {
          REQUIRE(parse_rc(s) == NSV_RET_ERR_OVERFLOW);
        }
      }
    }
  }

  SUBCASE("every width of number") {
    std::string digits = "1";
    for (uint64_t want = 1; want <= 4294967295u; want = want * 10 + 1) {
      std::string const s = digits + ".0.0";
      INFO(s);
      REQUIRE(parse_ok(s).major == want);
      digits += "1";
    }
  }

  SUBCASE("overflow keeps going: many digits past the limit") {
    REQUIRE(parse_rc("1" + std::string(100, '0') + ".0.0") == NSV_RET_ERR_OVERFLOW);
    REQUIRE(parse_rc("0.0." + std::string(5000, '9')) == NSV_RET_ERR_OVERFLOW);
    REQUIRE(parse_rc("0.0.42949672951") == NSV_RET_ERR_OVERFLOW);
    REQUIRE(parse_rc("5000000000.0.0") == NSV_RET_ERR_OVERFLOW);
  }

  SUBCASE("syntax errors take precedence over overflow") {
    for (char const *s : { "4294967296.0", "4294967296.0.0-", "4294967296.0.0+01x_",
                           "4294967296.0.0.", "4294967296.01.0", "0.4294967296.x",
                           "99999999999.0.0-01" }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }
}

TEST_CASE("nsv_parse: pre-release") {
  SUBCASE("valid identifiers") {
    struct {
      char const *s, *pre;
    } const cases[] = {
      { "1.0.0-0", "0" },
      { "1.0.0-9", "9" },
      { "1.0.0-10", "10" },
      { "1.0.0-0a", "0a" },
      { "1.0.0-a0", "a0" },
      { "1.0.0-00a", "00a" },
      { "1.0.0-0-", "0-" },
      { "1.0.0--0", "-0" },
      { "1.0.0--", "-" },
      { "1.0.0---", "--" },
      { "1.0.0-a.b.c", "a.b.c" },
      { "1.0.0-0.0.0", "0.0.0" },
      { "1.0.0-1.0", "1.0" },
      { "1.0.0-x.7.z.92", "x.7.z.92" },
      { "1.0.0-18446744073709551616", "18446744073709551616" },
      { "1.0.0-ABCxyz-09", "ABCxyz-09" },
    };
    for (auto const &c : cases) {
      INFO(c.s);
      std::string const s = c.s;
      nsv_semver_t const v = parse_ok(s);
      REQUIRE(v.pre_ofs == 6);
      REQUIRE(nsv_test::pre(v) == c.pre);
      REQUIRE(v.build_len == 0);
    }
  }

  SUBCASE("numeric identifiers with leading zeros") {
    for (char const *s : { "1.0.0-00", "1.0.0-01", "1.0.0-007", "1.0.0-1.01", "1.0.0-x.00",
                           "1.0.0-a.b.01", "1.0.0-01.a", "1.0.0-00+b" }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }

  SUBCASE("empty identifiers") {
    for (char const *s : { "1.0.0-", "1.0.0-.", "1.0.0-a.", "1.0.0-.a", "1.0.0-a..b",
                           "1.0.0-+b", "1.0.0-a.+b", "1.0.0-..", "1.0.0-a.b." }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }

  SUBCASE("a '+' ends the pre-release") {
    std::string const s = "1.0.0-a-b+c-d";
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(nsv_test::pre(v) == "a-b");
    REQUIRE(nsv_test::build(v) == "c-d");
    REQUIRE(v.build_ofs == 10);
  }
}

TEST_CASE("nsv_parse: build metadata") {
  SUBCASE("valid identifiers, leading zeros allowed") {
    for (char const *b : { "0", "00", "01", "007", "a", "-", "a-", "-a", "a.b.c", "0.0.0",
                           "sha.5114f85", "21AF26D3----117B344092BD" }) {
      std::string const s = std::string("1.0.0+") + b;
      INFO(s);
      nsv_semver_t const v = parse_ok(s);
      REQUIRE(v.build_ofs == 6);
      REQUIRE(nsv_test::build(v) == b);
      REQUIRE(v.pre_len == 0);
    }
  }

  SUBCASE("empty identifiers") {
    for (char const *s : { "1.0.0+", "1.0.0+.", "1.0.0+a.", "1.0.0+.a", "1.0.0+a..b",
                           "1.0.0-a+", "1.0.0-a+.", "1.0.0+.." }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }

  SUBCASE("only one '+'") {
    for (char const *s : { "1.0.0+a+b", "1.0.0++", "1.0.0-a+b+c", "1.0.0+a.b+c" }) {
      INFO(s);
      REQUIRE(parse_rc(s) == NSV_RET_ERR_SYNTAX);
    }
  }

  SUBCASE("a '-' inside build metadata is not a pre-release") {
    std::string const s = "1.0.0+b-c";
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(v.pre_len == 0);
    REQUIRE(nsv_test::build(v) == "b-c");
  }
}

TEST_CASE("nsv_parse: every byte value in every position") {
  for (int c = 0; c < 256; ++c) {
    INFO("byte ", c);
    // A number position takes only digits; a leading zero is fine alone.
    REQUIRE((parse_rc(with_byte("", c, ".0.0")) == NSV_RET_OK) == is_digit(c));
    REQUIRE((parse_rc(with_byte("1.", c, ".0")) == NSV_RET_OK) == is_digit(c));
    REQUIRE((parse_rc(with_byte("1.2.", c, "")) == NSV_RET_OK) == is_digit(c));
    // Separators between numbers: only '.'.
    REQUIRE((parse_rc(with_byte("1", c, "2.3")) == NSV_RET_OK) == (c == '.'));
    REQUIRE((parse_rc(with_byte("1.2", c, "3")) == NSV_RET_OK) == (c == '.'));
    // After the core: '-' or '+' introduces more, or a digit extends the patch.
    REQUIRE((parse_rc(with_byte("1.2.3", c, "a")) == NSV_RET_OK) == ((c == '-') || (c == '+')));
    REQUIRE((parse_rc(with_byte("1.2.3", c, "")) == NSV_RET_OK) == is_digit(c));
    // Inside a pre-release: identifier characters, '.' between them, '+' to end it.
    REQUIRE((parse_rc(with_byte("1.2.3-a", c, "a")) == NSV_RET_OK) ==
            (is_ident(c) || (c == '.') || (c == '+')));
    REQUIRE((parse_rc(with_byte("1.2.3-", c, "")) == NSV_RET_OK) == (is_ident(c)));
    // Inside build metadata: identifier characters and '.'.
    REQUIRE((parse_rc(with_byte("1.2.3+a", c, "a")) == NSV_RET_OK) ==
            (is_ident(c) || (c == '.')));
    REQUIRE((parse_rc(with_byte("1.2.3+", c, "")) == NSV_RET_OK) == is_ident(c));
    REQUIRE((parse_rc(with_byte("1.2.3-a+b", c, "")) == NSV_RET_OK) == is_ident(c));
  }
}

TEST_CASE("nsv_parse: only len bytes are read") {
  SUBCASE("a terminator inside len is a syntax error") {
    REQUIRE(nsv_test::parse_rc(std::string("1.0.0\0", 6)) == NSV_RET_ERR_SYNTAX);
    REQUIRE(nsv_test::parse_rc(std::string("1.0\0.0", 6)) == NSV_RET_ERR_SYNTAX);
  }

  SUBCASE("a prefix of a longer buffer") {
    std::string const s = "1.2.3-rc.1 and then some";
    nsv_semver_t v;
    REQUIRE(nsv_parse(&v, s.data(), 10) == NSV_RET_OK);
    REQUIRE(nsv_test::pre(v) == "rc.1");
    REQUIRE(nsv_parse(&v, s.data(), 11) == NSV_RET_ERR_SYNTAX);
  }

  SUBCASE("zero length") {
    nsv_semver_t v;
    REQUIRE(nsv_parse(&v, "1.2.3", 0) == NSV_RET_ERR_SYNTAX);
  }

  SUBCASE("every prefix of every corpus string agrees with the reference") {
    std::vector<char const *> all;
    all.insert(all.end(), std::begin(corpus::k_valid), std::end(corpus::k_valid));
    all.insert(all.end(), std::begin(corpus::k_invalid), std::end(corpus::k_invalid));
    all.insert(all.end(), std::begin(corpus::k_overflow), std::end(corpus::k_overflow));
    for (char const *full : all) {
      std::string const s = full;
      for (size_t n = 0; n <= s.size(); ++n) {
        std::string const p = s.substr(0, n);
        ref::version rv;
        ref::status const want = ref::parse(p, &rv);
        int const rc = parse_rc(p);
        INFO("\"", p, "\"");
        switch (want) {
          case ref::status::ok: REQUIRE(rc == NSV_RET_OK); break;
          case ref::status::syntax: REQUIRE(rc == NSV_RET_ERR_SYNTAX); break;
          case ref::status::overflow: REQUIRE(rc == NSV_RET_ERR_OVERFLOW); break;
          case ref::status::too_long: REQUIRE(rc == NSV_RET_ERR_TOO_LONG); break;
        }
      }
    }
  }
}

TEST_CASE("nsv_parse: the 65535-byte limit") {
  auto with_pre_to = [](size_t total) {
    return "1.2.3-" + std::string(total - 6, 'a');
  };

  SUBCASE("exactly 65535 bytes") {
    std::string const s = with_pre_to(65535);
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(v.pre_ofs == 6);
    REQUIRE(v.pre_len == 65529);
  }

  SUBCASE("65536 bytes") {
    REQUIRE(parse_rc(with_pre_to(65536)) == NSV_RET_ERR_TOO_LONG);
    REQUIRE(parse_rc(with_pre_to(100000)) == NSV_RET_ERR_TOO_LONG);
  }

  SUBCASE("too long but invalid is a syntax error") {
    REQUIRE(parse_rc(with_pre_to(65536) + ".") == NSV_RET_ERR_SYNTAX);
    REQUIRE(parse_rc(with_pre_to(65536) + "_") == NSV_RET_ERR_SYNTAX);
  }

  SUBCASE("too long and overflowing is an overflow") {
    REQUIRE(parse_rc("4294967296.0.0-" + std::string(70000, 'a')) == NSV_RET_ERR_OVERFLOW);
  }

  SUBCASE("build metadata far into the string") {
    std::string const s = "1.0.0-" + std::string(30000, 'p') + "+" + std::string(35528, 'b');
    REQUIRE(s.size() == 65535);
    nsv_semver_t const v = parse_ok(s);
    REQUIRE(v.pre_len == 30000);
    REQUIRE(v.build_ofs == 30007);
    REQUIRE(v.build_len == 35528);
  }

  SUBCASE("long numeric pre-release identifiers") {
    std::string const s = "1.0.0-1" + std::string(65528, '0');
    REQUIRE(s.size() == 65535);
    REQUIRE(parse_ok(s).pre_len == 65529);
  }
}

TEST_CASE("nsv_parse: *out is untouched on error") {
  nsv_semver_t v;
  unsigned char before[sizeof(v)];
  std::memset(&v, 0xA5, sizeof(v));
  std::memcpy(before, &v, sizeof(v));

  std::string const too_long = "1.2.3-" + std::string(70000, 'a');
  REQUIRE(nsv_parse(&v, nullptr, 5) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_parse(&v, "1.2", 3) == NSV_RET_ERR_SYNTAX);
  REQUIRE(nsv_parse(&v, "1.2.3-", 6) == NSV_RET_ERR_SYNTAX);
  REQUIRE(nsv_parse(&v, "1.2.3+", 6) == NSV_RET_ERR_SYNTAX);
  REQUIRE(nsv_parse(&v, "1.2.3x", 6) == NSV_RET_ERR_SYNTAX);
  REQUIRE(nsv_parse(&v, "4294967296.0.0", 14) == NSV_RET_ERR_OVERFLOW);
  REQUIRE(nsv_parse(&v, too_long.data(), too_long.size()) == NSV_RET_ERR_TOO_LONG);
  REQUIRE(!std::memcmp(&v, before, sizeof(v)));
}

TEST_CASE("nsv_parse: overwrites every field on success") {
  nsv_semver_t v;
  std::memset(&v, 0xA5, sizeof(v));
  std::string const s = "1.2.3";
  REQUIRE(nsv_parse(&v, s.data(), s.size()) == NSV_RET_OK);
  REQUIRE(v.major == 1);
  REQUIRE(v.minor == 2);
  REQUIRE(v.patch == 3);
  REQUIRE(v.src == s.data());
  REQUIRE(v.pre_ofs == 0);
  REQUIRE(v.pre_len == 0);
  REQUIRE(v.build_ofs == 0);
  REQUIRE(v.build_len == 0);
}
