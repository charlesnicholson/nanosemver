// Entry points into lit_c.c, which is compiled as C.
#pragma once

#include "nanosemver.h"

#ifdef __cplusplus
extern "C" {
#endif

nsv_semver_t const *lit_c_full(void);  // NSV_SEMVER_LIT(1, 4, 0, "rc.2", "g3f9a1c2")
nsv_semver_t const *lit_c_bare(void);  // NSV_SEMVER_LIT(2, 0, 0, "", "")
int lit_c_sort_check(void);            // 1 if a qsort from C sorts correctly
int lit_c_static_copy(char *out, size_t n);  // copies k_full into static storage, formats it

#ifdef __cplusplus
}
#endif
