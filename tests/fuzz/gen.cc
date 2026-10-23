#include "gen.h"

#include <vector>

namespace nsv_fuzz {
namespace {

char const k_ident_chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-";
char const k_hot_chars[] = ".-+0189azAZ";

char pick(rng &r, char const *chars, std::uint32_t n) { return chars[r.below(n)]; }

std::string digits(rng &r, std::uint32_t n, bool leading_zero_ok) {
  std::string s;
  for (std::uint32_t i = 0; i < n; ++i) {
    s += static_cast<char>('0' + r.below(10));
  }
  if (!leading_zero_ok && (s.size() > 1) && (s[0] == '0')) {
    s[0] = static_cast<char>('1' + r.below(9));
  }
  return s;
}

std::string number(rng &r) {
  switch (r.below(12)) {
    case 0: return "0";
    case 1: return "4294967295";
    case 2: return "4294967296";
    case 3: return "429496729" + digits(r, 1, true);
    case 4: return digits(r, 11 + r.below(15), false);  // overflows
    case 5: return "0" + digits(r, 1 + r.below(3), true);  // leading zero
    case 6: return digits(r, 10, false);
    default: return digits(r, 1 + r.below(3), false);
  }
}

std::string alnum(rng &r, std::uint32_t n) {
  std::string s;
  for (std::uint32_t i = 0; i < n; ++i) {
    s += pick(r, k_ident_chars, sizeof(k_ident_chars) - 1);
  }
  return s;
}

std::string pre_ident(rng &r) {
  static char const *const words[] = { "alpha", "beta", "rc", "a", "b", "A", "-", "x-y" };
  switch (r.below(10)) {
    case 0: return "0";
    case 1: return "0" + digits(r, 1 + r.below(2), true);  // leading zero: invalid
    case 2: return digits(r, 1 + r.below(25), false);      // numeric, any width
    case 3: return words[r.below(8)];
    case 4: return r.chance(20) ? "" : "0" + alnum(r, 1 + r.below(3));
    default: return alnum(r, 1 + r.below(8));
  }
}

std::string build_ident(rng &r) {
  switch (r.below(4)) {
    case 0: return digits(r, 1 + r.below(6), true);  // leading zeros are fine here
    case 1: return r.chance(10) ? "" : "sha";
    default: return alnum(r, 1 + r.below(8));
  }
}

struct parts {
  std::string major, minor, patch;
  bool has_pre = false, has_build = false;
  std::vector<std::string> pre, build;

  std::string str() const {
    std::string s = major + "." + minor + "." + patch;
    if (has_pre) {
      s += "-";
      for (size_t i = 0; i < pre.size(); ++i) {
        s += (i ? "." : "") + pre[i];
      }
    }
    if (has_build) {
      s += "+";
      for (size_t i = 0; i < build.size(); ++i) {
        s += (i ? "." : "") + build[i];
      }
    }
    return s;
  }
};

parts gen_parts(rng &r) {
  parts p;
  p.major = number(r);
  p.minor = number(r);
  p.patch = number(r);
  p.has_pre = r.chance(60);
  if (p.has_pre) {
    for (std::uint32_t i = 0, n = 1 + r.below(4); i < n; ++i) {
      p.pre.push_back(pre_ident(r));
    }
  }
  p.has_build = r.chance(30);
  if (p.has_build) {
    for (std::uint32_t i = 0, n = 1 + r.below(3); i < n; ++i) {
      p.build.push_back(build_ident(r));
    }
  }
  return p;
}

// A neighbour of p: one field nudged, so a comparison has to look deep to rank them.
parts tweak(rng &r, parts p) {
  switch (r.below(7)) {
    case 0: p.patch = number(r); break;
    case 1: p.has_pre = !p.has_pre; if (p.pre.empty()) p.pre.push_back(pre_ident(r)); break;
    case 2: if (!p.pre.empty()) p.pre.pop_back(); else p.has_pre = false; break;
    case 3: p.has_pre = true; p.pre.push_back(pre_ident(r)); break;
    case 4: if (!p.pre.empty()) p.pre[r.below(static_cast<std::uint32_t>(p.pre.size()))] = pre_ident(r); break;
    case 5: p.has_build = true; p.build.assign(1, build_ident(r)); break;
    default: break;  // identical
  }
  return p;
}

char random_byte(rng &r) {
  // Never '\n': that would split the input into two.
  char c;
  do {
    c = r.chance(50) ? pick(r, k_hot_chars, sizeof(k_hot_chars) - 1)
                     : static_cast<char>(r.below(256));
  } while (c == '\n');
  return c;
}

void mutate(rng &r, std::string &s) {
  for (std::uint32_t i = 0, n = 1 + r.below(3); i < n; ++i) {
    std::uint32_t const len = static_cast<std::uint32_t>(s.size());
    std::uint32_t const at = len ? r.below(len) : 0;
    switch (r.below(6)) {
      case 0: s.insert(at, 1, random_byte(r)); break;
      case 1: if (len) s.erase(at, 1); break;
      case 2: if (len) s[at] = random_byte(r); break;
      case 3: if (len) s.insert(at, s.substr(at, 1 + r.below(4))); break;
      case 4: s.resize(at); break;
      default: if (len > 1) std::swap(s[at], s[(at + 1) % len]); break;
    }
  }
}

// Total length in [65530, 65540], straddling the 65535-byte limit.
std::string near_length_limit(rng &r) {
  std::string s = "1.2.3-" + alnum(r, 3);
  std::size_t const want = 65530 + r.below(11);
  bool const in_build = r.chance(50);
  if (in_build) {
    s += "+";
  }
  while (s.size() < want) {
    s += r.chance(5) ? '.' : pick(r, k_ident_chars, sizeof(k_ident_chars) - 1);
  }
  if (s.back() == '.') {
    s.back() = 'z';
  }
  return s;
}

}  // namespace

std::string gen_version(rng &r) {
  if (r.chance(1) && r.chance(20)) {
    return near_length_limit(r);
  }
  std::string s = gen_parts(r).str();
  if (r.chance(30)) {
    mutate(r, s);
  }
  return s;
}

std::string gen_input(rng &r) {
  switch (r.below(4)) {
    case 0: return gen_version(r);
    case 1: return gen_version(r) + "\n" + gen_version(r);
    default: {
      parts const a = gen_parts(r);
      return a.str() + "\n" + tweak(r, a).str();
    }
  }
}

}  // namespace nsv_fuzz
