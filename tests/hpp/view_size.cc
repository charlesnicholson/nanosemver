// What tests/hpp/view_size.c does through the C API, here through nsv::SemVerView:
// `make size-check` fails if this is larger.
#include "nanosemver.hpp"

extern "C" {
int nsv_view_pre(char const *s, size_t len, char const **pre);
int nsv_view_prefix_pre(char const *s, size_t len, char const **pre);
int nsv_view_prefix_major(char const *s, size_t len);
}

int nsv_view_pre(char const *s, size_t len, char const **pre) {
  int rc;
  nsv::SemVerView const v = nsv::SemVerView::from(s, len, rc);
  return (rc == NSV_RET_OK) ? v.pre(*pre) : rc;
}

int nsv_view_prefix_pre(char const *s, size_t len, char const **pre) {
  size_t n;
  int rc;
  nsv::SemVerView const v = nsv::SemVerView::from_prefix(s, len, n, rc);
  return (rc == NSV_RET_OK) ? v.pre(*pre) : rc;
}

int nsv_view_prefix_major(char const *s, size_t len) {  // 0.0.0 on error
  size_t n;
  int rc;
  return nsv::SemVerView::from_prefix(s, len, n, rc).matches("^1", 2);
}
