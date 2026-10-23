/* Compiled as C99: the header, NSV_SEMVER_LIT, and the documented qsort wrapper all
   have to work from C, not just from the C++ tests. */
#include "lit_c.h"

#include <stdlib.h>
#include <string.h>

static nsv_semver_t const k_full = NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2");
static nsv_semver_t const k_bare = NSV_SEMVER_LIT(2, 0, 0, "", "");

nsv_semver_t const *lit_c_full(void) { return &k_full; }
nsv_semver_t const *lit_c_bare(void) { return &k_bare; }

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
