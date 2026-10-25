// Linked against nanosemver.c built with -DNSV_DIAG=1: what nsv_diag_set_print gets.
#include "../corpus.h"
#include "../nsv_test.h"
#include "diag.h"
#include "nanosemver.hpp"

#include <cstring>
#include <string>
#include <string_view>

using nsv_diag::capture;
using nsv_diag::expect;
using nsv_diag::message_ok;

namespace {

int const k_syntax = NSV_RET_ERR_SYNTAX, k_overflow = NSV_RET_ERR_OVERFLOW,
          k_too_long = NSV_RET_ERR_TOO_LONG;

// What each call prints for s, which sits against a guard page so any over-read faults,
// checked by message_ok as it goes.
std::string parse_msg(std::string_view s, int *rc = nullptr) {
  static nsv_test::guarded g;
  capture cap;
  nsv_semver_t v{};
  int const r = nsv_parse(&v, g.place(s), s.size());
  std::string m = cap.take();
  INFO("\"", s, "\" -> ", r, ":\n", m);
  REQUIRE(message_ok(m, "version", s, r));
  if (rc) {
    *rc = r;
  }
  return m;
}

std::string prefix_msg(std::string_view s, int *rc = nullptr) {
  static nsv_test::guarded g;
  capture cap;
  nsv_semver_t v{};
  size_t n = 0;
  int const r = nsv_parse_prefix(&v, g.place(s), s.size(), &n);
  std::string m = cap.take();
  INFO("\"", s, "\" -> ", r, ":\n", m);
  REQUIRE(message_ok(m, "version", s, r));
  if (rc) {
    *rc = r;
  }
  return m;
}

std::string range_msg(std::string_view s, int *rc = nullptr) {
  static nsv_test::guarded g;
  capture cap;
  nsv_semver_t const v{};
  char const *const p = g.place(s);
  int const r = nsv_range_match(p, s.size(), &v, 0);
  std::string m = cap.take();
  INFO("\"", s, "\" -> ", r, ":\n", m);
  REQUIRE(message_ok(m, "range", s, r));
  REQUIRE(nsv_range_match(p, s.size(), &v, NSV_RANGE_EXCLUDE_PRE) == r);
  REQUIRE(cap.take() == m);
  if (rc) {
    *rc = r;
  }
  return m;
}

struct msg_case {
  std::string in;
  int rc;
  size_t column;
  char const *why;
};

std::string huge_pre() { return "1.0.0-" + std::string(65530, 'a'); }

}  // namespace

TEST_CASE("diag: nothing without a printer, on success, or for NULL arguments") {
  nsv_semver_t v{};
  size_t n = 0;
  REQUIRE(nsv_parse(&v, "1.2", 3) == NSV_RET_ERR_SYNTAX);  // no printer yet
  {
    capture cap;
    REQUIRE(nsv_parse(&v, "1.2.3", 5) == NSV_RET_OK);
    REQUIRE(nsv_parse_prefix(&v, "1.2.3x", 6, &n) == NSV_RET_OK);
    REQUIRE(nsv_range_match(">=1.0.0", 7, &v, 0) == 1);
    REQUIRE(nsv_range_match("<1.0.0", 6, &v, 0) == 0);
    REQUIRE(nsv_parse(nullptr, "1.2", 3) == NSV_RET_ERR_ARG);
    REQUIRE(nsv_parse(&v, nullptr, 3) == NSV_RET_ERR_ARG);
    REQUIRE(nsv_parse_prefix(&v, "1.2", 3, nullptr) == NSV_RET_ERR_ARG);
    REQUIRE(nsv_parse_prefix(nullptr, "1.2", 3, &n) == NSV_RET_ERR_ARG);
    REQUIRE(nsv_range_match(nullptr, 3, &v, 0) == NSV_RET_ERR_ARG);
    REQUIRE(nsv_range_match("1.2", 3, nullptr, 0) == NSV_RET_ERR_ARG);
    REQUIRE(cap.take().empty());
    REQUIRE(nsv_parse(&v, "1.2", 3) == NSV_RET_ERR_SYNTAX);
    REQUIRE(!cap.take().empty());
  }
  REQUIRE(nsv_range_match("1.2.", 4, &v, 0) == NSV_RET_ERR_SYNTAX);  // unset again
  capture cap;
  nsv_diag_set_print(nullptr, nullptr);
  REQUIRE(nsv_range_match("1.2.", 4, &v, 0) == NSV_RET_ERR_SYNTAX);
  REQUIRE(cap.take().empty());
}

TEST_CASE("diag: nsv_parse") {
  std::string const tab_x = std::string("1.2.3\t") + "x";
  msg_case const cases[] = {
    // The numbers.
    { "", k_syntax, 1, "expected a number" },
    { "a.b.c", k_syntax, 1, "expected a number" },
    { " 1.2.3", k_syntax, 1, "expected a number" },
    { "v1.2.3", k_syntax, 1, "expected a number" },
    { "1.", k_syntax, 3, "expected a number" },
    { "1..2", k_syntax, 3, "expected a number" },
    { "1.2.", k_syntax, 5, "expected a number" },
    { "1.2.x", k_syntax, 5, "expected a number" },
    { "1", k_syntax, 2, "expected '.'" },
    { "1.2", k_syntax, 4, "expected '.'" },
    { "1-2.3", k_syntax, 2, "expected '.'" },
    { "12.34-5", k_syntax, 6, "expected '.'" },
    { "01.2.3", k_syntax, 1, "leading zero" },
    { "1.02.3", k_syntax, 3, "leading zero" },
    { "1.2.03", k_syntax, 5, "leading zero" },
    { "1.2.00", k_syntax, 5, "leading zero" },
    { "0.0.0", NSV_RET_OK, 0, nullptr },
    // Past them.
    { "1.2.3-01", k_syntax, 7, "leading zero" },
    { "1.2.3-rc.00", k_syntax, 10, "leading zero" },
    { "1.2.3-0.01.x", k_syntax, 9, "leading zero" },
    { "1.2.3.4", k_syntax, 6, "more than three components" },
    { "1.2.3.", k_syntax, 6, "more than three components" },
    { "1.2.3-", k_syntax, 7, "expected a pre-release" },
    { "1.2.3-.", k_syntax, 7, "expected a pre-release" },
    { "1.2.3-+b", k_syntax, 7, "expected a pre-release" },
    { "1.2.3-rc.", k_syntax, 10, "expected an identifier" },
    { "1.2.3-rc..1", k_syntax, 10, "expected an identifier" },
    { "1.2.3+b..c", k_syntax, 9, "expected an identifier" },
    { "1.2.3-a+b.", k_syntax, 11, "expected an identifier" },
    { "1.2.3+", k_syntax, 7, "expected build metadata" },
    { "1.2.3+.", k_syntax, 7, "expected build metadata" },
    { "1.2.3-rc+", k_syntax, 10, "expected build metadata" },
    { "1.2.3+b+c", k_syntax, 8, "unexpected '+'" },
    { "1.2.3-a+b+", k_syntax, 10, "unexpected '+'" },
    { "1.2.3 ", k_syntax, 6, "unexpected ' '" },
    { "1.2.3_4", k_syntax, 6, "unexpected '_'" },
    { "1.2.3a", k_syntax, 6, "unexpected 'a'" },
    { "1.2.3-rc.1+b_", k_syntax, 13, "unexpected '_'" },
    { tab_x, k_syntax, 6, "unexpected '\\x09'" },
    { "1.2.3\n", k_syntax, 6, "unexpected '\\x0a'" },
    { "1.2.3\x7f", k_syntax, 6, "unexpected '\\x7f'" },
    { "1.2.3\xff", k_syntax, 6, "unexpected '\\xff'" },
    { std::string("1.2.3\0", 6), k_syntax, 6, "unexpected '\\x00'" },
    { "1.2.3'", k_syntax, 6, "unexpected '''" },
    // Beyond the limits, which a syntax error beats.
    { "4294967296.0.0", k_overflow, 1, "number over 4294967295" },
    { "1.4294967296.0", k_overflow, 3, "number over 4294967295" },
    { "1.2.4294967296", k_overflow, 5, "number over 4294967295" },
    { "1.2.99999999999-rc", k_overflow, 5, "number over 4294967295" },
    { "4294967295.4294967296.4294967296", k_overflow, 12, "number over 4294967295" },
    { "4294967300.0.0", k_overflow, 1, "number over 4294967295" },
    { "4294967296.0.0x", k_syntax, 15, "unexpected 'x'" },
    { huge_pre(), k_too_long, 1, "version longer than 65535 bytes" },
    { huge_pre() + "+", k_syntax, 65538, "expected build metadata" },
  };
  for (msg_case const &c : cases) {
    int rc = 0;
    std::string const m = parse_msg(c.in, &rc);
    INFO("\"", c.in, "\"");
    REQUIRE(rc == c.rc);
    REQUIRE(m == (c.why ? expect("version", c.in, c.column - 1, c.why) : ""));
  }
}

TEST_CASE("diag: nsv_parse_prefix, whose only syntax error is in the numbers") {
  msg_case const cases[] = {
    { "", k_syntax, 1, "expected a number" },
    { "x", k_syntax, 1, "expected a number" },
    { "1.2", k_syntax, 4, "expected '.'" },
    { "1.2-rc", k_syntax, 4, "expected '.'" },
    { "1.2.x", k_syntax, 5, "expected a number" },
    { "01.2.3", k_syntax, 1, "leading zero" },
    { "1.02.3", k_syntax, 3, "leading zero" },
    { "4294967296.0.0 and more", k_overflow, 1, "number over 4294967295" },
    { "1.2.99999999999,", k_overflow, 5, "number over 4294967295" },
    { huge_pre() + " x", k_too_long, 1, "version longer than 65535 bytes" },
    { "1.2.03", NSV_RET_OK, 0, nullptr },
    { "1.2.3.4", NSV_RET_OK, 0, nullptr },
    { "1.2.3-", NSV_RET_OK, 0, nullptr },
    { "1.2.3 x", NSV_RET_OK, 0, nullptr },
  };
  for (msg_case const &c : cases) {
    int rc = 0;
    std::string const m = prefix_msg(c.in, &rc);
    INFO("\"", c.in, "\"");
    REQUIRE(rc == c.rc);
    REQUIRE(m == (c.why ? expect("version", c.in, c.column - 1, c.why) : ""));
  }
}

TEST_CASE("diag: nsv_range_match") {
  std::string const huge = huge_pre();
  msg_case const cases[] = {
    // No version where one has to be.
    { ">=1.0.0 <", k_syntax, 10, "expected a version" },
    { "1.0.0 ||", k_syntax, 9, "expected a version" },
    { "|| 1.0.0", k_syntax, 1, "expected a version" },
    { "1.0.0 || || 2.0.0", k_syntax, 10, "expected a version" },
    { "1.0.0 |||| 2.0.0", k_syntax, 9, "expected a version" },
    { ">=1.0.0 && <2.0.0", k_syntax, 9, "expected a version" },
    { "1.0.0 or 2.0.0", k_syntax, 7, "expected a version" },
    { "v", k_syntax, 2, "expected a version" },
    { "vv1.2.3", k_syntax, 2, "expected a version" },
    { "-1", k_syntax, 1, "expected a version" },
    { "1.0.0 || -1", k_syntax, 10, "expected a version" },
    { "1.0.0 v-1", k_syntax, 8, "expected a version" },
    { ">=-1", k_syntax, 3, "expected a version" },
    { "\xe2\x89\xa5" "1.0", k_syntax, 1, "expected a version" },
    { "", k_syntax, 1, "empty range; use * for any version" },
    { " \t ", k_syntax, 1, "empty range; use * for any version" },
    { "1.0.0 - 2.0.0", k_syntax, 7, "hyphen ranges are not supported; use >= and <=" },
    { "1 -2", k_syntax, 3, "hyphen ranges are not supported; use >= and <=" },
    { "1, - 2", k_syntax, 4, "hyphen ranges are not supported; use >= and <=" },
    { "1,-2", k_syntax, 3, "expected a version" },
    { "1.2 \t- 2.3", k_syntax, 6, "hyphen ranges are not supported; use >= and <=" },
    // Operators and separators.
    { "!1.2.3", k_syntax, 2, "expected '=' after '!'" },
    { "!", k_syntax, 2, "expected '=' after '!'" },
    { ">=1 !2", k_syntax, 6, "expected '=' after '!'" },
    { "1.0.0 | 2.0.0", k_syntax, 7, "expected '||'" },
    { "1.0.0|", k_syntax, 6, "expected '||'" },
    { "1.0.0,", k_syntax, 7, "expected a comparator after ','" },
    { "1.0.0 , ", k_syntax, 9, "expected a comparator after ','" },
    { "1.0.0, || 2.0.0", k_syntax, 8, "expected a comparator after ','" },
    { "1.0.0,|| 2.0.0", k_syntax, 7, "expected a comparator after ','" },
    { "~>1.2", k_syntax, 3, "expected a whole version after '~>'" },
    { "~=1", k_syntax, 3, "expected a whole version after '~='" },
    { "==1.x", k_syntax, 3, "expected a whole version after '=='" },
    { "== 1.2", k_syntax, 4, "expected a whole version after '=='" },
    { "~> v1.2", k_syntax, 5, "expected a whole version after '~>'" },
    { "==1.2x", k_syntax, 3, "expected a whole version after '=='" },
    { "==1.2.x.4", k_syntax, 3, "expected a whole version after '=='" },
    { "~>x.x.x.1", k_syntax, 3, "expected a whole version after '~>'" },
    // Unless what reads as a partial version is a whole one with a leading zero.
    { "==01", k_syntax, 3, "leading zero" },
    { "==1.02.3", k_syntax, 5, "leading zero" },
    { "~>01.2.3", k_syntax, 3, "leading zero" },
    { "!=1.02.3", k_syntax, 5, "leading zero" },
    { "==x1", k_syntax, 4, "number after a wildcard" },
    { "!=1.2", k_syntax, 3, "expected a whole version or a wildcard after '!='" },
    { ">=1 !=4294967296", k_syntax, 7, "expected a whole version or a wildcard after '!='" },
    // Partial versions.
    { "1.x.3", k_syntax, 5, "number after a wildcard" },
    { "x.1", k_syntax, 3, "number after a wildcard" },
    { "x.9", k_syntax, 3, "number after a wildcard" },
    { "*.*.1", k_syntax, 5, "number after a wildcard" },
    { "x1", k_syntax, 2, "number after a wildcard" },
    { "1.X1", k_syntax, 4, "number after a wildcard" },
    { "1.", k_syntax, 3, "expected a number or a wildcard" },
    { "1..x", k_syntax, 3, "expected a number or a wildcard" },
    { ">=1.2.", k_syntax, 7, "expected a number or a wildcard" },
    { "1.-2", k_syntax, 3, "expected a number or a wildcard" },
    { "1.:", k_syntax, 3, "expected a number or a wildcard" },
    { "1.2-beta", k_syntax, 4, "pre-release or build on a partial version" },
    { "1.x+b", k_syntax, 4, "pre-release or build on a partial version" },
    { "*-0", k_syntax, 2, "pre-release or build on a partial version" },
    { "x.x.x.x", k_syntax, 6, "more than three components" },
    { "1.2.x.4", k_syntax, 6, "more than three components" },
    { "1.2x", k_syntax, 4, "unexpected 'x'" },
    // Whole versions, run into what follows them.
    { "1.2.3x", k_syntax, 6, "unexpected 'x'" },
    { "1.2.3_<2", k_syntax, 6, "unexpected '_'" },
    { "1.0.0+b+c", k_syntax, 8, "unexpected '+'" },
    { "1.0.0\n", k_syntax, 6, "unexpected '\\x0a'" },
    { "1.2.3.4", k_syntax, 6, "more than three components" },
    { "01.2.3", k_syntax, 1, "leading zero" },
    { "01", k_syntax, 1, "leading zero" },
    { ">=01", k_syntax, 3, "leading zero" },
    { "1.2.03", k_syntax, 5, "leading zero" },
    { "1.0.02.0.0", k_syntax, 5, "leading zero" },
    { "1.2.3-01", k_syntax, 7, "leading zero" },
    { "1.2.3-rc.", k_syntax, 10, "expected an identifier" },
    { "1.2.3+b. 1.0.0", k_syntax, 9, "expected an identifier" },
    { "1.0.0-<2", k_syntax, 7, "expected a pre-release" },
    { "1.0.0+<2", k_syntax, 7, "expected build metadata" },
    { "1.0.0-rc+", k_syntax, 10, "expected build metadata" },
    // Beyond the limits: the first, unless a syntax error comes after.
    { ">=4294967296", k_overflow, 3, "number over 4294967295" },
    { "1 || 1.4294967296.x", k_overflow, 8, "number over 4294967295" },
    { "<4294967296.0.0 || 1.2.", k_syntax, 24, "expected a number or a wildcard" },
    { "^1.0.0 ~0.4294967296.0", k_overflow, 11, "number over 4294967295" },
    { "!=4294967296.x", k_overflow, 3, "number over 4294967295" },
    { "<" + huge, k_too_long, 2, "version longer than 65535 bytes" },
    { "4294967296.x || " + huge, k_overflow, 1, "number over 4294967295" },
    { huge + " || ^4294967296", k_too_long, 1, "version longer than 65535 bytes" },
    { "1.0.0 || >=" + huge, k_too_long, 12, "version longer than 65535 bytes" },
    { "1 || <" + huge, k_too_long, 7, "version longer than 65535 bytes" },
    { "1.x " + huge, k_too_long, 5, "version longer than 65535 bytes" },
    { huge + "_", k_syntax, 65537, "unexpected '_'" },
  };
  for (msg_case const &c : cases) {
    int rc = 0;
    std::string const m = range_msg(c.in, &rc);
    INFO("\"", c.in, "\"");
    REQUIRE(rc == c.rc);
    REQUIRE(m == expect("range", c.in, c.column - 1, c.why));
  }
}

TEST_CASE("diag: the message: column, window, tabs, escapes and pieces") {
  // Columns that carry a digit: the fault after k comparators of 6 bytes.
  for (size_t k : { 1u, 2u, 16u, 17u, 166u, 167u, 1666u, 1667u, 16666u }) {
    std::string r;
    for (size_t i = 0; i < k; ++i) {
      r += "1.0.0 ";
    }
    r.insert(r.size() - 1, "y");
    std::string const m = range_msg(r);
    REQUIRE(m == expect("range", r, r.size() - 2, "unexpected 'y'"));
  }
  // Up to 32 bytes either side of the fault, then ...
  for (size_t before = 28; before < 38; ++before) {
    for (size_t after = 28; after < 38; ++after) {
      std::string const r = std::string(before, ' ') + "y" + std::string(after, ' ');
      std::string const m = range_msg(r);
      INFO(before, " ", after, ":\n", m);
      REQUIRE(m == expect("range", r, before, "expected a version"));
      REQUIRE((m.find("\n  ...") != std::string::npos) == (before > 32));
      REQUIRE((m.find("...\n") != std::string::npos) == (after >= 32));
    }
  }
  // Tabs line the caret up under the fault; other bytes after it show as \xHH.
  std::string const tabs = ">=1.0.0\t\t<2.0.0 \t\x01\xfe";
  REQUIRE(range_msg(tabs) == "bad range at column 18: expected a version\n"
                             "  >=1.0.0\t\t<2.0.0 \t\\x01\\xfe\n"
                             "         \t\t       \t^\n");
  // A long message comes in pieces, each a whole number of bytes of it.
  capture cap;
  std::string const r = std::string(60, ' ') + "y";
  nsv_semver_t const v{};
  REQUIRE(nsv_range_match(r.data(), r.size(), &v, 0) == NSV_RET_ERR_SYNTAX);
  REQUIRE(cap.pieces() > 1);
  REQUIRE(cap.take() == expect("range", r, 60, "expected a version"));
  REQUIRE(nsv_range_match("y", 1, &v, 0) == NSV_RET_ERR_SYNTAX);
  REQUIRE(cap.pieces() == 1);
  REQUIRE(cap.take() == "bad range at column 1: expected a version\n  y\n  ^\n");
}

TEST_CASE("diag: from C++, through nanosemver.hpp") {
  capture cap;
  int rc = 0;
  nsv::SemVer const v = nsv::SemVer::from("1.2", rc);
  REQUIRE(rc == NSV_RET_ERR_SYNTAX);
  REQUIRE(cap.take() == "bad version at column 4: expected '.'\n  1.2\n     ^\n");
  REQUIRE(v.matches("~>1.2") == NSV_RET_ERR_SYNTAX);
  REQUIRE(cap.take() == expect("range", "~>1.2", 2, "expected a whole version after '~>'"));
}

TEST_CASE("diag: every short string, as a version, a prefix and a range") {
  // Every string of up to 5 bytes from each alphabet, each checked by message_ok.
  for (char const *alpha : { "01.-+a ", "10.x*-+<=!~>^| ,v" }) {
    size_t const na = std::strlen(alpha);
    bool const version = (na < 8);
    std::string s;
    for (size_t len = 0; len <= (version ? 5u : 4u); ++len) {
      size_t total = 1;
      for (size_t i = 0; i < len; ++i) {
        total *= na;
      }
      for (size_t k = 0; k < total; ++k) {
        s.clear();
        for (size_t i = 0, rest = k; i < len; ++i, rest /= na) {
          s += alpha[rest % na];
        }
        if (version) {
          parse_msg(s);
          prefix_msg(s);
        }
        range_msg(s);
      }
    }
  }
}

TEST_CASE("diag: printing changes no result") {
  auto const same_parse = [](std::string_view s) {
    nsv_semver_t a, b;
    std::memset(&a, 0xA5, sizeof a);
    std::memset(&b, 0xA5, sizeof b);
    size_t na = 7, nb = 7;
    int const ra = nsv_parse(&a, s.data(), s.size());
    int const pa = nsv_parse_prefix(&a, s.data(), s.size(), &na);
    capture cap;
    REQUIRE(nsv_parse(&b, s.data(), s.size()) == ra);
    REQUIRE(nsv_parse_prefix(&b, s.data(), s.size(), &nb) == pa);
    REQUIRE(!std::memcmp(&a, &b, sizeof a));
    REQUIRE(na == nb);
  };
  for (char const *s : corpus::k_valid) {
    same_parse(s);
  }
  for (char const *s : corpus::k_overflow) {
    same_parse(s);
  }
  for (char const *s : corpus::k_invalid) {
    same_parse(s);
  }
  for (char const *r : corpus::k_ranges) {
    for (char const *s : corpus::k_valid) {
      std::string const vs = s;
      nsv_semver_t const v = nsv_test::parse_ok(vs);
      for (unsigned flags : { 0u, unsigned(NSV_RANGE_EXCLUDE_PRE) }) {
        int const want = nsv_range_match(r, std::strlen(r), &v, flags);
        capture cap;
        REQUIRE(nsv_range_match(r, std::strlen(r), &v, flags) == want);
      }
    }
  }
}
