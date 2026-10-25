// A fixed-seed slice of the fuzzer on every test run. `make fuzz` runs it longer.
#include "corpus.h"
#include "fuzz/check.h"
#include "fuzz/gen.h"
#include "nsv_doctest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

void run(std::string const &in) {
  char const *const f =
      nsv_fuzz::check(reinterpret_cast<std::uint8_t const *>(in.data()), in.size());
  INFO("input: \"", in, "\"");
  REQUIRE(f == nullptr);
}

std::vector<std::string> corpus_strings() {
  std::vector<std::string> v;
  for (char const *s : corpus::k_valid) v.emplace_back(s);
  for (char const *s : corpus::k_overflow) v.emplace_back(s);
  for (char const *s : corpus::k_invalid) v.emplace_back(s);
  return v;
}

}  // namespace

TEST_CASE("fuzz: every corpus string, and every pair of them") {
  std::vector<std::string> const all = corpus_strings();
  for (std::string const &a : all) {
    for (std::string const &b : all) {
      run(a + "\n" + b);
    }
  }
}

TEST_CASE("fuzz: every corpus range against every corpus version") {
  for (char const *r : corpus::k_ranges) {
    for (char const *v : corpus::k_valid) {
      run(std::string(r) + "\n" + v);
    }
  }
}

TEST_CASE("fuzz: generated inputs") {
  nsv_fuzz::rng r(20261023);
  for (int i = 0; i < 100000; ++i) {
    run(nsv_fuzz::gen_input(r));
  }
}

TEST_CASE("fuzz: every compiler generates the same inputs") {
  // FNV-1a of 20000 inputs per seed. A mismatch on one compiler means a draw order left
  // to it in gen.cc; a deliberate change to gen.cc changes these too.
  struct {
    std::uint64_t seed, hash;
  } const want[] = { { 1, 0xf68ee11dcf064c0eull },
                     { 0x6e737668707021ull, 0xe6b8d8b1797f415eull } };
  for (auto const &w : want) {
    nsv_fuzz::rng r(w.seed);
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 20000; ++i) {
      for (char c : nsv_fuzz::gen_input(r)) {
        h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ull;
      }
      h = (h ^ 0xffu) * 0x100000001b3ull;  // between inputs
    }
    INFO("seed ", w.seed);
    REQUIRE(h == w.hash);
  }
}
