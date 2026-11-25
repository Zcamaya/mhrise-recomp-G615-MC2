#pragma once

#include <cstdint>

namespace nvn {

inline bool low_memory_pressure(bool current, long available_kb, uint64_t total_bytes, bool forced) {
    constexpr long enter_kb = 1536L * 1024;
    constexpr long exit_kb = 2048L * 1024;

    if (forced || (total_bytes && total_bytes < (5ull << 30))) return true;
    if (available_kb < 0) return current;
    return current ? available_kb < exit_kb : available_kb < enter_kb;
}

}  // namespace nvn
