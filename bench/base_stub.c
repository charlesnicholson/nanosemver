/* Stand-ins for the functions a BENCH_BASE from before them lacks, so the benchmark
   still links; NSV_BENCH_ABSENT tells it to time this tree's alone. */
#include "nanosemver.h"

#if !NSV_BASE_HAS_RANGE
#define NSV_BENCH_ABSENT (-1000)

int nsvbase_parse_prefix(nsv_semver_t *out, char const *s, size_t len, size_t *consumed);
int nsvbase_range_match(char const *s, size_t len, nsv_semver_t const *v, unsigned flags);

int nsvbase_parse_prefix(nsv_semver_t *out, char const *s, size_t len, size_t *consumed) {
  (void)out;
  (void)s;
  (void)len;
  (void)consumed;
  return NSV_BENCH_ABSENT;
}

int nsvbase_range_match(char const *s, size_t len, nsv_semver_t const *v, unsigned flags) {
  (void)s;
  (void)len;
  (void)v;
  (void)flags;
  return NSV_BENCH_ABSENT;
}
#else
typedef int nsv_bench_base_has_range; /* ISO C wants a declaration */
#endif
