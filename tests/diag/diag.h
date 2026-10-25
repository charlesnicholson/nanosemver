// Checks what nsv_diag_set_print gets, for the NSV_DIAG unit tests and the fuzzers. It shares
// no code with nanosemver.c: messages are rebuilt here from where and why, and a version's
// fault is checked against the reference implementation.
#pragma once

#include "../ref_semver.h"
#include "nanosemver.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace nsv_diag {

// Collects everything nanosemver prints while it lives.
class capture {
 public:
  capture() { nsv_diag_set_print(&print, this); }
  ~capture() { nsv_diag_set_print(nullptr, nullptr); }
  capture(capture const &) = delete;
  capture &operator=(capture const &) = delete;

  // What was printed since the last take(), and in how many calls.
  std::string take() {
    pieces_ = 0;
    std::string s;
    s.swap(text_);
    return s;
  }
  int pieces() const { return pieces_; }

 private:
  // A message is a few hundred bytes at most. More is a bug, or a mutant printing forever,
  // so stop before it takes all the memory there is.
  static void print(void *ctx, char const *s, size_t len) {
    capture *const c = static_cast<capture *>(ctx);
    if ((c->text_.size() + len) > 4096) {
      std::fputs("nsv_diag::capture: over 4096 bytes printed\n", stderr);
      std::abort();
    }
    c->text_.append(s, len);
    ++c->pieces_;
  }

  std::string text_;
  int pieces_ = 0;
};

inline bool digit(char c) { return (c >= '0') && (c <= '9'); }
inline bool wildcard(char c) { return (c == 'x') || (c == 'X') || (c == '*'); }

// A byte as a message shows it: printable ASCII as itself, a tab too unless quoted, and
// anything else as \xHH.
inline std::string shown(char c, bool quoted) {
  unsigned const u = static_cast<unsigned char>(c);
  if (((u == '\t') && !quoted) || ((u >= 0x20u) && (u < 0x7Fu))) {
    return std::string(1, c);
  }
  char buf[16];
  std::snprintf(buf, sizeof buf, "\\x%02x", u);
  return buf;
}

// The message for a fault at byte at of in: the line saying where and why, then up to 32
// bytes either side of it, with ... where cut, and a caret under it.
inline std::string expect(std::string_view kind, std::string_view in, size_t at,
                          std::string_view why) {
  size_t const lo = (at > 32) ? (at - 32) : 0;
  size_t const hi = ((in.size() - at) > 32) ? (at + 32) : in.size();
  std::string m = "bad " + std::string(kind) + " at column " + std::to_string(at + 1) + ": " +
                  std::string(why) + "\n  " + (lo ? "..." : "");
  for (size_t i = lo; i < hi; ++i) {
    m += shown(in[i], false);
  }
  m += (hi < in.size()) ? "...\n  " : "\n  ";
  m += lo ? "   " : "";
  for (size_t i = lo; i < at; ++i) {
    m += (in[i] == '\t') ? '\t' : ' ';
  }
  return m + "^\n";
}

// The longest prefix of s that a valid version starts with, by the reference. One of these
// tails completes any such prefix, and they are closed under taking prefixes, so a
// binary search finds the longest.
inline size_t viable(std::string_view s) {
  static char const *const tails[] = { "", "0", "a", ".0", "0.0", ".0.0", "0.0.0" };
  auto const ok = [s](size_t k) {
    for (char const *t : tails) {
      ref::version v;
      if (ref::parse(std::string(s.substr(0, k)) + t, &v) != ref::status::syntax) {
        return true;
      }
    }
    return false;
  };
  size_t lo = 0, hi = s.size();
  while (lo < hi) {
    size_t const mid = (lo + hi + 1) / 2;
    if (ok(mid)) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

// Whether the digits at in[at] start a number over UINT32_MAX.
inline bool overflows(std::string_view in, size_t at) {
  if ((at > 0) && digit(in[at - 1])) {
    return false;
  }
  std::uint64_t x = 0;
  size_t i = at;
  for (; (i < in.size()) && digit(in[i]); ++i) {
    x = (x > 0xFFFFFFFFull) ? x : ((x * 10) + static_cast<unsigned>(in[i] - '0'));
  }
  return (i > at) && (x > 0xFFFFFFFFull);
}

// Whether the version at at is longer than 65535 bytes.
inline bool too_long(std::string_view in, size_t at) {
  size_t i = at;
  while ((i < in.size()) && (std::string_view("0123456789.+-ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                              "abcdefghijklmnopqrstuvwxyz")
                                 .find(in[i]) != std::string_view::npos)) {
    ++i;
  }
  return (i - at) > 65535;
}

// Whether why, at at of in, fits a syntax error in a version. A leading zero is shown at
// its 0, and every other fault at the end of the longest prefix a version starts with.
inline bool version_syntax_ok(std::string_view in, size_t at, std::string const &why) {
  char const prev = at ? in[at - 1] : '\0';
  if (why == "leading zero") {
    return ((at + 1) < in.size()) && (in[at] == '0') && digit(in[at + 1]) &&
           (!at || (prev == '.') || (prev == '-')) && (viable(in) > at);
  }
  if (at != viable(in)) {
    return false;
  }
  if (at < in.size()) {
    if (why == "unexpected '" + shown(in[at], true) + "'") {
      return true;
    }
    if (why == "more than three components") {
      return in[at] == '.';
    }
  }
  return (why == "expected a number") || (why == "expected '.'") ||
         ((why == "expected an identifier") && (prev == '.')) ||
         ((why == "expected a pre-release") && (prev == '-')) ||
         ((why == "expected build metadata") && (prev == '+'));
}

// Whether why, at at of in, fits a syntax error in a range, by what is at and around at.
inline bool range_syntax_ok(std::string_view in, size_t at, std::string const &why) {
  bool const end = (at == in.size());
  char const prev = at ? in[at - 1] : '\0';
  char const c = end ? '\0' : in[at];  // a NUL in the range is just a byte, unlike end
  if (why == "expected a version") {
    return !digit(c) && !wildcard(c);
  }
  if (why == "empty range; use * for any version") {
    return !at && (in.find_first_not_of(" \t") == std::string_view::npos);
  }
  if (why == "hyphen ranges are not supported; use >= and <=") {
    return (c == '-') && ((prev == ' ') || (prev == '\t'));
  }
  if (why == "expected '=' after '!'") {
    return (prev == '!') && (c != '=');
  }
  if (why == "expected '||'") {
    return (c == '|') && (((at + 1) == in.size()) || (in[at + 1] != '|'));
  }
  if (why == "expected a comparator after ','") {
    size_t const k = in.find_last_not_of(" \t", at ? (at - 1) : 0);
    return (end || (c == '|')) && (k != std::string_view::npos) && (in[k] == ',');
  }
  if (why == "leading zero") {
    return (c == '0') && ((at + 1) < in.size()) && digit(in[at + 1]) && !digit(prev);
  }
  if (why == "number after a wildcard") {
    return digit(c) && (wildcard(prev) || ((prev == '.') && (at > 1) && wildcard(in[at - 2])));
  }
  if (why == "expected a number or a wildcard") {
    return (prev == '.') && !digit(c) && !wildcard(c);
  }
  if (why == "more than three components") {
    return c == '.';
  }
  if (why == "pre-release or build on a partial version") {
    return (c == '-') || (c == '+');
  }
  if ((why == "expected a whole version after '=='") ||
      (why == "expected a whole version after '~='") ||
      (why == "expected a whole version after '~>'") ||
      (why == "expected a whole version or a wildcard after '!='")) {
    return digit(c) || wildcard(c);
  }
  return ((why == "expected an identifier") && (prev == '.')) ||
         ((why == "expected a pre-release") && (prev == '-')) ||
         ((why == "expected build metadata") && (prev == '+')) ||
         (!end && (why == "unexpected '" + shown(c, true) + "'"));
}

// Whether msg is what nanosemver prints for rc from parsing in as kind, "version" or
// "range": nothing for anything but SYNTAX, OVERFLOW or TOO_LONG, else one message in
// expect()'s form, whose why fits rc and what is at its column.
inline bool message_ok(std::string const &msg, std::string_view kind, std::string_view in,
                       int rc) {
  if ((rc != NSV_RET_ERR_SYNTAX) && (rc != NSV_RET_ERR_OVERFLOW) &&
      (rc != NSV_RET_ERR_TOO_LONG)) {
    return msg.empty();
  }
  std::string const head = "bad " + std::string(kind) + " at column ";
  if (msg.compare(0, head.size(), head) != 0) {
    return false;
  }
  size_t i = head.size(), col = 0;
  for (; (i < msg.size()) && digit(msg[i]) && (col <= in.size() + 1); ++i) {
    col = (col * 10) + static_cast<size_t>(msg[i] - '0');
  }
  size_t const nl = msg.find('\n', i);
  if ((col == 0) || (col > in.size() + 1) || (msg.compare(i, 2, ": ") != 0) ||
      (nl == std::string::npos)) {
    return false;
  }
  size_t const at = col - 1;
  std::string const why = msg.substr(i + 2, nl - i - 2);
  if (msg != expect(kind, in, at, why)) {
    return false;
  }
  bool const ver = (kind == "version");
  if (rc == NSV_RET_ERR_OVERFLOW) {
    if (ver) {  // the first of the three numbers to overflow
      for (size_t k = 0, b = 0; k < 3; ++k, b = in.find('.', b) + 1) {
        if (overflows(in, b)) {
          return (why == "number over 4294967295") && (b == at);
        }
      }
      return false;
    }
    return (why == "number over 4294967295") && overflows(in, at);
  }
  if (rc == NSV_RET_ERR_TOO_LONG) {
    return (why == "version longer than 65535 bytes") && (ver ? (at == 0) : too_long(in, at));
  }
  return ver ? version_syntax_ok(in, at, why) : range_syntax_ok(in, at, why);
}

}  // namespace nsv_diag
