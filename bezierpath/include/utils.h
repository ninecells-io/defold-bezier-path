#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_TAU
#define M_TAU 6.2831853071795864769f
#endif

#include "buffer.h"

template <typename T>
T clamp(T val, T min_val, T max_val)
{
    return (val < min_val) ? min_val : (val > max_val) ? max_val : val;
}

void flatten_arc(
    float cx, float cy,
    float radius,
    float start_angle,
    float end_angle,
    bool  clockwise,
    std::vector<std::pair<float,float>>& out_pts,
    float tolerance_px = 0.25f,
    bool  emit_start_point = false
);

void flatten_path(
    const std::vector<PathPoint>& path,
    std::vector<std::pair<float, float>>& points,
    float tolerance_px = 0.25f,  // e.g. 0.25f
    int   max_depth = 10,        // e.g. 10
    std::vector<uint8_t>* corner_flags_out = nullptr
);

bool point_in_polygon(
    float x,
    float y,
    const std::vector<std::pair<float, float>>& poly
);

void rasterize_polygon_to_coverage(
    const BufferInfo& buffer_info,
    const std::vector<std::pair<float,float>>& poly,
    std::vector<float>& coverage,
    int supersample
);