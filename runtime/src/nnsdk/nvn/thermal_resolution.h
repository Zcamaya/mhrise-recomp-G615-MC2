#pragma once

namespace nvn {

inline int next_thermal_resolution_level(int current, float headroom) {
    constexpr float enter[] = {0.65f, 0.78f, 0.88f, 0.95f};
    constexpr float exit[] = {0.55f, 0.66f, 0.77f, 0.86f};
    constexpr int max_level = 4;

    if (current < 0) current = 0;
    if (current > max_level) current = max_level;
    if (!(headroom >= 0.0f)) return current;

    if (current < max_level && headroom >= enter[current]) return current + 1;
    if (current > 0 && headroom < exit[current - 1]) return current - 1;
    return current;
}

inline int next_thermal_fps_level(int current, float headroom) {
    constexpr float enter[] = {0.80f, 0.95f};
    constexpr float exit[] = {0.68f, 0.86f};
    constexpr int max_level = 2;

    if (current < 0) current = 0;
    if (current > max_level) current = max_level;
    if (!(headroom >= 0.0f)) return current;

    if (headroom >= enter[1]) return max_level;
    if (current < max_level && headroom >= enter[current]) return current + 1;
    if (current > 0 && headroom < exit[current - 1]) return current - 1;
    return current;
}

}  // namespace nvn
