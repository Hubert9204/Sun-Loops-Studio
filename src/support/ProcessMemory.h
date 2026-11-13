#pragma once

#include <cstdint>

namespace sls
{
// The memory the program holds now (its resident set), in bytes; 0 where the system does not say (0.63.0: Linux and macOS).
std::int64_t residentMemoryBytes() noexcept;
} // namespace sls
