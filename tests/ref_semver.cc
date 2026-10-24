#include "ref_semver.h"

#include <algorithm>
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

status parse_prefix(std::string_view s, version *out, size_t *consumed) {
  for (size_t n = s.size() + 1; n-- > 0;) {
    status const st = parse(s.substr(0, n), out);
    if (st != status::syntax) {
      *consumed = n;
      return st;
    }
  }
  *consumed = 0;
  return status::syntax;
}

namespace {

bool is_space(char c) { return (c == ' ') || (c == '\t'); }

std::vector<std::string_view> split_spaces(std::string_view s) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = i;
    while ((j < s.size()) && !is_space(s[j])) {
      ++j;
    }
    if (j > i) {
      out.push_back(s.substr(i, j - i));
    }
    i = j + 1;
  }
  return out;
}

// Longest first, so "<=" is not read as "<" then "=".
std::string_view const k_ops[] = { "<=", ">=", "<", ">", "=", "^", "~" };

std::string_view op_prefix(std::string_view tok) {
  for (std::string_view const op : k_ops) {
    if (tok.substr(0, op.size()) == op) {
      return op;
    }
  }
  return {};
}

struct comparator {
  std::string_view op;
  version v;
};

// npm's rewrite of ^c and ~c into >=c <up-0, where up is the next version they exclude.
// up-0 ranks below every other version with up's core, so v is below it exactly when
// v's core is.
bool below_upper(version const &v, std::string_view op, version const &c, bool *same_core) {
  std::uint64_t up[3] = { c.major, c.minor, c.patch };
  if ((op == "~") || ((op == "^") && !c.major && c.minor)) {
    up[1] += 1;
    up[2] = 0;
  } else if (c.major) {
    up[0] += 1;
    up[1] = up[2] = 0;
  } else {
    up[2] += 1;
  }
  std::uint64_t const vc[3] = { v.major, v.minor, v.patch };
  *same_core = (vc[0] == up[0]) && (vc[1] == up[1]) && (vc[2] == up[2]);
  return std::lexicographical_compare(vc, vc + 3, up, up + 3);
}

}  // namespace

status range_match(std::string_view range, version const &v, bool exclude_pre, bool *match) {
  std::vector<std::vector<comparator>> sets;
  status first = status::ok;

  // Sets, split on "||"; a '|' left in one is a syntax error.
  std::vector<std::string_view> pieces;
  for (std::string_view rest = range;;) {
    size_t const p = rest.find("||");
    pieces.push_back(rest.substr(0, p));
    if (p == std::string_view::npos) {
      break;
    }
    rest.remove_prefix(p + 2);
  }
  for (std::string_view const piece : pieces) {
    if (piece.find('|') != std::string_view::npos) {
      return status::syntax;
    }
    std::vector<std::string_view> const toks = split_spaces(piece);
    if (toks.empty()) {
      return status::syntax;
    }
    std::vector<comparator> set;
    for (size_t i = 0; i < toks.size(); ++i) {
      comparator c;
      std::string_view ver;
      c.op = op_prefix(toks[i]);
      if (c.op.size() == toks[i].size()) {  // an operator alone: the next token is its version
        if (++i == toks.size()) {
          return status::syntax;
        }
        ver = toks[i];
      } else {
        ver = toks[i].substr(c.op.size());
      }
      status const st = parse(ver, &c.v);
      if (st == status::syntax) {
        return st;
      }
      if (first == status::ok) {
        first = st;
      }
      set.push_back(c);
    }
    sets.push_back(set);
  }
  if (first != status::ok) {
    return first;
  }

  *match = false;
  for (std::vector<comparator> const &set : sets) {
    bool all = true, pre_ok = !v.has_pre || !exclude_pre;
    for (comparator const &c : set) {
      int const k = cmp(v, c.v);
      bool ok;
      if (c.op == "<") {
        ok = (k < 0);
      } else if (c.op == "<=") {
        ok = (k <= 0);
      } else if (c.op == ">") {
        ok = (k > 0);
      } else if (c.op == ">=") {
        ok = (k >= 0);
      } else if ((c.op == "^") || (c.op == "~")) {
        // npm checks both halves for a pre-release on v's core; the upper one has "-0".
        bool upper_same_core = false;
        ok = (k >= 0) && below_upper(v, c.op, c.v, &upper_same_core);
        pre_ok = pre_ok || upper_same_core;
      } else {
        ok = (k == 0);
      }
      all = all && ok;
      pre_ok = pre_ok || (c.v.has_pre && (c.v.major == v.major) && (c.v.minor == v.minor) &&
                          (c.v.patch == v.patch));
    }
    *match = *match || (all && pre_ok);
  }
  return status::ok;
}

}  // namespace ref
