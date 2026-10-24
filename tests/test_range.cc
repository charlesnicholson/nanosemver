#include "nsv_test.h"
#include "range_cases.h"
#include "ref_semver.h"

#include <iterator>
#include <string>
#include <vector>

namespace {

// nsv_range_match on range, which sits against a guard page so any over-read faults.
int match(std::string_view range, std::string const &version, unsigned flags = 0) {
  static nsv_test::guarded g;
  nsv_semver_t const v = nsv_test::parse_ok(version);
  return nsv_range_match(g.place(range), range.size(), &v, flags);
}

using range_cases::match_case;

void check(match_case const &c) {
  INFO("\"", c.range, "\" against ", c.version);
  REQUIRE(match(c.range, c.version) == c.plain);
  REQUIRE(match(c.range, c.version, NSV_RANGE_EXCLUDE_PRE) == c.exclude_pre);
}

}  // namespace

TEST_CASE("nsv_range_match: NULL arguments") {
  nsv_semver_t const v{};
  REQUIRE(nsv_range_match(nullptr, 5, &v, 0) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_range_match("1.0.0", 5, nullptr, 0) == NSV_RET_ERR_ARG);
}

TEST_CASE("nsv_range_match: Envy's option ranges") {
  for (match_case const &c : range_cases::k_envy) {
    check(c);
  }
  REQUIRE(match(">>>bad", "1.0.0") == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("nsv_range_match: comparison operators") {
  // Each operator against versions below, at, and above 1.2.3, and its pre-release.
  struct {
    char const *op;
    int below, pre, at, above;
  } const ops[] = {
    { "", 0, 0, 1, 0 },   { "=", 0, 0, 1, 0 }, { "==", 0, 0, 1, 0 }, { "<", 1, 1, 0, 0 },
    { "<=", 1, 1, 1, 0 }, { ">", 0, 0, 0, 1 }, { ">=", 0, 0, 1, 1 }, { "!=", 1, 1, 0, 1 },
  };
  for (auto const &o : ops) {
    std::string const r = std::string(o.op) + "1.2.3";
    INFO(r);
    REQUIRE(match(r, "1.2.2") == o.below);
    REQUIRE(match(r, "1.2.3-rc.1") == o.pre);
    REQUIRE(match(r, "1.2.3") == o.at);
    REQUIRE(match(r, "1.2.3+build") == o.at);
    REQUIRE(match(r, "1.2.4-0") == o.above);
    REQUIRE(match(r, "1.2.4") == o.above);
    REQUIRE(match(r, "2.0.0") == o.above);
  }
}

TEST_CASE("nsv_range_match: build metadata never matters") {
  check({ "=1.2.3+a", "1.2.3+b", 1, 1 });
  check({ "1.2.3+a", "1.2.3", 1, 1 });
  check({ "<1.2.3+a", "1.2.3+0", 0, 0 });
  check({ ">=1.2.3-rc+a", "1.2.3-rc+b", 1, 1 });
}

TEST_CASE("nsv_range_match: tilde and caret") {
  for (match_case const &c : range_cases::k_tilde_caret) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: sets and alternatives") {
  for (match_case const &c : range_cases::k_sets) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: other grammars' spellings") {
  for (match_case const &c : range_cases::k_other) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: spacing") {
  for (char const *r : { ">=1.0.0 <2.0.0", " >=1.0.0 <2.0.0", ">=1.0.0 <2.0.0 ",
                         ">=1.0.0   <2.0.0", ">=1.0.0\t<2.0.0", " \t>=1.0.0 \t <2.0.0\t ",
                         ">= 1.0.0 < 2.0.0", ">=\t1.0.0 <  2.0.0", ">=1.0.0 <2.0.0 || 9.9.9",
                         ">=1.0.0 <2.0.0||9.9.9", ">=1.0.0 <2.0.0 ||9.9.9",
                         ">=1.0.0 <2.0.0||  9.9.9",
                         // Commas, and no space at all before an operator.
                         ">=1.0.0,<2.0.0", ">=1.0.0, <2.0.0", ">=1.0.0 ,<2.0.0",
                         ">=1.0.0 , <2.0.0", ">=1.0.0\t,\t<2.0.0", ">= 1.0.0 ,\t< 2.0.0",
                         ">=1.0.0<2.0.0", ">= 1.0.0<2.0.0", ">=1.0.0< 2.0.0",
                         ">=1.0.0<2.0.0||9.9.9", ">=1.0.0,<2.0.0 || 9.9.9",
                         ">=1.0.0<2.0.0 ||9.9.9", " >=1.0.0<2.0.0 ",
                         // A v before the versions.
                         ">=v1.0.0 <v2.0.0", ">= v1.0.0 <2.0.0", ">=v1.0.0<v2.0.0 v*",
                         ">=v1.0.0,<v2.0.0 || v9.9.9" }) {
    INFO("\"", r, "\"");
    REQUIRE(match(r, "1.5.0") == 1);
    REQUIRE(match(r, "2.5.0") == 0);
  }
  REQUIRE(match("^ 1.2.3", "1.9.0") == 1);
  REQUIRE(match("~ 1.2.3", "1.3.0") == 0);
  REQUIRE(match("= 1.2.3", "1.2.3") == 1);
}

TEST_CASE("nsv_range_match: syntax errors") {
  for (char const *r : {
           // Empty.
           "", " ", "\t", "||", " || ", "1.0.0 ||", "|| 1.0.0", "1.0.0 || || 2.0.0",
           // Not a version, whole or partial.
           "1.2.3.4", "01.2.3", "1.2.03", "1.2.3-", "1.2.3+", "1.2.3-01", "1.", "1.2.",
           ".1", "1..2", "01", "1.02", "01.x", "1.2x", "1x", "x1", "xx", "**", "x.1",
           "*.1.2", "1.x.3", "1.2.x.x", "1.2.3.x", "1.2-beta", "1.x-beta", "1.2+b",
           "1.2.x-rc", "*-0", "-1", "+", ".", "1.2.3 .x", "x.x.x.x", "*.*.*.*", "x.",
           ".x", "x-1", "1.x.x-rc", "1.2.x+b", "0.0.0.0", "00", "00.x", "0.00", "1. 2",
           "1 .2", "x .x", "1.2.3.4.5", "1.x.0", "*1", "1*", "1.*2",
           // Operators no grammar has, or split by a space.
           "!1.2.3", "! =1.2.3", "!==1.2.3", "=!1.2.3", "=>1.2.3", "=<1.2.3", "<>1.2.3",
           ">>1.2.3", "<<1.2.3", "^=1.2.3", "^^1.2.3", "~^1.2.3", "> =1.2.3", "< =1.2.3",
           "!", ">=<1.0.0", "<=>1.0.0", "^~1.2.3", "!=!=1.2.3", "!= =1.2.3", "~~1", "^^1",
           ">^1.2.3", "<~1.2.3", "=^1.2.3", "!^1.2.3", "!~1", "=<1", ">==1.2.3", "===1.2.3",
           "= =1.2.3", "~ >1.2.3", "~ =1.2.3", "~=>1.2.3", "~>=1.2.3", "~==1.2.3", "~>>1.2.3",
           "^>1.2.3", "=~1.2.3", "==~1.2.3", "~=~1.2.3",
           // Other grammars' operators on a partial version, which they read otherwise.
           "==1.2", "==1", "==1.x", "==*", "== 1.2", "~=1.2", "~=1", "~=1.x", "~>1.2",
           "~>1", "~>1.x", "~>*", "~> 1.2", "==v1.2", "~>v1",
           // != on a partial version without a wildcard, which elsewhere means not X.Y.0.
           "!=1", "!=1.2", "!= 1.2", "!=0", "!=0.0", "!=v1.2", "!=4294967296",
           // A v anywhere but just before a version.
           "V1.2.3", "vv1.2.3", "v 1.2.3", "vvx", "v.1", "v", ">=v", ">= v", "v-1", "xv",
           "1.2.3v", "v1.2.3v", "=vv1.2.3", ">v=1.2.3", "v>=1.2.3", "1.2.3-rc v",
           // An operator with no version.
           "<", "<=", ">", ">=", "=", "^", "~", "!=", ">=1.0.0 <", "1.0.0 || >=",
           ">= || 1.0.0", "1.0.0 || ^", "~ || ~", "!= ", "^\t",
           // Bars.
           "1.0.0 || |2.0.0", "1.0.0| |2.0.0", "1.0.0 |||| 2.0.0", "|1.0.0", "1.0.0|",
           "| |", "|||", "1.0.0 ||\t|| 2.0.0",
           // Separators no grammar has.
           ">=1.0.0 && <2.0.0", "1.0.0 | 2.0.0", "1.0.0|2.0.0", "1.0.0 ||| 2.0.0",
           "1.0.0 or 2.0.0", "1.0.0\n", "1.0.0\r", "1.0.0\v2.0.0", "1.0.0;2.0.0",
           // A comma anywhere but between two comparators.
           ",", " , ", ",1.0.0", ", 1.0.0", "1.0.0,", "1.0.0 ,", "1.0.0,,2.0.0",
           "1.0.0, ,2.0.0", ">=,1.0.0", ">= ,1.0.0", "1.0.0, || 2.0.0", "1.0.0,|| 2.0.0",
           "1.0.0 ||, 2.0.0", "1.0.0 || ,2.0.0", "1.0.0,||2.0.0", "1.0.0||,2.0.0", ",||",
           // A version runs into whatever follows it but an operator.
           "1.0.02.0.0", "1.0.0x", "1.0.0.x", "*1", "x1", "1.xx", "1.2.3a", "1.2.3_<2",
           "1.0.0>", "1.0.0<=", "1.0.0!", "1.0.0!1.0.0", "1.0.0-<2", "1.0.0+<2",
           // Hyphen ranges.
           "1.0.0 - 2.0.0", "1.0.0 -2.0.0", "1.2 - 2.3", "1 - 2",
           // Trailing junk.
           ">=1.0.0 y", ">=1.0.0 v", "1.0.0 |" }) {
    INFO("\"", r, "\"");
    REQUIRE(match(r, "1.0.0") == NSV_RET_ERR_SYNTAX);
    REQUIRE(match(r, "1.0.0", NSV_RANGE_EXCLUDE_PRE) == NSV_RET_ERR_SYNTAX);
  }
}

TEST_CASE("nsv_range_match: a syntax error anywhere, even after a match") {
  REQUIRE(match("1.0.0 || 2.0.", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("* || 1.x.3", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("1.0.0 || ", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("<1.0.0 >=1.0.0 y", "1.0.0") == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("nsv_range_match: versions beyond the limits") {
  std::string const huge = "1.0.0-" + std::string(65530, 'a');
  REQUIRE(match("<4294967296.0.0", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("1.0.0 || <4294967296.0.0", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("^1.0.0 ~0.4294967296.0", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("<" + huge, "1.0.0") == NSV_RET_ERR_TOO_LONG);
  REQUIRE(match("1.0.0 || >=" + huge, "1.0.0") == NSV_RET_ERR_TOO_LONG);
  // The first such error is the one returned; a syntax error anywhere beats them all.
  REQUIRE(match("<4294967296.0.0 || " + huge, "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match(huge + " <4294967296.0.0", "1.0.0") == NSV_RET_ERR_TOO_LONG);
  REQUIRE(match("<4294967296.0.0 || 1.2.", "1.0.0") == NSV_RET_ERR_SYNTAX);
  // Partial versions too.
  REQUIRE(match(">=4294967296", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("1.4294967296.x", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("^" + std::string(100, '9'), "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("1.x || <4294967296", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("4294967296.x.1", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("4294967295.4294967295.x", "4294967295.4294967295.4294967295") == 1);
  // The first error in the range, partial or whole, is the one returned.
  REQUIRE(match("4294967296.x || " + huge, "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match(huge + " || ^4294967296", "1.0.0") == NSV_RET_ERR_TOO_LONG);
  REQUIRE(match(">4294967296 <4294967296.0.0", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("1.x 4294967296.x 1.x.3", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("!=4294967296.x", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("!=4294967296", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("==4294967296.0.0", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("~>4294967296.0", "1.0.0") == NSV_RET_ERR_SYNTAX);
  REQUIRE(match("~=" + huge, "1.0.0") == NSV_RET_ERR_TOO_LONG);
  REQUIRE(match(">=1.0.0," + huge, "1.0.0") == NSV_RET_ERR_TOO_LONG);
  REQUIRE(match(">=1.0.0<4294967296.0.0", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("~4294967295.4294967296", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match("^" + std::string(100000, '1') + ".x", "1.0.0") == NSV_RET_ERR_OVERFLOW);
  REQUIRE(match(huge + "_", "1.0.0") == NSV_RET_ERR_SYNTAX);
  // A long range is fine as long as each version fits.
  std::string many;
  for (int i = 0; i < 10000; ++i) {
    many += (i ? " || " : "") + std::to_string(i) + ".0.0";
  }
  REQUIRE(many.size() > 65535);
  REQUIRE(match(many, "9999.0.0") == 1);
  REQUIRE(match(many, "10000.0.0") == 0);
}

TEST_CASE("nsv_range_match: pre-releases") {
  for (match_case const &c : range_cases::k_pre) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: as npm answers, and where it differs") {
  // node-semver's default excludes pre-releases as NSV_RANGE_EXCLUDE_PRE does, and its
  // includePrerelease ranks them as flags 0 does.
  for (match_case const &c : range_cases::k_npm) {
    check(c);
  }
  for (match_case const &c : range_cases::k_npm_differs) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: partial versions and wildcards, as npm answers") {
  for (match_case const &c : range_cases::k_partial) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: partial version numbers around UINT32_MAX, in each place") {
  // Every number from 4294967280 to 4294967319 and ten times those, as the major or the
  // minor, bare or followed by wildcards, behind every operator: the ones that fit match
  // their own line, and the rest overflow.
  for (uint64_t base : { uint64_t{ 4294967280u }, uint64_t{ 42949672800u } }) {
    for (uint64_t n = base; n < base + 40; ++n) {
      std::string const num = std::to_string(n);
      bool const fits = (n <= 0xFFFFFFFFu);
      for (int place = 0; place < 2; ++place) {
        std::string const line = place ? ("7." + num) : num;
        std::string const on = place ? ("7." + num + ".0") : (num + ".9.9");
        for (char const *tail : { "", ".x", ".*" }) {
          if (place && *tail && (std::string(tail) == ".*")) {
            continue;
          }
          for (char const *op : { "", "=", ">=", "<=", ">", "<", "^", "~", "!=" }) {
            std::string const r = std::string(op) + line + tail;
            INFO(r);
            if ((std::string(op) == "!=") && !*tail) {
              REQUIRE(match(r, "1.0.0") == NSV_RET_ERR_SYNTAX);
              continue;
            }
            if (!fits) {
              REQUIRE(match(r, "1.0.0") == NSV_RET_ERR_OVERFLOW);
              continue;
            }
            int const expect = (!*op || (std::string(op) == "=") || (std::string(op) == ">=") ||
                                (std::string(op) == "<=") || (std::string(op) == "^") ||
                                (std::string(op) == "~"))
                                   ? 1
                                   : 0;
            REQUIRE(match(r, on) == expect);
          }
        }
      }
    }
  }
}

TEST_CASE("nsv_range_match: partial versions past UINT32_MAX") {
  for (match_case const &c : range_cases::k_partial_max) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: !=") {
  for (match_case const &c : range_cases::k_not) {
    check(c);
  }
}

TEST_CASE("nsv_range_match: every byte between comparators and before a version") {
  for (int c = 0; c < 256; ++c) {
    char const ch = static_cast<char>(c);
    bool const space = (ch == ' ') || (ch == '\t');
    INFO("byte ", c);
    // Between two comparators: nothing, or a space, a tab or a comma.
    REQUIRE((match(std::string(">=1.0.0") + ch + "<2.0.0", "1.5.0") >= 0) ==
            (space || (ch == ',')));
    // Before a version: an operator, a space, a tab, a v or the version's own first digit.
    bool const lead = space || (ch == '<') || (ch == '>') || (ch == '=') || (ch == '^') ||
                      (ch == '~') || (ch == 'v') || ((ch >= '1') && (ch <= '9'));
    REQUIRE((match(std::string(1, ch) + "1.2.3", "1.2.3") >= 0) == lead);
    // Between a version and the next operator: nothing, a space, a tab or a comma, or
    // a digit that lengthens the version.
    REQUIRE((match(std::string("1.2.3") + ch + ">=1.0.0", "1.2.3") >= 0) ==
            (space || (ch == ',') || ((ch >= '0') && (ch <= '9'))));
    // After a version: the end, a space, a tab, or a '|' that starts "||".
    REQUIRE((match(std::string("1.2.3") + ch + "|1.0.0", "1.0.0") >= 0) == (ch == '|'));
  }
}

namespace {

// Every range of up to max_k comparators drawn from ops x vers, with AND or OR between
// each two, checked against every test version in both modes. The answer is built from
// the range's own structure, OR over sets of AND over comparators, from what each
// comparator alone answers according to the reference; a comparator the reference
// rejects makes any range with it a syntax error.
void every_range(std::vector<char const *> const &ops, std::vector<char const *> const &vers,
                 size_t max_k) {
  static char const *const tests[] = {
    "0.0.0-rc",  "0.0.0",     "0.0.2",      "0.0.3-rc",   "0.0.3", "0.0.4",
    "0.2.3-beta", "0.2.9",    "0.3.0",      "0.9.9",      "1.0.0-0", "1.0.0",
    "1.2.0-beta", "1.2.2",    "1.2.3-rc.1", "1.2.3-rc.2", "1.2.3", "1.2.9",
    "1.3.0-0",   "1.9.0-rc",  "1.9.0",      "2.0.0-0",    "2.0.0-rc.1", "2.0.0",
  };
  size_t const nv = vers.size(), nt = std::size(tests), nc = ops.size() * nv;

  std::vector<std::string> const tstr(std::begin(tests), std::end(tests));
  std::vector<nsv_semver_t> tv;
  std::vector<ref::version> trv(nt);
  for (size_t t = 0; t < nt; ++t) {
    tv.push_back(nsv_test::parse_ok(tstr[t]));
    REQUIRE(ref::parse(tstr[t], &trv[t]) == ref::status::ok);
  }
  // sat[m][c][t]: comparator c alone admits test t, in mode m (1 excluding pre-releases).
  // names[c][t]: c names a pre-release on t's MAJOR.MINOR.PATCH.
  std::vector<std::vector<bool>> sat[2], names(nc, std::vector<bool>(nt));
  std::vector<bool> valid(nc);
  for (int m = 0; m < 2; ++m) {
    sat[m].assign(nc, std::vector<bool>(nt));
    for (size_t c = 0; c < nc; ++c) {
      std::string const term = std::string(ops[c / nv]) + vers[c % nv];
      for (size_t t = 0; t < nt; ++t) {
        bool pass = false, named = false;
        ref::status const st = ref::term_match(term, trv[t], m == 1, &pass, &named);
        REQUIRE(((st == ref::status::ok) || (st == ref::status::syntax)));
        valid[c] = (st == ref::status::ok);
        sat[m][c][t] = pass;
        names[c][t] = named;
      }
    }
  }

  static nsv_test::guarded g;
  // Between two comparators of a set; "" only before an operator.
  static char const *const ands[] = { " ", "\t", "  ", " \t ", ",", ", ", " ,\t", "" };
  static char const *const ors[] = { "||", " || ", "\t||", "||  " };
  for (size_t k = 1; k <= max_k; ++k) {
    size_t combos = 1;
    for (size_t i = 0; i < k; ++i) {
      combos *= nc;
    }
    for (size_t combo = 0; combo < combos; ++combo) {
      size_t cs[3];
      for (size_t i = 0, rest = combo; i < k; ++i, rest /= nc) {
        cs[i] = rest % nc;
      }
      for (unsigned bounds = 0; bounds < (1u << (k - 1)); ++bounds) {
        // Spacing varies with the indices, so every kind turns up everywhere.
        size_t const vary = combo + bounds;
        std::string r = (vary % 5 == 0) ? " " : "";
        bool ok = true;
        for (size_t i = 0; i < k; ++i) {
          char const *const op = ops[cs[i] / nv], *const ver = vers[cs[i] % nv];
          if (i) {
            char const *const a = ands[(vary + i) % 8];
            r += ((bounds >> (i - 1)) & 1u) ? ors[(vary + i) % 4] : ((*a || *op) ? a : " ");
          }
          r += op;
          r += ((vary + i) % 3 == 0) ? " " : "";
          r += (((vary + i) % 5 == 2) && (*ver >= '0') && (*ver <= '9')) ? "v" : "";
          r += ver;
          ok = ok && valid[cs[i]];
        }
        r += (vary % 7 == 0) ? "\t" : "";
        char const *const pr = g.place(r);

        for (size_t t = 0; t < nt; ++t) {
          for (int m = 0; m < 2; ++m) {
            bool any = false, all = true, pre_ok = false;
            for (size_t i = 0; i < k; ++i) {
              all = all && sat[m][cs[i]][t];
              pre_ok = pre_ok || names[cs[i]][t];
              if ((i + 1 == k) || ((bounds >> i) & 1u)) {
                any = any || (all && (!m || !trv[t].has_pre || pre_ok));
                all = true;
                pre_ok = false;
              }
            }
            unsigned const flags = m ? unsigned(NSV_RANGE_EXCLUDE_PRE) : 0u;
            int const got = nsv_range_match(pr, r.size(), &tv[t], flags);
            int const want = ok ? int(any) : int(NSV_RET_ERR_SYNTAX);
            if (got != want) {
              INFO("\"", r, "\" against ", tstr[t], m ? ", excluding pre-releases" : "");
              REQUIRE(got == want);
            }
          }
        }
      }
    }
  }
}

}  // namespace

TEST_CASE("nsv_range_match: every range of up to three comparators") {
  every_range({ "", "=", "<", "<=", ">", ">=", "^", "~" },
              { "0.0.3", "0.2.3", "1.2.3", "1.2.3-rc.1", "2.0.0-0" }, 3);
}

TEST_CASE("nsv_range_match: every range of up to two comparators, partial versions too") {
  every_range({ "", "=", "<", "<=", ">", ">=", "^", "~", "!=", "==", "~=", "~>" },
              { "0.0.3", "1.2.3", "1.2.3-rc.1", "1.2", "1", "1.x", "*", "0.2", "0", "0.0" },
              2);
}

TEST_CASE("nsv_range_match: every byte at every position, and every truncation") {
  std::string const bases[] = {
    ">=1.2.3-rc.1 <2.0.0",
    "^0.2.3 || ~1.2.3\t=2.0.0-0+b",
    " 1.2.3 ||>1.2.3 <=1.9.0-x.7 ",
    "<0.0.3||^0.0.3-a",
    ">=v1.2.3,~>1.2.4<2||!=1.x ==0.2.5",
  };
  std::string const vs[] = { "1.2.3", "1.2.3-rc.2", "0.2.5", "1.9.0-x.7", "0.0.3-b" };
  static nsv_test::guarded g;
  auto check = [&](std::string const &r) {
    char const *const pr = g.place(r);
    for (std::string const &vs1 : vs) {
      nsv_semver_t const v = nsv_test::parse_ok(vs1);
      ref::version rv;
      REQUIRE(ref::parse(vs1, &rv) == ref::status::ok);
      for (bool excl : { false, true }) {
        bool m = false;
        ref::status const st = ref::range_match(r, rv, excl, &m);
        int const want = (st == ref::status::ok)       ? int(m)
                         : (st == ref::status::syntax) ? int(NSV_RET_ERR_SYNTAX)
                         : (st == ref::status::overflow) ? int(NSV_RET_ERR_OVERFLOW)
                                                         : int(NSV_RET_ERR_TOO_LONG);
        unsigned const flags = excl ? unsigned(NSV_RANGE_EXCLUDE_PRE) : 0u;
        int const got = nsv_range_match(pr, r.size(), &v, flags);
        if (got != want) {
          INFO("\"", r, "\" against ", vs1, excl ? ", excluding pre-releases" : "");
          REQUIRE(got == want);
        }
      }
    }
  };
  for (std::string const &base : bases) {
    for (size_t n = 0; n <= base.size(); ++n) {
      check(base.substr(0, n));
      check(base.substr(n));
    }
    for (size_t i = 0; i < base.size(); ++i) {
      for (int c = 0; c < 256; ++c) {
        std::string r = base;
        r[i] = static_cast<char>(c);
        check(r);
        check(base.substr(0, i) + static_cast<char>(c) + base.substr(i));
      }
    }
  }
}

TEST_CASE("nsv_range_match: v as a view, a copy and a literal") {
  static nsv_semver_t const lit = NSV_SEMVER_LIT(1, 2, 3, "rc.1", "b");
  std::string const s = "1.2.3-rc.1+b";
  nsv_semver_t const view = nsv_test::parse_ok(s);
  nsv_test::blob const copy = nsv_test::copy(view);
  for (nsv_semver_t const *v : { &view, static_cast<nsv_semver_t const *>(copy.get()), &lit }) {
    REQUIRE(nsv_range_match(">=1.2.3-rc.0 <1.2.3", 19, v, 0) == 1);
    REQUIRE(nsv_range_match(">=1.2.3-rc.0 <1.2.3", 19, v, NSV_RANGE_EXCLUDE_PRE) == 1);
    REQUIRE(nsv_range_match("^1.0.0", 6, v, 0) == 1);
    REQUIRE(nsv_range_match("^1.0.0", 6, v, NSV_RANGE_EXCLUDE_PRE) == 0);
    REQUIRE(nsv_range_match("=1.2.3-rc.1+x", 13, v, 0) == 1);
  }
}

TEST_CASE("nsv_range_match: a zero-initialized version is 0.0.0") {
  nsv_semver_t const v{};
  for (char const *r : { "0.0.0", "*", ">=0", "0", "0.x", "^0", "~0.0", "<0.0.1", "<=0.0.0",
                         ">=0.0.0-0 <0.0.1-0", "!=1.x", "0.0.0 || 1.0.0", "==0.0.0",
                         "~>0.0.0", ">=0.0.0,<0.0.1", "v0.0.0" }) {
    INFO(r);
    std::string const rs = r;
    REQUIRE(nsv_range_match(rs.data(), rs.size(), &v, 0) == 1);
    REQUIRE(nsv_range_match(rs.data(), rs.size(), &v, NSV_RANGE_EXCLUDE_PRE) == 1);
  }
  for (char const *r : { "<0.0.0", ">0.0.0", "!=0.0.0", "!=0.x", "<0", ">0", "1", "^0.0.1",
                         "==0.0.1", "~=0.1.0", ">0.0.0<1.0.0" }) {
    INFO(r);
    std::string const rs = r;
    REQUIRE(nsv_range_match(rs.data(), rs.size(), &v, 0) == 0);
  }
}

TEST_CASE("nsv_range_match: NULL with a zero length is still NULL") {
  nsv_semver_t const v{};
  REQUIRE(nsv_range_match(nullptr, 0, &v, 0) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_range_match(nullptr, 0, nullptr, 0) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_range_match("", 0, &v, 0) == NSV_RET_ERR_SYNTAX);
}

TEST_CASE("nsv_range_match: unknown flags are ignored") {
  REQUIRE(match(">=1.0.0 <2.0.0", "1.5.0-rc", 0xFFFFFFFEu) == 1);
  REQUIRE(match(">=1.0.0 <2.0.0", "1.5.0-rc", 0xFFFFFFFFu) == 0);
}

TEST_CASE("nsv_range_match: long ranges") {
  SUBCASE("spaces") {
    std::string const sp(70000, ' ');
    REQUIRE(match(sp + ">=" + sp + "1.0.0" + sp + "<2.0.0" + sp, "1.5.0") == 1);
    REQUIRE(match(sp + "||" + sp + "1.0.0", "1.0.0") == NSV_RET_ERR_SYNTAX);
    REQUIRE(match(sp, "1.0.0") == NSV_RET_ERR_SYNTAX);
  }

  SUBCASE("many comparators in one set") {
    std::string r;
    for (int i = 0; i < 5000; ++i) {
      r += ">=0.0." + std::to_string(i) + " ";
    }
    REQUIRE(match(r, "0.0.4999") == 1);
    REQUIRE(match(r, "0.0.4998") == 0);
    REQUIRE(match(r + "<0.0.4999", "0.0.4999") == 0);
  }

  SUBCASE("the longest version a range can hold, and one more byte") {
    std::string const v = "1.0.0-" + std::string(65529, 'a');
    REQUIRE(match(">=" + v + " || 0.0.0", "1.0.0") == 1);
    REQUIRE(match(">=" + v + "a || 0.0.0", "1.0.0") == NSV_RET_ERR_TOO_LONG);
  }
}
