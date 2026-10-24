// Properties nsv_range_match must have whatever the reference says: its operators agree
// with their definitions in terms of each other, alternatives are unions, spacing and
// order do not matter, ranges of bounds are intervals, and the pre-release flag only
// ever takes matches away. Ranges are drawn at random from operators and versions, whole
// and partial, around the edges of every rule, and checked against every version in a
// pool that straddles them.
#include "fuzz/gen.h"
#include "nsv_test.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace {

char const *const k_pool[] = {
  "0.0.0-0", "0.0.0-rc", "0.0.0", "0.0.1-0", "0.0.1", "0.0.3", "0.1.0-0", "0.1.0", "0.2.0",
  "0.2.3-beta", "0.2.3", "0.2.9", "0.3.0-0", "0.3.0", "0.9.9", "1.0.0-0", "1.0.0-alpha",
  "1.0.0", "1.0.1", "1.1.9", "1.2.0-0", "1.2.0-beta", "1.2.0", "1.2.3-rc.1", "1.2.3-rc.2",
  "1.2.3", "1.2.4", "1.2.9", "1.3.0-0", "1.3.0-a", "1.3.0", "1.4.0", "1.4.1", "1.4.9",
  "1.5.0-0", "1.5.0", "1.9.9", "2.0.0-0", "2.0.0-rc.1", "2.0.0", "2.0.1", "3.0.0",
  "4294967294.4294967295.4294967295", "4294967295.0.0-0", "4294967295.0.0",
  "4294967295.4294967295.0-rc", "4294967295.4294967295.4294967295",
};

char const *const k_full[] = {
  "0.0.0", "0.0.1", "0.0.3", "0.2.3", "0.2.3-beta", "1.0.0", "1.0.0-0", "1.2.0-beta",
  "1.2.3", "1.2.3-rc.1", "1.2.3+b", "1.3.0-0", "1.4.1", "2.0.0", "2.0.0-0",
  "4294967295.0.0", "4294967295.4294967295.4294967295",
};

char const *const k_partial[] = {
  "*", "x", "X", "x.x", "*.*.*", "0", "0.x", "0.0", "0.0.x", "0.2", "0.3", "1", "1.x",
  "1.X", "1.2", "1.2.x", "1.2.*", "1.3", "1.4", "2", "4294967295", "4294967295.4294967295",
  "1.4294967295", "4294967294.4294967295",
};

char const *const k_ops[] = { "", "=", "<", "<=", ">", ">=", "^", "~", "!=" };

struct versions {
  std::vector<std::string> str;
  std::vector<nsv_semver_t> v;
  explicit versions(std::vector<std::string> s) : str(std::move(s)) {
    v.reserve(str.size());
    for (std::string const &x : str) {
      v.push_back(nsv_test::parse_ok(x));
    }
  }
};

versions const &pool() {
  static versions const p{ std::vector<std::string>(std::begin(k_pool), std::end(k_pool)) };
  return p;
}

int match(std::string const &range, nsv_semver_t const &v, unsigned flags) {
  static nsv_test::guarded g;
  return nsv_range_match(g.place(range), range.size(), &v, flags);
}

// Every version in the pool, both ways of ranking pre-releases.
void same_everywhere(std::string const &a, std::string const &b) {
  for (size_t i = 0; i < pool().v.size(); ++i) {
    for (unsigned f : { 0u, unsigned(NSV_RANGE_EXCLUDE_PRE) }) {
      int const x = match(a, pool().v[i], f), y = match(b, pool().v[i], f);
      if (x != y) {
        INFO("\"", a, "\" and \"", b, "\" against ", pool().str[i], f ? ", excluding" : "");
        REQUIRE(x == y);
      }
    }
  }
}

template <typename A>
auto pick(nsv_fuzz::rng &r, A const &a) {
  return a[r.below(static_cast<uint32_t>(std::size(a)))];
}

// A random place in a container of n, as an iterator offset.
std::ptrdiff_t offset(nsv_fuzz::rng &r, size_t n) {
  return static_cast<std::ptrdiff_t>(r.below(static_cast<uint32_t>(n)));
}

std::string comparator(nsv_fuzz::rng &r) {
  std::string const op = pick(r, k_ops);
  if (r.chance(50)) {
    return op + pick(r, k_full);
  }
  return op + pick(r, k_partial);
}

using set = std::vector<std::string>;
using range = std::vector<set>;

range random_range(nsv_fuzz::rng &r) {
  range out(1 + r.below(3));
  for (set &s : out) {
    s.resize(1 + r.below(3));
    for (std::string &c : s) {
      c = comparator(r);
    }
  }
  return out;
}

// One way of writing a range out; spacing varies with style.
std::string render(range const &rg, unsigned style) {
  static char const *const ands[] = { " ", "\t", "   ", " \t " };
  static char const *const ors[] = { "||", " || ", "\t||\t", "||  " };
  std::string s = (style & 1) ? " " : "";
  for (size_t i = 0; i < rg.size(); ++i) {
    s += i ? ors[(style + i) % 4] : "";
    for (size_t j = 0; j < rg[i].size(); ++j) {
      s += j ? ands[(style + j) % 4] : "";
      std::string c = rg[i][j];
      if (style & 4) {  // a space after the operator
        size_t const k = c.find_first_not_of("<>=!^~");
        if (k && (k != std::string::npos)) {
          c.insert(k, " ");
        }
      }
      s += c;
    }
  }
  return s + ((style & 8) ? "\t" : "");
}

// The version after every one starting with the first k of major.minor.patch: 1.2 ->
// 1.3.0, 1.4294967295 -> 2.0.0. Empty if there is none.
std::string next_line(uint64_t major, uint64_t minor, uint64_t patch, size_t k) {
  uint64_t n[3] = { major, minor, patch };
  for (size_t i = k; i < 3; ++i) {
    n[i] = 0;
  }
  for (size_t i = k; i-- > 0;) {
    if (++n[i] <= 0xFFFFFFFFull) {
      return std::to_string(n[0]) + "." + std::to_string(n[1]) + "." + std::to_string(n[2]);
    }
    n[i] = 0;
  }
  return "";
}

}  // namespace

TEST_CASE("range properties: the flag only takes matches away, and never from a release") {
  nsv_fuzz::rng r(101);
  for (int n = 0; n < 3000; ++n) {
    std::string const s = render(random_range(r), r.below(16));
    for (size_t i = 0; i < pool().v.size(); ++i) {
      nsv_semver_t const &v = pool().v[i];
      int const plain = match(s, v, 0), excl = match(s, v, NSV_RANGE_EXCLUDE_PRE);
      INFO("\"", s, "\" against ", pool().str[i]);
      if (plain < 0) {
        REQUIRE(excl == plain);  // errors do not depend on the flag
        continue;
      }
      REQUIRE(((excl == 0) || (excl == 1)));
      REQUIRE(!(excl && !plain));
      if (!v.pre_len) {
        REQUIRE(excl == plain);
      }
    }
  }
}

TEST_CASE("range properties: build metadata on the version never matters") {
  nsv_fuzz::rng r(102);
  std::vector<std::string> with;
  for (std::string const &s : pool().str) {
    with.push_back(s + "+build.7-x");
  }
  versions const built{ with };
  for (int n = 0; n < 2000; ++n) {
    std::string const s = render(random_range(r), r.below(16));
    for (size_t i = 0; i < pool().v.size(); ++i) {
      for (unsigned f : { 0u, unsigned(NSV_RANGE_EXCLUDE_PRE) }) {
        INFO("\"", s, "\" against ", pool().str[i]);
        REQUIRE(match(s, pool().v[i], f) == match(s, built.v[i], f));
      }
    }
  }
}

TEST_CASE("range properties: spacing, order and repetition do not matter") {
  nsv_fuzz::rng r(103);
  for (int n = 0; n < 2000; ++n) {
    range rg = random_range(r);
    std::string const base = render(rg, 0);
    same_everywhere(base, render(rg, 1 + r.below(15)));
    // Comparators within a set, and sets within the range, in another order.
    for (set &s : rg) {
      std::rotate(s.begin(), s.begin() + offset(r, s.size()), s.end());
    }
    std::rotate(rg.begin(), rg.begin() + offset(r, rg.size()), rg.end());
    same_everywhere(base, render(rg, r.below(16)));
    // A comparator twice, a set twice.
    set &s0 = rg[r.below(static_cast<uint32_t>(rg.size()))];
    s0.push_back(s0[r.below(static_cast<uint32_t>(s0.size()))]);
    rg.push_back(rg[r.below(static_cast<uint32_t>(rg.size()))]);
    same_everywhere(base, render(rg, r.below(16)));
  }
}

TEST_CASE("range properties: || is the union of its sets") {
  nsv_fuzz::rng r(104);
  for (int n = 0; n < 2000; ++n) {
    range const rg = random_range(r);
    std::string const whole = render(rg, r.below(16));
    for (size_t i = 0; i < pool().v.size(); ++i) {
      for (unsigned f : { 0u, unsigned(NSV_RANGE_EXCLUDE_PRE) }) {
        int const w = match(whole, pool().v[i], f);
        int any = 0, err = 0;
        for (set const &s : rg) {
          int const one = match(render(range{ s }, 0), pool().v[i], f);
          if ((one < 0) && !err) {
            err = one;
          }
          any |= (one == 1);
        }
        INFO("\"", whole, "\" against ", pool().str[i], f ? ", excluding" : "");
        REQUIRE(w == (err ? err : any));
      }
    }
  }
}

TEST_CASE("range properties: without pre-releases in the way, a set is the AND of its comparators") {
  nsv_fuzz::rng r(105);
  for (int n = 0; n < 2000; ++n) {
    set const s = random_range(r)[0];
    std::string const whole = render(range{ s }, r.below(16));
    for (size_t i = 0; i < pool().v.size(); ++i) {
      int const w = match(whole, pool().v[i], 0);
      int all = 1, err = 0;
      for (std::string const &c : s) {
        int const one = match(c, pool().v[i], 0);
        if ((one < 0) && !err) {
          err = one;
        }
        all &= (one == 1);
      }
      INFO("\"", whole, "\" against ", pool().str[i]);
      REQUIRE(w == (err ? err : all));
    }
  }
}

TEST_CASE("range properties: each operator is its definition") {
  // Whole versions: ^ and ~ are their bounds, <= and >= are < or = and > or =, != is
  // not =, and exactly one of <, = and > holds.
  for (char const *vs : k_full) {
    std::string const v = vs;
    nsv_semver_t const pv = nsv_test::parse_ok(v);
    same_everywhere("=" + v, v);
    same_everywhere("<=" + v, "<" + v + " || " + v);
    same_everywhere(">=" + v, ">" + v + " || " + v);
    std::string const tilde = next_line(pv.major, pv.minor, pv.patch, 2);
    same_everywhere("~" + v, ">=" + v + (tilde.empty() ? "" : " <" + tilde + "-0"));
    std::string const caret =
        next_line(pv.major, pv.minor, pv.patch, pv.major ? 1 : (pv.minor ? 2 : 3));
    same_everywhere("^" + v, ">=" + v + (caret.empty() ? "" : " <" + caret + "-0"));
    for (size_t i = 0; i < pool().v.size(); ++i) {
      nsv_semver_t const &x = pool().v[i];
      INFO(v, " against ", pool().str[i]);
      int const lt = match("<" + v, x, 0), eq = match(v, x, 0), gt = match(">" + v, x, 0);
      REQUIRE(lt + eq + gt == 1);
      REQUIRE(match("!=" + v, x, 0) == 1 - eq);
      if (!pv.pre_len) {  // a release bound names no pre-release, either way
        REQUIRE(match("!=" + v, x, NSV_RANGE_EXCLUDE_PRE) ==
                match("<" + v + " || >" + v, x, NSV_RANGE_EXCLUDE_PRE));
      }
    }
  }

  // Partial versions: a line, and everything below or above it.
  for (char const *ps : k_partial) {
    std::string const p = ps;
    for (size_t i = 0; i < pool().v.size(); ++i) {
      nsv_semver_t const &x = pool().v[i];
      INFO(p, " against ", pool().str[i]);
      int const lt = match("<" + p, x, 0), eq = match(p, x, 0), gt = match(">" + p, x, 0);
      REQUIRE(lt + eq + gt == 1);
      REQUIRE(match("=" + p, x, 0) == eq);
      REQUIRE(match("!=" + p, x, 0) == 1 - eq);
      REQUIRE(match("<=" + p, x, 0) == (lt | eq));
      REQUIRE(match(">=" + p, x, 0) == (gt | eq));
      for (unsigned f : { 0u, unsigned(NSV_RANGE_EXCLUDE_PRE) }) {
        REQUIRE(match("~" + p, x, f) == match(p, x, f));  // ~ on a line is the line
      }
    }
  }

  // Partial versions spelled out as npm writes them: lower bounds X.Y.0-0, or under
  // npm's default X.Y.0, and upper bounds -0.
  struct {
    char const *partial;
    uint64_t major, minor;
    size_t k;
  } const lines[] = { { "1", 1, 0, 1 },        { "1.x", 1, 0, 1 },  { "1.2", 1, 2, 2 },
                      { "1.2.x", 1, 2, 2 },    { "0", 0, 0, 1 },    { "0.0", 0, 0, 2 },
                      { "0.2", 0, 2, 2 },      { "1.3", 1, 3, 2 },  { "2", 2, 0, 1 },
                      { "1.4294967295", 1, 4294967295u, 2 } };
  for (auto const &l : lines) {
    std::string const p = l.partial;
    std::string const low = std::to_string(l.major) + "." + std::to_string(l.minor) + ".0";
    std::string const next = next_line(l.major, l.minor, 0, l.k);
    std::string const up = next.empty() ? std::string("") : (" <" + next + "-0");
    for (size_t i = 0; i < pool().v.size(); ++i) {
      nsv_semver_t const &x = pool().v[i];
      INFO(p, " against ", pool().str[i]);
      REQUIRE(match(p, x, 0) == match(">=" + low + "-0" + up, x, 0));
      REQUIRE(match(">=" + p, x, 0) == match(">=" + low + "-0", x, 0));
      REQUIRE(match("<" + p, x, 0) == match("<" + low + "-0", x, 0));
      if (!next.empty()) {
        REQUIRE(match(">" + p, x, 0) == match(">=" + next + "-0", x, 0));
        REQUIRE(match("<=" + p, x, 0) == match("<" + next + "-0", x, 0));
      }
      if (!x.pre_len) {
        continue;
      }
      // Under npm's default a pre-release needs a comparator naming its line, and a
      // partial version's lower bound is the .0 release: >=1.2 <=1.2.0-z is >=1.2.0
      // <=1.2.0-z, which 1.2.0-beta is not part of.
      std::string const named = " <=" + std::to_string(x.major) + "." +
                                std::to_string(x.minor) + "." + std::to_string(x.patch) +
                                "-zzz";
      unsigned const ex = NSV_RANGE_EXCLUDE_PRE;
      REQUIRE(match(">=" + p + named, x, ex) == match(">=" + low + named, x, ex));
      REQUIRE(match(p + named, x, ex) == match(">=" + low + up + named, x, ex));
      if (!next.empty()) {
        REQUIRE(match(">" + p + named, x, ex) == match(">=" + next + named, x, ex));
      }
    }
  }
}

TEST_CASE("range properties: a set of bounds is an interval of versions") {
  // In precedence order, the versions a set of <, <=, >, >= and = bounds admits are
  // contiguous: there is no gap between two matches.
  std::vector<size_t> order(pool().v.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
    return nsv_cmp(&pool().v[a], &pool().v[b]) == NSV_CMP_LT;
  });
  static char const *const bound_ops[] = { "", "=", "<", "<=", ">", ">=", "^", "~" };
  nsv_fuzz::rng r(106);
  for (int n = 0; n < 5000; ++n) {
    std::string s;
    for (uint32_t j = 0, k = 1 + r.below(3); j < k; ++j) {
      s += j ? " " : "";
      s += pick(r, bound_ops);
      s += r.chance(50) ? pick(r, k_full)
                        : pick(r, k_partial);
    }
    int seen = 0;  // 0 before any match, 1 in a run, 2 after it
    for (size_t i : order) {
      int const m = match(s, pool().v[i], 0);
      REQUIRE(m >= 0);
      if (m && (seen == 2)) {
        INFO("\"", s, "\" admits ", pool().str[i], " after a gap");
        REQUIRE(seen != 2);
      }
      seen = m ? 1 : (seen ? 2 : 0);
    }
  }
}

TEST_CASE("range properties: v as a view, a copy, or relocated") {
  nsv_fuzz::rng r(107);
  for (int n = 0; n < 500; ++n) {
    std::string const s = render(random_range(r), r.below(16));
    for (size_t i = 0; i < pool().v.size(); ++i) {
      nsv_test::blob const c = nsv_test::copy(pool().v[i]);
      for (unsigned f : { 0u, unsigned(NSV_RANGE_EXCLUDE_PRE) }) {
        INFO("\"", s, "\" against ", pool().str[i]);
        REQUIRE(match(s, pool().v[i], f) == match(s, *c, f));
      }
    }
  }
}
