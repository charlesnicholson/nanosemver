// A/B benchmark: this tree's nanosemver.c against a baseline revision's, both linked
// into one binary (the baseline under nsvbase_* names) and timed interleaved, with the
// order alternating each round so drift hits both equally. Every workload also
// checksums both sides' results; a mismatch voids the run.
//
//   nsv_bench [--quick] [--filter SUBSTR] [--samples N] [--batch-ms N] [--ghz F]
//             [--only new|base] [--list] [--dump]
//
// --only times one side alone, for a profiler. --dump prints the parse corpora.

#include "nanosemver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef NSV_BENCH_BASE
#define NSV_BENCH_BASE "base"
#endif

extern "C" {
int nsvbase_parse(nsv_semver_t *out, char const *s, size_t len);
int nsvbase_parse_prefix(nsv_semver_t *out, char const *s, size_t len, size_t *consumed);
int nsvbase_cmp(nsv_semver_t const *a, nsv_semver_t const *b);
int nsvbase_range_match(char const *s, size_t len, nsv_semver_t const *v, unsigned flags);
int nsvbase_copy(void *dst, size_t cap, nsv_semver_t const *s);
int nsvbase_format(char *dst, size_t n, nsv_semver_t const *s);
}

// What bench/base_stub.c's stand-ins return for a BENCH_BASE that predates them.
#define NSV_BENCH_ABSENT (-1000)

namespace {

// ------------------------------------------------------------------ the two sides --

struct impl {
  char const *name;
  int (*parse)(nsv_semver_t *, char const *, size_t);
  int (*parse_prefix)(nsv_semver_t *, char const *, size_t, size_t *);
  int (*cmp)(nsv_semver_t const *, nsv_semver_t const *);
  int (*range_match)(char const *, size_t, nsv_semver_t const *, unsigned);
  int (*copy)(void *, size_t, nsv_semver_t const *);
  int (*format)(char *, size_t, nsv_semver_t const *);
};

impl const k_base{ "base",          nsvbase_parse,  nsvbase_parse_prefix, nsvbase_cmp,
                   nsvbase_range_match, nsvbase_copy, nsvbase_format };
impl const k_new{ "new", nsv_parse, nsv_parse_prefix, nsv_cmp, nsv_range_match, nsv_copy,
                  nsv_format };

// Whether BENCH_BASE has nsv_parse_prefix and nsv_range_match, or only stand-ins.
bool base_has_ranges() {
  nsv_semver_t const v{};
  return nsvbase_range_match("1.0.0", 5, &v, 0) != NSV_BENCH_ABSENT;
}

// ------------------------------------------------------------------- measurement --

using clk = std::chrono::steady_clock;

void clobber() {
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("" : : : "memory");
#endif
}

template <typename F>
uint64_t time_batch(F &&f, size_t reps) {
  clobber();
  auto const t0 = clk::now();
  for (size_t i = 0; i < reps; ++i) {
    f();
    clobber();
  }
  auto const t1 = clk::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

template <typename F>
size_t calibrate(F &&f, uint64_t target_ns) {
  size_t reps = 1;
  for (int i = 0; i < 64; ++i) {
    uint64_t const ns = time_batch(f, reps);
    if (ns >= target_ns) {
      return reps;
    }
    double const grow = (ns == 0) ? 8.0
                                  : std::min(8.0, 1.25 * static_cast<double>(target_ns) /
                                                      static_cast<double>(ns));
    reps = std::max(reps + 1, static_cast<size_t>(static_cast<double>(reps) * grow));
  }
  return reps;
}

void spin_warmup(int ms) {
  auto const end = clk::now() + std::chrono::milliseconds(ms);
  uint64_t x = 1;
  while (clk::now() < end) {
    x = (x * 6364136223846793005ull) + 1442695040888963407ull;
  }
  std::printf("# warmup done (%llx)\n", static_cast<unsigned long long>(x & 0xf));
}

uint64_t fnv(uint64_t h, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h = (h ^ (v & 0xffu)) * 1099511628211ull;
    v >>= 8;
  }
  return h;
}

uint64_t fnv_bytes(uint64_t h, char const *p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    h = (h ^ static_cast<unsigned char>(p[i])) * 1099511628211ull;
  }
  return h;
}

// ------------------------------------------------------------------------ corpus --

struct rng {
  uint64_t s;
  uint64_t next() {
    uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }
  uint32_t below(uint32_t n) { return static_cast<uint32_t>(next() % n); }
  template <size_t N>
  char const *pick(char const *const (&a)[N]) {
    return a[below(N)];
  }
};

// Most published versions have one- and two-digit components; a few are dates or
// build counters.
std::string num(rng &r, bool major) {
  uint32_t const p = r.below(100);
  uint32_t v;
  if (p < (major ? 60u : 40u)) {
    v = r.below(10);
  } else if (p < (major ? 90u : 72u)) {
    v = 10 + r.below(90);
  } else if (p < 94) {
    v = 100 + r.below(900);
  } else if (p < 98) {
    v = 1000 + r.below(99000);
  } else {
    v = 20000101 + r.below(260000);
  }
  return std::to_string(v);
}

std::string hex(rng &r, size_t n) {
  static char const k[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += k[r.below(16)];
  }
  return s;
}

// The generators draw from r in source order: operands of + are unsequenced, so every
// draw is its own statement and the corpus is the same under any compiler.
std::string core(rng &r) {
  std::string s = num(r, true);
  s += ".";
  s += num(r, false);
  s += ".";
  s += num(r, false);
  return s;
}

char const *const k_tags[] = { "alpha",  "beta",    "rc",      "dev",      "pre",
                               "next",   "canary",  "SNAPSHOT", "nightly", "preview",
                               "insiders", "RC",    "M",       "b",        "a" };

std::string pre(rng &r) {
  uint32_t const p = r.below(100);
  std::string s = r.pick(k_tags);
  if (p < 25) {
  } else if (p < 80) {
    s += "." + std::to_string(r.below(p < 70 ? 10 : 200));
  } else if (p < 88) {
    s += std::to_string(r.below(20));  // "rc1", "beta2"
  } else if (p < 94) {
    s += "." + std::to_string(r.below(5));
    s += "." + std::to_string(r.below(30));
  } else if (p < 97) {
    s += "." + std::to_string(20230101 + r.below(30000));
    s += "." + hex(r, 7);
  } else {
    s = std::to_string(r.below(10));
  }
  return s;
}

std::string build(rng &r) {
  switch (r.below(6)) {
    case 0: return "build." + std::to_string(r.below(5000));
    case 1: return "sha." + hex(r, 7);
    case 2: return "g" + hex(r, 9);
    case 3: return std::to_string(20230101000000ull + r.below(100000000));
    case 4: return "exp.sha." + hex(r, 7);
    default: return "linux.x86-64";
  }
}

std::string core_pre(rng &r) {
  std::string s = core(r);
  return s + "-" + pre(r);
}

std::string core_build(rng &r) {
  std::string s = core(r);
  return s + "+" + build(r);
}

std::string core_pre_build(rng &r) {
  std::string s = core_pre(r);
  return s + "+" + build(r);
}

std::string long_one(rng &r) {
  uint32_t const k = r.below(4);
  std::string s = core(r);
  if (k == 0) {
    return s + "+" + hex(r, 40);
  }
  if (k == 2) {
    return s + "-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay";
  }
  s += "-";
  s += r.pick(k_tags);
  if (k == 1) {
    return s + "." + hex(r, 40);
  }
  s += "." + std::to_string(r.below(100));
  s += "+" + std::to_string(20230101000000ull + r.below(100000000));
  return s + "." + hex(r, 40);
}

// Plain releases dominate any registry; pre-releases are the bulk of the rest.
std::string mix_one(rng &r) {
  uint32_t const p = r.below(100);
  if (p < 80) {
    return core(r);
  }
  if (p < 94) {
    return core_pre(r);
  }
  if (p < 98) {
    return core_build(r);
  }
  return core_pre_build(r);
}

std::string invalid_one(rng &r) {
  std::string const v = mix_one(r);
  uint32_t const k = r.below(8);
  switch (k) {
    case 0: return "v" + v;
    case 1: return "0" + v;
    case 2: return v + " ";
    case 3: return v.substr(0, v.find('.', v.find('.') + 1));  // "1.2"
    case 4: return v + ".4";
    default: break;
  }
  std::string s = core(r);
  if (k == 5) {
    return s + "-";
  }
  s += "-";
  s += r.pick(k_tags);
  return s + ((k == 6) ? ".01" : "..1");
}

// A version and what follows it in a range or a list.
std::string prefix_one(rng &r) {
  static char const *const k_tails[] = { "", " ", " <2.0.0", ", 1.4.0", " || ^2.1.0" };
  std::string s = mix_one(r);
  return s + r.pick(k_tails);
}

// Ranges as package manifests write them: mostly ^ and ~, then bounded pairs, exact
// versions, alternatives, and pre-release bounds.
std::string range_one(rng &r) {
  uint32_t const k = r.below(100);
  std::string s;
  if (k < 35) {
    s = "^" + core(r);
  } else if (k < 50) {
    s = "~" + core(r);
  } else if (k < 75) {
    s = ">=" + core(r);
    s += " <" + core(r);
  } else if (k < 85) {
    s = core(r);
  } else if (k < 95) {
    s = ">=" + core(r);
    s += " <" + core(r);
    s += " || ^" + core(r);
  } else {
    s = ">=" + core(r);
    s += "-0 <" + core(r);
    s += "-0";
  }
  return s;
}

// Several alternatives, each a bounded pair: every set is parsed and compared.
std::string range_or(rng &r) {
  std::string s;
  for (int i = 0; i < 4; ++i) {
    s += i ? " || >=" : ">=";
    s += core(r);
    s += " <" + core(r);
  }
  return s;
}

// Ranges written with partial versions and wildcards, and an exclusion.
std::string range_partial(rng &r) {
  uint32_t const k = r.below(100);
  std::string const a = num(r, true);
  std::string const b = num(r, false);
  std::string s;
  if (k < 30) {
    s = "^" + a + "." + b;
  } else if (k < 45) {
    s = "~" + a + "." + b;
  } else if (k < 60) {
    s = a + ".x";
  } else if (k < 80) {
    s = ">=" + a + "." + b;
    s += " <" + num(r, true);
  } else if (k < 90) {
    s = ">=" + a;
    s += " !=" + core(r);
  } else {
    s = "*";
  }
  return s;
}

// Strings packed back to back in one buffer, the way a manifest or lockfile holds
// them: no NULs, every parse bounded by its length alone.
struct strings {
  std::string buf;
  std::vector<size_t> ofs, len;
  size_t bytes = 0;

  void add(std::string const &s) {
    ofs.push_back(buf.size());
    len.push_back(s.size());
    buf += s;
    bytes += s.size();
  }
  size_t size() const { return ofs.size(); }
  char const *at(size_t i) const { return buf.data() + ofs[i]; }
};

size_t const k_n = 1024;

template <typename G>
strings make_strings(uint64_t seed, G &&gen) {
  rng r{ seed };
  strings s;
  for (size_t i = 0; i < k_n; ++i) {
    s.add(gen(r));
  }
  return s;
}

std::vector<nsv_semver_t> parse_all(strings const &s) {
  std::vector<nsv_semver_t> v(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (nsvbase_parse(&v[i], s.at(i), s.len[i]) != NSV_RET_OK) {
      std::fprintf(stderr, "corpus string %zu did not parse\n", i);
      std::exit(1);
    }
  }
  return v;
}

// ------------------------------------------------------------------- reporting --

struct config {
  uint64_t batch_ns = 40'000'000;
  int samples = 15;
  std::string filter;
  double ghz = 0.0;
  impl const *only = nullptr;
  bool list = false;
  bool dump = false;
};

struct row {
  std::string name;
  size_t ops, bytes;
  double ns_base, ns_new;  // per op, best batch
  double paired;           // median of per-round base/new
  bool ck_ok;
};

std::vector<row> g_rows;

// A workload: run(impl, check) performs one batch of `ops` operations over `bytes`
// input bytes. With check it returns a checksum of every result, computed once per
// side outside the timing; without, something cheap that keeps the work alive.
// With has_base false, BENCH_BASE predates the function, so only this tree's is timed.
template <typename Run>
void measure(config const &cfg, char const *name, size_t ops, size_t bytes, Run &&run,
             bool has_base = true) {
  std::string const label = name;
  if (!cfg.filter.empty() && (label.find(cfg.filter) == std::string::npos)) {
    return;
  }
  if (cfg.list) {
    std::printf("%s\n", name);
    return;
  }

  volatile uint64_t sink = 0;
  if (!has_base) {
    if (cfg.only == &k_base) {
      return;
    }
    run(k_new, true);
    auto f = [&] { sink = sink + run(k_new, false); };
    size_t const reps = calibrate(f, cfg.batch_ns);
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < cfg.samples; ++i) {
      best = std::min(best, time_batch(f, reps));
    }
    double const ns = static_cast<double>(best) / static_cast<double>(reps * ops);
    if (cfg.only) {
      std::printf("%-22s %s %8.2f ns/op\n", name, k_new.name, ns);
    } else {
      g_rows.push_back(row{ label, ops, bytes, 0.0, ns, 0.0, true });
    }
    return;
  }

  uint64_t const ck_base = run(k_base, true), ck_new = run(k_new, true);

  if (cfg.only) {
    auto f = [&] { sink = sink + run(*cfg.only, false); };
    size_t const reps = calibrate(f, cfg.batch_ns);
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < cfg.samples; ++i) {
      best = std::min(best, time_batch(f, reps));
    }
    double const ns = static_cast<double>(best) / static_cast<double>(reps * ops);
    std::printf("%-22s %s %8.2f ns/op\n", name, cfg.only->name, ns);
    return;
  }

  auto fb = [&] { sink = sink + run(k_base, false); };
  auto fn = [&] { sink = sink + run(k_new, false); };
  size_t const reps = calibrate(fn, cfg.batch_ns);
  time_batch(fb, reps);
  time_batch(fn, reps);

  uint64_t min_b = UINT64_MAX, min_n = UINT64_MAX;
  std::vector<double> ratios;
  for (int i = 0; i < cfg.samples; ++i) {
    uint64_t b, n;
    if (i & 1) {
      n = time_batch(fn, reps);
      b = time_batch(fb, reps);
    } else {
      b = time_batch(fb, reps);
      n = time_batch(fn, reps);
    }
    min_b = std::min(min_b, b);
    min_n = std::min(min_n, n);
    if (n) {
      ratios.push_back(static_cast<double>(b) / static_cast<double>(n));
    }
  }
  std::sort(ratios.begin(), ratios.end());
  double const per = static_cast<double>(reps * ops);
  g_rows.push_back(row{ label, ops, bytes, static_cast<double>(min_b) / per,
                        static_cast<double>(min_n) / per,
                        ratios.empty() ? 0.0 : ratios[ratios.size() / 2],
                        ck_base == ck_new });
}

int report(config const &cfg) {
  if (g_rows.empty()) {
    return 0;
  }
  bool const cyc = cfg.ghz > 0.0;
  std::printf("\n%-22s %9s %9s %8s %8s %8s%s  %s\n", "workload", "base ns", "new ns",
              "B/op", "min x", "paired x", cyc ? "  base cyc   new cyc" : "", "verdict");
  double log_sum = 0.0;
  bool bad = false;
  size_t paired = 0;
  for (row const &r : g_rows) {
    double const per = static_cast<double>(r.bytes) / static_cast<double>(r.ops);
    if (r.ns_base <= 0.0) {  // BENCH_BASE predates it
      std::printf("%-22s %9s %9.2f %8.1f %8s %8s", r.name.c_str(), "-", r.ns_new, per, "-",
                  "-");
      if (cyc) {
        std::printf(" %9s %9.1f", "-", r.ns_new * cfg.ghz);
      }
      std::printf("  new\n");
      continue;
    }
    double const x = r.ns_new > 0.0 ? r.ns_base / r.ns_new : 0.0;
    char const *verdict = !r.ck_ok ? "CHECKSUM-MISMATCH"
                          : (x >= 1.10)  ? "faster"
                          : (x <= 0.95)  ? "SLOWER"
                                         : "";
    bad |= !r.ck_ok;
    std::printf("%-22s %9.2f %9.2f %8.1f %7.2fx %7.2fx", r.name.c_str(), r.ns_base, r.ns_new,
                per, x, r.paired);
    if (cyc) {
      std::printf(" %9.1f %9.1f", r.ns_base * cfg.ghz, r.ns_new * cfg.ghz);
    }
    std::printf("  %s\n", verdict);
    log_sum += std::log(x > 0.0 ? x : 1.0);
    ++paired;
  }
  std::printf("# geomean %.2fx over %zu workloads (>1: new is faster). Under 1.10x is\n"
              "# noise until repeated; code alignment alone moves microbenchmarks 5%%.\n",
              paired ? std::exp(log_sum / static_cast<double>(paired)) : 1.0, paired);
  if (bad) {
    std::printf("# *** CHECKSUM MISMATCH: base and new disagree; results are void. ***\n");
  }
  return bad ? 1 : 0;
}

// ---------------------------------------------------------------------- workloads --

void parse_workload(config const &cfg, char const *name, strings const &s) {
  if (cfg.dump) {
    for (size_t i = 0; i < s.size(); ++i) {
      std::printf("%s\t%.*s\n", name, static_cast<int>(s.len[i]), s.at(i));
    }
    return;
  }
  std::vector<nsv_semver_t> out(s.size());
  measure(cfg, name, s.size(), s.bytes, [&](impl const &im, bool check) {
    uint64_t ck = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      int const rc = im.parse(&out[i], s.at(i), s.len[i]);
      ck += static_cast<uint64_t>(static_cast<int64_t>(rc));
    }
    if (!check) {
      return ck;
    }
    for (nsv_semver_t const &v : out) {
      ck = fnv(ck, (uint64_t{ v.major } << 32) ^ (uint64_t{ v.minor } << 16) ^ v.patch);
      ck = fnv(ck, (uint64_t{ v.pre_ofs } << 48) | (uint64_t{ v.pre_len } << 32) |
                       (uint64_t{ v.build_ofs } << 16) | v.build_len);
    }
    return ck;
  });
}

using pairs = std::vector<std::pair<nsv_semver_t const *, nsv_semver_t const *>>;

void cmp_workload(config const &cfg, char const *name, pairs const &p) {
  measure(cfg, name, p.size(), 0, [&](impl const &im, bool) {
    uint64_t ck = 0;
    for (auto const &ab : p) {
      ck = (ck * 3) + static_cast<uint64_t>(im.cmp(ab.first, ab.second));
    }
    return ck;
  });
}

void prefix_workload(config const &cfg, char const *name, strings const &s) {
  if (cfg.dump) {
    for (size_t i = 0; i < s.size(); ++i) {
      std::printf("%s\t%.*s\n", name, static_cast<int>(s.len[i]), s.at(i));
    }
    return;
  }
  std::vector<nsv_semver_t> out(s.size());
  measure(
      cfg, name, s.size(), s.bytes,
      [&](impl const &im, bool check) {
        uint64_t ck = 0;
        for (size_t i = 0; i < s.size(); ++i) {
          size_t n = 0;
          int const rc = im.parse_prefix(&out[i], s.at(i), s.len[i], &n);
          ck += static_cast<uint64_t>(static_cast<int64_t>(rc)) + n;
        }
        if (!check) {
          return ck;
        }
        for (nsv_semver_t const &v : out) {
          ck = fnv(ck, (uint64_t{ v.major } << 32) ^ (uint64_t{ v.minor } << 16) ^ v.patch);
          ck = fnv(ck, (uint64_t{ v.pre_len } << 16) | v.build_len);
        }
        return ck;
      },
      base_has_ranges());
}

// Range i against version i, both from their own corpora.
void range_workload(config const &cfg, char const *name, strings const &ranges,
                    std::vector<nsv_semver_t> const &v, unsigned flags) {
  if (cfg.dump) {
    for (size_t i = 0; i < ranges.size(); ++i) {
      std::printf("%s\t%.*s\n", name, static_cast<int>(ranges.len[i]), ranges.at(i));
    }
    return;
  }
  measure(
      cfg, name, ranges.size(), ranges.bytes,
      [&](impl const &im, bool) {
        uint64_t ck = 0;
        for (size_t i = 0; i < ranges.size(); ++i) {
          int const rc = im.range_match(ranges.at(i), ranges.len[i], &v[i], flags);
          ck = (ck * 3) + static_cast<uint64_t>(static_cast<int64_t>(rc));
        }
        return ck;
      },
      base_has_ranges());
}

void run_all(config const &cfg) {
  strings const s_core = make_strings(1, core);
  strings const s_pre = make_strings(2, core_pre);
  strings const s_build = make_strings(3, core_build);
  strings const s_full = make_strings(4, core_pre_build);
  strings const s_long = make_strings(5, long_one);
  strings const s_mix = make_strings(6, mix_one);
  strings const s_bad = make_strings(7, invalid_one);

  parse_workload(cfg, "parse/core", s_core);
  parse_workload(cfg, "parse/pre", s_pre);
  parse_workload(cfg, "parse/build", s_build);
  parse_workload(cfg, "parse/full", s_full);
  parse_workload(cfg, "parse/long", s_long);
  parse_workload(cfg, "parse/mix", s_mix);
  parse_workload(cfg, "parse/invalid", s_bad);

  std::vector<nsv_semver_t> const v_mix = parse_all(s_mix);
  std::vector<nsv_semver_t> const v_full = parse_all(s_full);
  std::vector<nsv_semver_t> const v_long = parse_all(s_long);

  // Neighbours in sorted order: what a sort's late passes and a dedup compare.
  {
    std::vector<nsv_semver_t> sorted = v_mix;
    std::sort(sorted.begin(), sorted.end(), [](nsv_semver_t const &a, nsv_semver_t const &b) {
      return nsvbase_cmp(&a, &b) == NSV_CMP_LT;
    });
    static std::vector<nsv_semver_t> keep;
    keep = sorted;
    pairs p;
    for (size_t i = 0; i + 1 < keep.size(); ++i) {
      p.emplace_back(&keep[i], &keep[i + 1]);
    }
    cmp_workload(cfg, "cmp/sorted-adjacent", p);
  }
  {
    rng r{ 8 };
    pairs p;
    for (size_t i = 0; i < k_n; ++i) {
      size_t const a = r.below(k_n);
      p.emplace_back(&v_mix[a], &v_mix[r.below(k_n)]);
    }
    cmp_workload(cfg, "cmp/random", p);
  }

  // Same core, different pre-release: every compare reaches the identifier walk.
  static strings s_prepairs;
  static std::vector<nsv_semver_t> v_prepairs;
  {
    rng r{ 9 };
    s_prepairs = make_strings(9, [](rng &rr) { return "1.0.0-" + pre(rr); });
    v_prepairs = parse_all(s_prepairs);
    pairs p;
    for (size_t i = 0; i < k_n; ++i) {
      size_t const a = r.below(k_n);
      p.emplace_back(&v_prepairs[a], &v_prepairs[r.below(k_n)]);
    }
    cmp_workload(cfg, "cmp/pre-random", p);
  }
  {
    // semver.org's precedence chain, every ordered pair.
    static char const *const k_chain[] = { "1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta",
                                           "1.0.0-beta",   "1.0.0-beta.2",  "1.0.0-beta.11",
                                           "1.0.0-rc.1",   "1.0.0" };
    static std::vector<nsv_semver_t> chain;
    chain.clear();
    for (char const *c : k_chain) {
      nsv_semver_t v{};
      nsvbase_parse(&v, c, std::strlen(c));
      chain.push_back(v);
    }
    pairs p;
    for (size_t i = 0; i < chain.size(); ++i) {
      for (size_t j = 0; j < chain.size(); ++j) {
        p.emplace_back(&chain[i], &chain[j]);
      }
    }
    cmp_workload(cfg, "cmp/spec-chain", p);
  }
  {
    // Equal precedence: the whole pre-release is walked.
    static std::vector<nsv_semver_t> twin_full, twin_long;
    static std::string twin_buf_full, twin_buf_long;
    twin_buf_full = s_full.buf;
    twin_buf_long = s_long.buf;
    twin_full = v_full;
    twin_long = v_long;
    for (nsv_semver_t &v : twin_full) {
      v.src = twin_buf_full.data() + (v.src - s_full.buf.data());
    }
    for (nsv_semver_t &v : twin_long) {
      v.src = twin_buf_long.data() + (v.src - s_long.buf.data());
    }
    pairs p, q;
    for (size_t i = 0; i < k_n; ++i) {
      p.emplace_back(&v_full[i], &twin_full[i]);
      q.emplace_back(&v_long[i], &twin_long[i]);
    }
    cmp_workload(cfg, "cmp/equal-full", p);
    cmp_workload(cfg, "cmp/equal-long", q);
  }
  {
    std::vector<nsv_semver_t> work(v_mix.size());
    measure(cfg, "sort/mix", 1, 0, [&](impl const &im, bool check) {
      work = v_mix;
      std::sort(work.begin(), work.end(), [&](nsv_semver_t const &a, nsv_semver_t const &b) {
        return im.cmp(&a, &b) == NSV_CMP_LT;
      });
      uint64_t ck = work[0].major;
      if (!check) {
        return ck;
      }
      for (nsv_semver_t const &v : work) {
        ck = fnv(ck, (uint64_t{ v.major } << 40) ^ (uint64_t{ v.minor } << 20) ^ v.patch);
      }
      return ck;
    });
  }

  auto fmt = [&](char const *name, std::vector<nsv_semver_t> const &v, bool query) {
    std::vector<char> buf(256);
    measure(cfg, name, v.size(), 0, [&, query](impl const &im, bool check) {
      uint64_t ck = 0;
      for (nsv_semver_t const &x : v) {
        int const n = query ? im.format(nullptr, 0, &x) : im.format(buf.data(), buf.size(), &x);
        if (!check) {
          ck += static_cast<uint64_t>(n);
          continue;
        }
        ck = fnv(ck, static_cast<uint64_t>(static_cast<int64_t>(n)));
        if (!query) {
          ck = fnv_bytes(ck, buf.data(), static_cast<size_t>(n));
        }
      }
      return ck;
    });
  };
  {
    strings const s_prefix = make_strings(10, prefix_one);
    strings const s_ranges = make_strings(11, range_one);
    strings const s_ors = make_strings(12, range_or);
    strings const s_partial = make_strings(13, range_partial);
    prefix_workload(cfg, "prefix/mix", s_prefix);
    range_workload(cfg, "range/mix", s_ranges, v_mix, 0);
    range_workload(cfg, "range/mix-excl", s_ranges, v_mix, NSV_RANGE_EXCLUDE_PRE);
    range_workload(cfg, "range/or4", s_ors, v_mix, 0);
    range_workload(cfg, "range/partial", s_partial, v_mix, 0);
  }

  fmt("format/mix", v_mix, false);
  fmt("format/long", v_long, false);
  fmt("format-len/mix", v_mix, true);

  {
    struct alignas(nsv_semver_t) blob {
      unsigned char b[256];
    };
    std::vector<blob> dst(v_mix.size());
    measure(cfg, "copy/mix", v_mix.size(), 0, [&](impl const &im, bool check) {
      uint64_t ck = 0;
      for (size_t i = 0; i < v_mix.size(); ++i) {
        int const n = im.copy(&dst[i], sizeof(blob), &v_mix[i]);
        if (!check) {
          ck += static_cast<uint64_t>(n);
          continue;
        }
        nsv_semver_t const *c = reinterpret_cast<nsv_semver_t const *>(&dst[i]);
        ck = fnv(ck, static_cast<uint64_t>(static_cast<int64_t>(n)));
        ck = fnv(ck, (uint64_t{ c->major } << 32) ^ (uint64_t{ c->build_ofs } << 16) ^
                         c->patch);
        ck = fnv_bytes(ck, reinterpret_cast<char const *>(c + 1),
                       static_cast<size_t>(n) - sizeof(nsv_semver_t));
      }
      return ck;
    });
  }
}

}  // namespace

int main(int argc, char **argv) {
  config cfg;
  for (int i = 1; i < argc; ++i) {
    std::string const a = argv[i];
    auto val = [&]() -> char const * {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--quick") {
      cfg.batch_ns = 5'000'000;
      cfg.samples = 5;
    } else if (a == "--filter") {
      cfg.filter = val();
    } else if (a == "--samples") {
      cfg.samples = std::max(1, std::atoi(val()));
    } else if (a == "--batch-ms") {
      cfg.batch_ns = static_cast<uint64_t>(std::max(1, std::atoi(val()))) * 1'000'000;
    } else if (a == "--ghz") {
      cfg.ghz = std::atof(val());
    } else if (a == "--only") {
      std::string const w = val();
      cfg.only = (w == "base") ? &k_base : &k_new;
    } else if (a == "--list") {
      cfg.list = true;
    } else if (a == "--dump") {
      cfg.dump = cfg.list = true;
    } else {
      std::fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  if (!cfg.list) {
    std::printf("# nanosemver A/B: base %s vs new (working tree)\n", NSV_BENCH_BASE);
    spin_warmup(300);
  }
  run_all(cfg);
  return report(cfg);
}
