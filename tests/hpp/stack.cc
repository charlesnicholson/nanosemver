// nanosemver.hpp's stack, for `make size-check`: each member of SemVer, at the default size
// and at 4096, in a function of its own, compiled for Cortex-M0+ with -fstack-usage. Only
// assign_through_copy, the path for a source inside the version, may take a frame the
// version's size; at the default size, GCC would inline it but for NSV_HPP_NOINLINE.
#include "nanosemver.hpp"

#define NSV_STACK(NAME, T)                                                              \
  int nsv_stack_parse_##NAME(T &v, char const *s, size_t n) { return v.parse(s, n); }  \
  int nsv_stack_parse_nul_##NAME(T &v, char const *s) { return v.parse(s); }           \
  int nsv_stack_parse_prefix_##NAME(T &v, char const *s, size_t n, size_t &consumed) { \
    return v.parse_prefix(s, n, consumed);                                              \
  }                                                                                     \
  int nsv_stack_assign_##NAME(T &v, nsv_semver_t const &s) { return v.assign(s); }      \
  T nsv_stack_from_##NAME(nsv_semver_t const &s, int &rc) { return T::from(s, rc); }    \
  int nsv_stack_view_##NAME(T const &v, char const *range) {                            \
    nsv::SemVerView const w = v;                                                        \
    return w.matches(range);                                                            \
  }

NSV_STACK(default, nsv::SemVer<>)
NSV_STACK(4096, nsv::SemVer<4096>)
