#include "ref_semver.h"

#include <vector>

namespace ref {
namespace {

bool is_digit(char c) { return (c >= '0') && (c <= '9'); }

bool is_ident_char(char c) {
  return is_digit(c) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
         (c == '-');
}

bool all_digits(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  for (char const c : s) {
    if (!is_digit(c)) {
      return false;
    }
  }
  return true;
}

bool all_ident_chars(std::string_view s) {
  for (char const c : s) {
    if (!is_ident_char(c)) {
      return false;
    }
  }
  return true;
}

// Keeps empty pieces: "a..b" is three pieces, "" is one.
std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  for (;;) {
    size_t const p = s.find(sep);
    out.push_back(s.substr(0, p));
    if (p == std::string_view::npos) {
      return out;
    }
    s.remove_prefix(p + 1);
  }
}

bool numeric_identifier(std::string_view s) {
  return all_digits(s) && ((s.size() == 1) || (s[0] != '0'));
}

bool fits_u32(std::string_view digits) {
  std::string_view const max = "4294967295";
  return (digits.size() < max.size()) ||
         ((digits.size() == max.size()) && (digits <= max));
}

std::uint32_t to_u32(std::string_view digits) {
  std::uint64_t v = 0;
  for (char const c : digits) {
    v = (v * 10) + static_cast<std::uint64_t>(c - '0');
  }
  return static_cast<std::uint32_t>(v);
}

int sign(long long v) { return (v > 0) - (v < 0); }

// Zero-pads the shorter to the longer's width, so string order is numeric order.
int cmp_numeric(std::string_view a, std::string_view b) {
  size_t const w = (a.size() > b.size()) ? a.size() : b.size();
  std::string const pa = std::string(w - a.size(), '0') + std::string(a);
  std::string const pb = std::string(w - b.size(), '0') + std::string(b);
  return sign(pa.compare(pb));
}

}  // namespace

status parse(std::string_view s, version *out) {
  version v;

  size_t const plus = s.find('+');
  std::string_view const head = s.substr(0, plus);
  v.has_build = (plus != std::string_view::npos);
  if (v.has_build) {
    v.build = std::string(s.substr(plus + 1));
  }

  size_t const dash = head.find('-');
  std::string_view const core = head.substr(0, dash);
  v.has_pre = (dash != std::string_view::npos);
  if (v.has_pre) {
    v.pre = std::string(head.substr(dash + 1));
  }

  std::vector<std::string_view> const nums = split(core, '.');
  if (nums.size() != 3) {
    return status::syntax;
  }
  for (std::string_view const n : nums) {
    if (!numeric_identifier(n)) {
      return status::syntax;
    }
  }
  if (v.has_pre) {
    for (std::string_view const id : split(v.pre, '.')) {
      if (id.empty() || !all_ident_chars(id) || (all_digits(id) && !numeric_identifier(id))) {
        return status::syntax;
      }
    }
  }
  if (v.has_build) {
    for (std::string_view const id : split(v.build, '.')) {
      if (id.empty() || !all_ident_chars(id)) {
        return status::syntax;
      }
    }
  }

  for (std::string_view const n : nums) {
    if (!fits_u32(n)) {
      return status::overflow;
    }
  }
  if (s.size() > 65535) {
    return status::too_long;
  }

  v.major = to_u32(nums[0]);
  v.minor = to_u32(nums[1]);
  v.patch = to_u32(nums[2]);
  *out = v;
  return status::ok;
}

int cmp(version const &a, version const &b) {
  if (a.major != b.major) {
    return (a.major < b.major) ? -1 : 1;
  }
  if (a.minor != b.minor) {
    return (a.minor < b.minor) ? -1 : 1;
  }
  if (a.patch != b.patch) {
    return (a.patch < b.patch) ? -1 : 1;
  }
  if (!a.has_pre || !b.has_pre) {
    return int(!a.has_pre) - int(!b.has_pre);
  }

  std::vector<std::string_view> const ai = split(a.pre, '.');
  std::vector<std::string_view> const bi = split(b.pre, '.');
  for (size_t k = 0; (k < ai.size()) && (k < bi.size()); ++k) {
    bool const an = all_digits(ai[k]), bn = all_digits(bi[k]);
    int c;
    if (an && bn) {
      c = cmp_numeric(ai[k], bi[k]);
    } else if (an != bn) {
      c = an ? -1 : 1;
    } else {
      c = sign(ai[k].compare(bi[k]));
    }
    if (c) {
      return c;
    }
  }
  return sign(static_cast<long long>(ai.size()) - static_cast<long long>(bi.size()));
}

}  // namespace ref
