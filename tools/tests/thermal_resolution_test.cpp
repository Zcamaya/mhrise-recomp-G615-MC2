#include <cassert>
#include <limits>

#include "nnsdk/nvn/memory_pressure.h"
#include "nnsdk/nvn/thermal_resolution.h"

int main() {
    using nvn::next_thermal_resolution_level;

    assert(next_thermal_resolution_level(0, 0.64f) == 0);
    assert(next_thermal_resolution_level(0, 0.65f) == 1);
    assert(next_thermal_resolution_level(1, 0.80f) == 2);
    assert(next_thermal_resolution_level(2, 0.90f) == 3);
    assert(next_thermal_resolution_level(3, 0.96f) == 4);
    assert(next_thermal_resolution_level(4, 1.0f) == 4);

    assert(next_thermal_resolution_level(1, 0.60f) == 1);
    assert(next_thermal_resolution_level(1, 0.54f) == 0);
    assert(next_thermal_resolution_level(4, 0.85f) == 3);

    assert(next_thermal_resolution_level(2, -1.0f) == 2);
    assert(next_thermal_resolution_level(2, std::numeric_limits<float>::quiet_NaN()) == 2);
    assert(next_thermal_resolution_level(9, -1.0f) == 4);

    using nvn::next_thermal_fps_level;
    assert(next_thermal_fps_level(0, 0.79f) == 0);
    assert(next_thermal_fps_level(0, 0.80f) == 1);
    assert(next_thermal_fps_level(0, 0.95f) == 2);
    assert(next_thermal_fps_level(1, 0.95f) == 2);
    assert(next_thermal_fps_level(2, 1.0f) == 2);
    assert(next_thermal_fps_level(1, 0.68f) == 1);
    assert(next_thermal_fps_level(1, 0.67f) == 0);
    assert(next_thermal_fps_level(2, 0.87f) == 2);
    assert(next_thermal_fps_level(2, 0.85f) == 1);
    assert(next_thermal_fps_level(1, std::numeric_limits<float>::quiet_NaN()) == 1);

    using nvn::low_memory_pressure;
    constexpr long mib = 1024;
    assert(!low_memory_pressure(false, 1536 * mib, 8ull << 30, false));
    assert(low_memory_pressure(false, 1535 * mib, 8ull << 30, false));
    assert(low_memory_pressure(true, 1800 * mib, 8ull << 30, false));
    assert(!low_memory_pressure(true, 2048 * mib, 8ull << 30, false));
    assert(low_memory_pressure(true, -1, 8ull << 30, false));
    assert(low_memory_pressure(false, 4000 * mib, 4ull << 30, false));
    assert(low_memory_pressure(false, 4000 * mib, 8ull << 30, true));
}
