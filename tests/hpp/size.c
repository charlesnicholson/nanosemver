/* What tests/hpp/size.cc does through nanosemver.hpp, here through the C API: `make
   size-check` fails if the C++ is larger. Each version is 0.0.0 on error, as there. */
#include "nanosemver.h"

typedef struct {
  nsv_semver_t v;
  char s[64];
} nsv_copy64_t;

int nsv_hpp_view_pre(char const *s, size_t len, char const **pre);
int nsv_hpp_view_prefix_match(char const *s, size_t len);
int nsv_hpp_copy_format(char const *s, size_t len, char *buf, size_t n);
int nsv_hpp_copy_prefix_match(char const *s, size_t len);
int nsv_hpp_copy_of_format(nsv_semver_t const *c, char *buf, size_t n);

static int nsv_hpp_copy(nsv_copy64_t *c, nsv_semver_t const *view, int rc) {
  int const n = (rc == NSV_RET_OK) ? nsv_copy(c, sizeof *c, view) : rc;
  if (n < 0) {
    nsv_semver_t const zero = { 0, 0, 0, NULL, 0, 0, 0, 0 };
    c->v = zero;
  }
  return (n < 0) ? n : NSV_RET_OK;
}

int nsv_hpp_view_pre(char const *s, size_t len, char const **pre) {
  nsv_semver_t v;
  int const rc = nsv_parse(&v, s, len);
  return (rc == NSV_RET_OK) ? nsv_get_pre(&v, pre) : rc;
}

int nsv_hpp_view_prefix_match(char const *s, size_t len) {
  nsv_semver_t v = { 0, 0, 0, NULL, 0, 0, 0, 0 };
  size_t n;
  (void)nsv_parse_prefix(&v, s, len, &n);
  return nsv_range_match("^1", 2, &v, 0);
}

int nsv_hpp_copy_format(char const *s, size_t len, char *buf, size_t n) {
  nsv_copy64_t c;
  nsv_semver_t view;
  int const rc = nsv_hpp_copy(&c, &view, nsv_parse(&view, s, len));
  return (rc == NSV_RET_OK) ? nsv_format(buf, n, &c.v) : rc;
}

int nsv_hpp_copy_prefix_match(char const *s, size_t len) {
  nsv_copy64_t c;
  nsv_semver_t view;
  size_t n;
  (void)nsv_hpp_copy(&c, &view, nsv_parse_prefix(&view, s, len, &n));
  return nsv_range_match("^1", 2, &c.v, 0);
}

int nsv_hpp_copy_of_format(nsv_semver_t const *from, char *buf, size_t n) {
  nsv_copy64_t c;
  int const rc = nsv_hpp_copy(&c, from, NSV_RET_OK);
  return (rc == NSV_RET_OK) ? nsv_format(buf, n, &c.v) : rc;
}
