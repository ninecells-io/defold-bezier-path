#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI_2
#define M_PI_2 6.2831853071795864769f
#endif


template <typename T>
T clamp(T val, T min_val, T max_val)
{
    return (val < min_val) ? min_val : (val > max_val) ? max_val : val;
}

void flatten_path(
    std::vector<std::pair<float, float>>& points,
    int curve_segments = 16,
    std::vector<uint8_t>* corner_flags_out = nullptr
);

bool point_in_polygon(
    float x,
    float y,
    const std::vector<std::pair<float, float>>& poly
);