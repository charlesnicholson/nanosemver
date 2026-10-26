/* What tests/hpp/view_size.cc does through nanosemver.hpp, here through the C API:
   `make size-check` fails if SemVerView's code is larger. */
#include "nanosemver.h"

int nsv_view_pre(char const *s, size_t len, char const **pre);
int nsv_view_prefix_pre(char const *s, size_t len, char const **pre);
int nsv_view_prefix_major(char const *s, size_t len);

int nsv_view_pre(char const *s, size_t len, char const **pre) {
  nsv_semver_t v;
  int const rc = nsv_parse(&v, s, len);
  return (rc == NSV_RET_OK) ? nsv_get_pre(&v, pre) : rc;
}

int nsv_view_prefix_pre(char const *s, size_t len, char const **pre) {
  nsv_semver_t v;
  size_t n;
  int const rc = nsv_parse_prefix(&v, s, len, &n);
  return (rc == NSV_RET_OK) ? nsv_get_pre(&v, pre) : rc;
}

int nsv_view_prefix_major(char const *s, size_t len) {  /* 0.0.0 on error */
  nsv_semver_t v = { 0, 0, 0, NULL, 0, 0, 0, 0 };
  size_t n;
  (void)nsv_parse_prefix(&v, s, len, &n);
  return nsv_range_match("^1", 2, &v, 0);
}
