/* Compiled as C99: the header, NSV_SEMVER_LIT, the documented qsort wrapper, ranges,
   prefixes and the version macros all have to work from C, not just from the C++ tests. */
#include "lit_c.h"

#include <stdlib.h>
#include <string.h>

static nsv_semver_t const k_full = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
static nsv_semver_t const k_bare = NSV_SEMVER_LIT(2, 0, 0, "", "");
static nsv_semver_t const k_version = NSV_VERSION_LIT;

nsv_semver_t const *lit_c_full(void) { return &k_full; }
nsv_semver_t const *lit_c_bare(void) { return &k_bare; }
nsv_semver_t const *lit_c_version(void) { return &k_version; }

static int ver_qsort_cmp(void const *a, void const *b) {
  return nsv_cmp((nsv_semver_t const *)a, (nsv_semver_t const *)b) - 1;
}

int lit_c_sort_check(void) {
  static char const *const strs[] = { "2.0.0", "1.0.0-rc.1", "1.0.0", "1.0.0-alpha",
                                      "0.9.0", "1.0.0-beta.11", "1.0.0-beta.2" };
  static char const *const sorted[] = { "0.9.0", "1.0.0-alpha", "1.0.0-beta.2",
                                        "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "2.0.0" };
  enum { N = sizeof(strs) / sizeof(strs[0]) };
  nsv_semver_t v[N];
  char buf[32];
  int i;
  for (i = 0; i < N; ++i) {
    if (nsv_parse(&v[i], strs[i], strlen(strs[i])) != NSV_RET_OK) {
      return 0;
    }
  }
  qsort(v, N, sizeof(v[0]), ver_qsort_cmp);
  for (i = 0; i < N; ++i) {
    if ((nsv_format(buf, sizeof(buf), &v[i]) < 0) || strcmp(buf, sorted[i])) {
      return 0;
    }
  }
  return 1;
}

int lit_c_static_copy(char *out, size_t n) {
  static struct {
    nsv_semver_t v;
    char strs[32];
  } g_ver;
  int const rc = nsv_copy(&g_ver, sizeof(g_ver), &k_full);
  if (rc < 0) {
    return rc;
  }
  return nsv_format(out, n, &g_ver.v);
}

int lit_c_range_check(void) {
  static char const range[] = ">=1.2.0 <1.4.0 || ^2.1.0";
  static char const text[] = "1.3.0-rc.1, and so on";
  nsv_semver_t v;
  size_t n = 0;
  char buf[256];
  if ((nsv_parse_prefix(&v, text, sizeof(text) - 1, &n) != NSV_RET_OK) || (n != 10)) {
    return 0;
  }
  if ((nsv_range_match(range, sizeof(range) - 1, &v, 0) != 1) ||
      (nsv_range_match(range, sizeof(range) - 1, &v, NSV_RANGE_EXCLUDE_PRE) != 0) ||
      (nsv_range_match(range, sizeof(range) - 1, &k_full, 0) != 1) ||
      (nsv_range_match(range, sizeof(range) - 1, &k_bare, 0) != 0)) {
    return 0;
  }
  if ((nsv_range_match(">=1.2 !=1.3.1 <2", 16, &k_bare, 0) != 0) ||
      (nsv_range_match("1.x || 2.*", 10, &k_bare, 0) != 1) ||
      (nsv_range_match("^1.4", 4, &k_full, NSV_RANGE_EXCLUDE_PRE) != 0)) {
    return 0;
  }
  if ((nsv_format(buf, sizeof(buf), &k_version) < 0) ||
      (nsv_parse(&v, buf, strlen(buf)) != NSV_RET_OK)) {
    return 0;
  }
  return nsv_cmp(&v, &k_version) == NSV_CMP_EQ;
}
