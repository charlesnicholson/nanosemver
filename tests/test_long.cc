// Long pre-release and build sections, exhaustively: every byte value at every position,
// every length up to 40, leading zeros at every offset, and comparisons that differ at
// every position. Every input goes through the fuzz property check: the reference
// implementation, format, copy and comparison, against a guard page.
#include "fuzz/check.h"
#include "nsv_doctest.h"

#include <cstdint>
#include <string>

namespace {

void run(std::string const &in) {
  char const *const f =
      nsv_fuzz::check(reinterpret_cast<std::uint8_t const *>(in.data()), in.size());
  INFO("input: \"", in, "\"");
  REQUIRE(f == nullptr);
}

void run2(std::string const &a, std::string const &b) { run(a + "\n" + b); }

// n identifier bytes cycling through every class: digits, both cases, hyphens.
std::string ident(size_t n, size_t phase = 0) {
  static char const k[] = "a1B-z9Z0m5Q";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += k[(i + phase) % (sizeof(k) - 1)];
  }
  return s;
}

std::string digits(size_t n, char first = '1') {
  std::string s(n, '7');
  if (n) {
    s[0] = first;
  }
  return s;
}

}  // namespace

TEST_CASE("long: every byte at every position of a long section") {
  std::string const sections[] = {
    "-" + ident(40),
    "+" + ident(40),
    "-" + ident(9) + "." + ident(17, 3) + "." + digits(11),
    "+" + digits(23, '0') + "." + ident(16, 5),
    "-" + ident(20) + "+" + ident(20, 7),
  };
  for (std::string const &sec : sections) {
    std::string const base = "1.2.3" + sec;
    for (size_t i = 6; i < base.size(); ++i) {
      for (int c = 0; c < 256; ++c) {
        if (c == '\n') {
          continue;
        }
        std::string s = base;
        s[i] = static_cast<char>(c);
        run(s);
      }
    }
  }
}

TEST_CASE("long: every section length up to 40") {
  for (size_t n = 1; n <= 40; ++n) {
    for (size_t phase = 0; phase < 3; ++phase) {
      std::string const id = ident(n, phase);
      run("1.2.3-" + id);
      run("1.2.3+" + id);
      run("1.2.3-" + id + "+" + id);
      run("1.2.3-" + digits(n));
      run("1.2.3+" + digits(n, '0'));
      run("1.2.3-" + id + ".");
      run("1.2.3-" + id + "..x");
      run("1.2.3+" + id + "_");
      run("1.2.3-" + id + "\xc3\xa9");
      run("1.2.3-x." + id);
      run("1.2.3-" + ident(n % 7 + 1) + "." + id + "." + ident(n % 5 + 1));
    }
  }
}

TEST_CASE("long: leading zeros and numeric identifiers at every offset") {
  for (size_t at = 0; at <= 24; ++at) {
    std::string const head = at ? ident(at) + "." : "";
    for (size_t n = 1; n <= 20; ++n) {
      run("1.2.3-" + head + digits(n, '0'));  // invalid past one digit
      run("1.2.3-" + head + digits(n, '0') + "a");
      run("1.2.3-" + head + digits(n, '0') + ".x");
      run("1.2.3-" + head + digits(n, '1') + "." + ident(17));
      run("1.2.3+" + head + digits(n, '0') + "." + ident(17));
    }
  }
}

TEST_CASE("long: comparisons that differ at every position") {
  char const subs[] = { '0', '1', '9', 'a', 'z', 'A', 'Z', '-', '.' };
  std::string const bases[] = {
    ident(40),
    ident(9) + "." + digits(19) + "." + ident(12),
    digits(17) + "." + digits(17),
  };
  for (std::string const &base : bases) {
    std::string const a = "1.0.0-" + base;
    run2(a, a);
    for (size_t i = 6; i < a.size(); ++i) {
      for (char c : subs) {
        std::string b = a;
        b[i] = c;
        run2(a, b);
        run2(a, b.substr(0, i));
        run2(a.substr(0, i) + c, a);
      }
    }
  }
}
