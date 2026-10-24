// Standalone fuzz driver: needs no libFuzzer, so it runs with any compiler, MSVC
// included. Generates inputs from a seed, or replays files (e.g. libFuzzer crashes).
//
//   nsv_fuzz [-n iterations] [-t seconds] [-s seed]   generate; stops at either limit
//   nsv_fuzz --emit-corpus DIR                        write seed inputs for libFuzzer
//   nsv_fuzz FILE...                                  replay
#include "../corpus.h"
#include "check.h"
#include "gen.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string escaped(std::string const &s) {
  std::string out;
  for (char const ch : s) {
    unsigned char const c = static_cast<unsigned char>(ch);
    if ((c >= 0x20) && (c < 0x7f) && (c != '\\')) {
      out += ch;
    } else {
      char hex[8];
      std::snprintf(hex, sizeof(hex), "\\x%02x", c);
      out += hex;
    }
  }
  return out;
}

bool run(std::string const &in, char const *label) {
  char const *const f =
      nsv_fuzz::check(reinterpret_cast<std::uint8_t const *>(in.data()), in.size());
  if (f) {
    std::fprintf(stderr, "FAIL (%s): %s\n  input (%zu bytes): \"%s\"\n", label, f,
                 in.size(), escaped(in).c_str());
  }
  return !f;
}

bool write_file(std::string const &path, std::string const &data) {
  std::ofstream f(path, std::ios::binary);
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

int emit_corpus(char const *dir) {
  std::vector<std::string> inputs;
  for (char const *s : corpus::k_valid) inputs.emplace_back(s);
  for (char const *s : corpus::k_overflow) inputs.emplace_back(s);
  for (char const *s : corpus::k_invalid) inputs.emplace_back(s);
  for (char const *s : corpus::k_ranges) inputs.push_back(std::string(s) + "\n1.2.3-rc.1");
  nsv_fuzz::rng r(1);
  for (int i = 0; i < 500; ++i) {
    std::string s = nsv_fuzz::gen_input(r);
    if (s.size() < 4096) {
      inputs.push_back(std::move(s));
    }
  }
  for (size_t i = 0; i < inputs.size(); ++i) {
    std::string const path = std::string(dir) + "/seed-" + std::to_string(i);
    if (!write_file(path, inputs[i])) {
      std::fprintf(stderr, "cannot write %s\n", path.c_str());
      return 1;
    }
  }
  std::fprintf(stderr, "wrote %zu seed inputs to %s\n", inputs.size(), dir);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  unsigned long long iterations = 0, seed = 1;
  double seconds = 0;
  std::vector<char const *> files;

  for (int i = 1; i < argc; ++i) {
    bool const has_value = (i + 1 < argc);
    if (!std::strcmp(argv[i], "-n") && has_value) {
      iterations = std::strtoull(argv[++i], nullptr, 10);
    } else if (!std::strcmp(argv[i], "-s") && has_value) {
      seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (!std::strcmp(argv[i], "-t") && has_value) {
      seconds = std::strtod(argv[++i], nullptr);
    } else if (!std::strcmp(argv[i], "--emit-corpus") && has_value) {
      return emit_corpus(argv[++i]);
    } else if (argv[i][0] == '-') {
      std::fprintf(stderr, "usage: %s [-n iterations] [-t seconds] [-s seed] | "
                   "--emit-corpus DIR | FILE...\n", argv[0]);
      return 2;
    } else {
      files.push_back(argv[i]);
    }
  }

  if (!files.empty()) {
    for (char const *path : files) {
      std::ifstream f(path, std::ios::binary);
      if (!f) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return 2;
      }
      std::string const in((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      if (!run(in, path)) {
        return 1;
      }
    }
    std::printf("replayed %zu file(s), all passed\n", files.size());
    return 0;
  }

  if (!iterations && (seconds <= 0)) {
    iterations = 1000000;
  }
  auto const start = std::chrono::steady_clock::now();
  nsv_fuzz::rng r(seed);
  unsigned long long i = 0;
  for (;; ++i) {
    if (iterations && (i >= iterations)) {
      break;
    }
    if ((seconds > 0) && !(i & 1023)) {
      std::chrono::duration<double> const el = std::chrono::steady_clock::now() - start;
      if (el.count() >= seconds) {
        break;
      }
    }
    std::string const in = nsv_fuzz::gen_input(r);
    if (!run(in, "generated")) {
      std::fprintf(stderr, "  seed %llu, iteration %llu\n", seed, i);
      return 1;
    }
  }
  std::chrono::duration<double> const el = std::chrono::steady_clock::now() - start;
  std::printf("%llu inputs (seed %llu) in %.1fs, all passed\n", i, seed, el.count());
  return 0;
}
