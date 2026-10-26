// What tests/hpp/size.c does through the C API, here through nanosemver.hpp: `make
// size-check` fails if this is larger.
#include "nanosemver.hpp"

extern "C" {
int nsv_hpp_view_pre(char const *s, size_t len, char const **pre);
int nsv_hpp_view_prefix_match(char const *s, size_t len);
int nsv_hpp_copy_format(char const *s, size_t len, char *buf, size_t n);
int nsv_hpp_copy_prefix_match(char const *s, size_t len);
int nsv_hpp_copy_of_format(nsv_semver_t const *c, char *buf, size_t n);
}

int nsv_hpp_view_pre(char const *s, size_t len, char const **pre) {
  int rc;
  nsv::SemVerView const v = nsv::SemVerView::from(s, len, rc);
  return (rc == NSV_RET_OK) ? v.pre(*pre) : rc;
}

int nsv_hpp_view_prefix_match(char const *s, size_t len) {
  size_t n;
  int rc;
  return nsv::SemVerView::from_prefix(s, len, n, rc).matches("^1", 2);
}

int nsv_hpp_copy_format(char const *s, size_t len, char *buf, size_t n) {
  int rc;
  nsv::SemVer const v = nsv::SemVer::from(s, len, rc);
  return (rc == NSV_RET_OK) ? v.format(buf, n) : rc;
}

int nsv_hpp_copy_prefix_match(char const *s, size_t len) {
  size_t n;
  int rc;
  return nsv::SemVer::from_prefix(s, len, n, rc).matches("^1", 2);
}

int nsv_hpp_copy_of_format(nsv_semver_t const *c, char *buf, size_t n) {
  int rc;
  nsv::SemVer const v = nsv::SemVer::from(*c, rc);
  return (rc == NSV_RET_OK) ? v.format(buf, n) : rc;
}
