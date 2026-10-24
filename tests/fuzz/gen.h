// Deterministic input generation for the standalone fuzz driver and the unit tests.
#pragma once

#include <cstdint>
#include <string>

namespace nsv_fuzz {

// splitmix64: tiny, fast, and the same sequence on every platform.
class rng {
 public:
  explicit rng(std::uint64_t seed) : s_{ seed } {}
  std::uint64_t next() {
    std::uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  std::uint32_t below(std::uint32_t n) {  // [0, n)
    return static_cast<std::uint32_t>(next() % n);
  }
  bool chance(std::uint32_t percent) { return below(100) < percent; }

 private:
  std::uint64_t s_;
};

// One version string: mostly well-formed, biased toward the edges of every rule,
// sometimes mutated into nonsense.
std::string gen_version(rng &r);

// One fuzz input: a version, or two related versions separated by '\n' so the
// comparison has near-misses to rank, or a range and a version near its bounds.
std::string gen_input(rng &r);

}  // namespace nsv_fuzz
