// libFuzzer entry point: clang -fsanitize=fuzzer. See `make libfuzzer`.
#include "check.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) {
  if (char const *f = nsv_fuzz::check(data, size)) {
    std::fprintf(stderr, "%s\n", f);
    std::abort();
  }
  return 0;
}
