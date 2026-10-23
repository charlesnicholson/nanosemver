// The largest length nanosemver.c reports through int. The 16-bit-int variant builds
// nanosemver.c and the tests alike with -DNSV_INT_MAX=32767.
#pragma once

#include <climits>

namespace nsv_test {
#ifdef NSV_INT_MAX
inline constexpr long k_int_max = NSV_INT_MAX;
#else
inline constexpr long k_int_max = INT_MAX;
#endif

inline bool fits_int(size_t n) { return n <= static_cast<size_t>(k_int_max); }
}  // namespace nsv_test
