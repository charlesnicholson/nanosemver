// The fuzz property check, shared by the unit tests, the standalone driver, and the
// libFuzzer entry point.
#pragma once

#include <cstddef>
#include <cstdint>

namespace nsv_fuzz {

// Input is one version string, or two separated by '\n'. Each is parsed and checked
// against the reference implementation: result code, fields, accessors, format
// round-trip, deep copy and relocation. Two that both parse are also compared, both
// ways. Returns nullptr on success, else a description of the first failed check.
char const *check(std::uint8_t const *data, std::size_t size);

}  // namespace nsv_fuzz
