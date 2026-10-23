#include "corpus.h"
#include "nsv_test.h"
#include "ref_semver.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using nsv_test::cmp;
using nsv_test::parse_ok;

namespace {

// Each strictly greater than the one before.
std::vector<std::string> const k_ascending = {
  "0.0.0",
  "0.0.1",
  "0.0.9",
  "0.0.10",
  "0.1.0",
  "0.10.0",
  "1.0.0-0",
  "1.0.0-0.0",
  "1.0.0-0.a",
  "1.0.0-1",
  "1.0.0-2",
  "1.0.0-10",
  "1.0.0-18446744073709551615",
  "1.0.0-18446744073709551616",
  "1.0.0-100000000000000000000",
  "1.0.0--",
  "1.0.0--.1",
  "1.0.0-0a",
  "1.0.0-A",
  "1.0.0-Z",
  "1.0.0-a",
  "1.0.0-a.1",
  "1.0.0-a.1.0",
  "1.0.0-a.2",
  "1.0.0-a.a",
  "1.0.0-a-",  // a prefix ranks first, so "a" and all its extensions precede "a-"
  "1.0.0-a0",
  "1.0.0-aA",
  "1.0.0-aa",
  // semver.org's own example, item 11.4.4.
  "1.0.0-alpha",
  "1.0.0-alpha.1",
  "1.0.0-alpha.beta",
  "1.0.0-beta",
  "1.0.0-beta.2",
  "1.0.0-beta.11",
  "1.0.0-rc.1",
  "1.0.0",
  "1.0.1-0",
  "1.0.1",
  "1.9.9",
  "2.0.0",
  "2.1.0",
  "2.1.1",
  "4294967295.4294967295.4294967294",
  "4294967295.4294967295.4294967295-0",
  "4294967295.4294967295.4294967295-z",
  "4294967295.4294967295.4294967295",
};

int qsort_cmp(void const *a, void const *b) {
  return nsv_cmp(static_cast<nsv_semver_t const *>(a), static_cast<nsv_semver_t const *>(b)) - 1;
}

int qsort_cmp_ptr(void const *a, void const *b) {
  return nsv_cmp(*static_cast<nsv_semver_t const *const *>(a),
                 *static_cast<nsv_semver_t const *const *>(b)) - 1;
}

}  // namespace

TEST_CASE("nsv_cmp: NULL arguments") {
  std::string const s = "1.2.3";
  nsv_semver_t const v = parse_ok(s);
  REQUIRE(nsv_cmp(nullptr, &v) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_cmp(&v, nullptr) == NSV_RET_ERR_ARG);
  REQUIRE(nsv_cmp(nullptr, nullptr) == NSV_RET_ERR_ARG);
}

TEST_CASE("nsv_cmp: the result values are fixed") {
  REQUIRE(NSV_CMP_LT == 0);
  REQUIRE(NSV_CMP_EQ == 1);
  REQUIRE(NSV_CMP_GT == 2);
  std::string const a = "1.0.0", b = "2.0.0";
  nsv_semver_t const va = parse_ok(a), vb = parse_ok(b);
  REQUIRE(nsv_cmp(&va, &vb) == NSV_CMP_LT);
  REQUIRE(nsv_cmp(&va, &va) == NSV_CMP_EQ);
  REQUIRE(nsv_cmp(&vb, &va) == NSV_CMP_GT);
}

TEST_CASE("nsv_cmp: every pair of an ascending list") {
  std::vector<nsv_semver_t> v;
  for (std::string const &s : k_ascending) {
    v.push_back(parse_ok(s));
  }
  for (size_t i = 0; i < v.size(); ++i) {
    for (size_t j = 0; j < v.size(); ++j) {
      INFO(k_ascending[i], " vs ", k_ascending[j]);
      int const want = (i < j) ? -1 : (i > j) ? 1 : 0;
      REQUIRE(cmp(v[i], v[j]) == want);
    }
  }
}

TEST_CASE("nsv_cmp: each core field decides alone") {
  std::string const s[] = { "1.2.3", "2.0.0", "1.3.0", "1.2.4", "0.9.9", "1.1.9", "1.2.2" };
  nsv_semver_t const base = parse_ok(s[0]);
  for (int i = 1; i <= 3; ++i) {
    INFO(s[i]);
    REQUIRE(cmp(base, parse_ok(s[i])) == -1);
    REQUIRE(cmp(parse_ok(s[i]), base) == 1);
  }
  for (int i = 4; i <= 6; ++i) {
    INFO(s[i]);
    REQUIRE(cmp(base, parse_ok(s[i])) == 1);
    REQUIRE(cmp(parse_ok(s[i]), base) == -1);
  }
}

TEST_CASE("nsv_cmp: a pre-release ranks below its release") {
  for (char const *p : { "1.0.0-0", "1.0.0-rc.1", "1.0.0-zzz", "1.0.0--", "1.0.0-99999" }) {
    std::string const pre = p, rel = "1.0.0";
    INFO(pre);
    REQUIRE(cmp(parse_ok(pre), parse_ok(rel)) == -1);
    REQUIRE(cmp(parse_ok(rel), parse_ok(pre)) == 1);
  }
  // But not below a lower release.
  std::string const pre = "1.0.0-0", lower = "0.9.9";
  REQUIRE(cmp(parse_ok(pre), parse_ok(lower)) == 1);
}

TEST_CASE("nsv_cmp: build metadata is ignored") {
  struct {
    char const *a, *b;
  } const cases[] = {
    { "1.0.0+a", "1.0.0+b" },
    { "1.0.0", "1.0.0+b" },
    { "1.0.0-rc.1+1", "1.0.0-rc.1+2" },
    { "1.0.0-rc.1", "1.0.0-rc.1+build.5" },
    { "1.0.0+zzz.999", "1.0.0+0" },
  };
  for (auto const &c : cases) {
    std::string const a = c.a, b = c.b;
    INFO(a, " vs ", b);
    REQUIRE(cmp(parse_ok(a), parse_ok(b)) == 0);
    REQUIRE(cmp(parse_ok(b), parse_ok(a)) == 0);
  }
}

TEST_CASE("nsv_cmp: pre-release identifier rules") {
  struct {
    char const *lo, *hi;
  } const cases[] = {
    // Numeric identifiers compare numerically, at any width.
    { "1.0.0-2", "1.0.0-10" },
    { "1.0.0-9", "1.0.0-10" },
    { "1.0.0-10", "1.0.0-11" },
    { "1.0.0-99", "1.0.0-100" },
    { "1.0.0-123456789012345678901234567890", "1.0.0-123456789012345678901234567891" },
    { "1.0.0-99999999999999999999999999999", "1.0.0-123456789012345678901234567890" },
    // Numeric ranks below alphanumeric, whatever the characters.
    { "1.0.0-1", "1.0.0-a" },
    { "1.0.0-999", "1.0.0-0a" },
    { "1.0.0-1", "1.0.0--" },
    { "1.0.0-1", "1.0.0-1a" },
    { "1.0.0-a.1", "1.0.0-a.b" },
    { "1.0.0-a.99999", "1.0.0-a.-" },
    // Alphanumeric identifiers compare in ASCII order.
    { "1.0.0--", "1.0.0-0a" },
    { "1.0.0-A", "1.0.0-a" },
    { "1.0.0-Z", "1.0.0-a" },
    { "1.0.0-a", "1.0.0-b" },
    { "1.0.0-a-", "1.0.0-a0" },
    { "1.0.0-a0", "1.0.0-aA" },
    { "1.0.0-aA", "1.0.0-aa" },
    { "1.0.0-alphA", "1.0.0-alpha" },
    { "1.0.0-alpha", "1.0.0-alpha0" },
    { "1.0.0-alpha", "1.0.0-alpha-" },
    { "1.0.0-alpha", "1.0.0-alphb" },
    { "1.0.0-b", "1.0.0-ba" },
    { "1.0.0-ab.z", "1.0.0-b.a" },
    // More identifiers ranks higher when all before are equal.
    { "1.0.0-a", "1.0.0-a.0" },
    { "1.0.0-a.0", "1.0.0-a.a" },
    { "1.0.0-a.b", "1.0.0-a.b.c" },
    { "1.0.0-1.2", "1.0.0-1.2.0" },
    { "1.0.0-a.b.c.d", "1.0.0-a.b.c.d.e" },
    // An early difference wins over a later one.
    { "1.0.0-a.z.z", "1.0.0-b.a" },
    { "1.0.0-1.z", "1.0.0-2.a" },
    { "1.0.0-a.1.z", "1.0.0-a.2" },
  };
  for (auto const &c : cases) {
    std::string const lo = c.lo, hi = c.hi;
    INFO(lo, " < ", hi);
    nsv_semver_t const vl = parse_ok(lo), vh = parse_ok(hi);
    REQUIRE(cmp(vl, vh) == -1);
    REQUIRE(cmp(vh, vl) == 1);
  }
}

TEST_CASE("nsv_cmp: equal versions in different buffers") {
  for (char const *p : { "1.0.0", "1.0.0-a.b.c", "1.0.0-1.2.3", "1.0.0-alpha.1+b" }) {
    std::string const a = p, b = p;
    REQUIRE(a.data() != b.data());
    INFO(a);
    REQUIRE(cmp(parse_ok(a), parse_ok(b)) == 0);
  }
}

TEST_CASE("nsv_cmp: views, copies, literals and zero-init agree") {
  std::string const s = "1.4.0-rc.2+g3f9a1c2";
  nsv_semver_t const view = parse_ok(s);
  nsv_test::blob const c = nsv_test::copy(view);
  nsv_test::blob const cc = nsv_test::copy(*c);
  static nsv_semver_t const lit = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "other.build");
  nsv_semver_t const *const all[] = { &view, c.get(), cc.get(), &lit };
  for (nsv_semver_t const *a : all) {
    for (nsv_semver_t const *b : all) {
      REQUIRE(nsv_cmp(a, b) == NSV_CMP_EQ);
    }
  }

  nsv_semver_t zero{};
  std::string const z = "0.0.0", zp = "0.0.0-0";
  REQUIRE(cmp(zero, parse_ok(z)) == 0);
  REQUIRE(cmp(zero, parse_ok(zp)) == 1);
  REQUIRE(cmp(zero, view) == -1);
}

TEST_CASE("nsv_cmp: a total order over the corpus, agreeing with the reference") {
  std::vector<std::string> strs(std::begin(corpus::k_valid), std::end(corpus::k_valid));
  strs.insert(strs.end(), k_ascending.begin(), k_ascending.end());
  std::vector<nsv_semver_t> v;
  std::vector<ref::version> r;
  for (std::string const &s : strs) {
    v.push_back(parse_ok(s));
    r.emplace_back();
    REQUIRE(ref::parse(s, &r.back()) == ref::status::ok);
  }
  size_t const n = v.size();
  std::vector<int> c(n * n);
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      INFO(strs[i], " vs ", strs[j]);
      c[i * n + j] = cmp(v[i], v[j]);
      REQUIRE(c[i * n + j] == ref::cmp(r[i], r[j]));
      if (i == j) {
        REQUIRE(c[i * n + j] == 0);
      }
    }
  }
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      REQUIRE(c[i * n + j] == -c[j * n + i]);  // antisymmetric
      for (size_t k = 0; k < n; ++k) {
        // transitive: a <= b <= c implies a <= c, and likewise for >=.
        if ((c[i * n + j] <= 0) && (c[j * n + k] <= 0)) {
          REQUIRE(c[i * n + k] <= 0);
        }
        if ((c[i * n + j] >= 0) && (c[j * n + k] >= 0)) {
          REQUIRE(c[i * n + k] >= 0);
        }
      }
    }
  }
}

TEST_CASE("nsv_cmp: sorts with qsort through the documented wrapper") {
  std::vector<nsv_semver_t> v;
  for (std::string const &s : k_ascending) {
    v.push_back(parse_ok(s));
  }
  std::vector<nsv_semver_t> shuffled = v;
  std::reverse(shuffled.begin(), shuffled.end());
  std::rotate(shuffled.begin(), shuffled.begin() + 7, shuffled.end());
  std::qsort(shuffled.data(), shuffled.size(), sizeof(nsv_semver_t), qsort_cmp);
  for (size_t i = 0; i < v.size(); ++i) {
    INFO(k_ascending[i]);
    REQUIRE(shuffled[i].src == v[i].src);
  }
}

TEST_CASE("nsv_cmp: sorts copies through an array of pointers") {
  std::vector<nsv_test::blob> copies;
  for (std::string const &s : k_ascending) {
    copies.push_back(nsv_test::copy(parse_ok(s)));
  }
  std::vector<nsv_semver_t const *> p;
  for (auto it = copies.rbegin(); it != copies.rend(); ++it) {
    p.push_back(it->get());
  }
  std::qsort(p.data(), p.size(), sizeof(p[0]), qsort_cmp_ptr);
  for (size_t i = 0; i < p.size(); ++i) {
    REQUIRE(p[i] == copies[i].get());
    REQUIRE(nsv_test::format(*p[i]) == k_ascending[i]);
  }
}
