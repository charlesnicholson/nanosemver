// Writes the cases for tests/npm/judge.js, one JSON array per line:
//   [range, version, nsv_range_match(range, version, 0), ...(NSV_RANGE_EXCLUDE_PRE)]
// for every range nanosemver accepts: the unit tests' tables, the corpus ranges against
// the corpus versions,
// every range of up to two comparators built from a few operators and versions, whole
// and partial, against versions around them, and generated ranges from the fuzz
// generator. Ranges with !=, which npm has not, go too; the judge counts them aside. The
// other grammars' spellings go as they are, and the judge respells them as npm's.
//
//   nsv_npm_emit [generated-count [seed]]
#include "../corpus.h"
#include "../fuzz/gen.h"
#include "../range_cases.h"
#include "nanosemver.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string json(std::string const &s) {
  std::string out = "\"";
  for (char const ch : s) {
    unsigned char const c = static_cast<unsigned char>(ch);
    if ((ch == '"') || (ch == '\\')) {
      out += '\\';
      out += ch;
    } else if ((c < 0x20) || (c >= 0x7f)) {
      char hex[8];
      std::snprintf(hex, sizeof(hex), "\\u%04x", c);
      out += hex;
    } else {
      out += ch;
    }
  }
  return out + "\"";
}

unsigned long emitted = 0;

void emit(std::string const &range, std::string const &version) {
  nsv_semver_t v;
  if (nsv_parse(&v, version.data(), version.size()) != NSV_RET_OK) {
    return;
  }
  int const plain = nsv_range_match(range.data(), range.size(), &v, 0);
  int const excl = nsv_range_match(range.data(), range.size(), &v, NSV_RANGE_EXCLUDE_PRE);
  if (plain < 0) {
    return;  // npm may take what nanosemver does not
  }
  std::printf("[%s,%s,%d,%d]\n", json(range).c_str(), json(version).c_str(), plain, excl);
  ++emitted;
}

}  // namespace

int main(int argc, char **argv) {
  unsigned long const generated = (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 200000;
  nsv_fuzz::rng r((argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 1);

  // The unit tests' own tables, so their answers are npm's.
  for (range_cases::table const &t : range_cases::k_all) {
    for (unsigned i = 0; i < t.n; ++i) {
      emit(t.rows[i].range, t.rows[i].version);
    }
  }
  for (char const *range : corpus::k_ranges) {
    for (char const *version : corpus::k_valid) {
      emit(range, version);
    }
  }

  static char const *const ops[] = { "", "=", "<", "<=", ">", ">=", "^", "~", "==", "~=", "~>" };
  // Between two comparators of a set, in turn; "" only before an operator.
  static char const *const ands[] = { " ", ", ", ",", "" };
  size_t turn = 0;
  static char const *const vers[] = { "0.0.0", "0.0.3", "0.2.3", "1.2.3", "1.2.3-rc.1",
                                      "2.0.0-0", "1.2", "1", "1.x", "*", "0.2", "0", "0.0" };
  static char const *const tests[] = { "0.0.0-rc", "0.0.0", "0.0.3-rc", "0.0.3", "0.0.4",
                                       "0.2.3-beta", "0.2.9", "0.3.0", "1.2.0-beta",
                                       "1.2.3-rc.1", "1.2.3-rc.2", "1.2.3", "1.3.0-0",
                                       "1.9.0-rc", "2.0.0-rc.1", "2.0.0" };
  for (char const *o1 : ops) {
    for (char const *v1 : vers) {
      std::string const c1 = std::string(o1) + v1;
      for (char const *t : tests) {
        emit(c1, t);
      }
      for (char const *o2 : ops) {
        for (char const *v2 : vers) {
          char const *const a = ands[turn++ % 4];
          for (char const *sep : { (*a || *o2) ? a : " ", " || " }) {
            for (char const *t : tests) {
              emit(c1 + sep + o2 + v2, t);
            }
          }
        }
      }
    }
  }

  // Ranges come from about a third of the generator's inputs; generated-count of them on
  // top of the cases above.
  for (unsigned long const fixed = emitted; (emitted - fixed) < generated;) {
    std::string const in = nsv_fuzz::gen_input(r);
    size_t const nl = in.find('\n');
    if (nl != std::string::npos) {
      emit(in.substr(0, nl), in.substr(nl + 1));
    }
  }
  return 0;
}
