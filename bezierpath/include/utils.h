#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_TAU
#define M_TAU 6.2831853071795864769f
#endif

#include "buffer.h"

struct RasterScratch {
    SampleMask mask;
    std::vector<float> values;
    std::vector<float> temp;
    std::vector<float> intersections;
};

RasterScratch& get_raster_scratch();

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

bool get_flattened_path(
    PathPointList* path,
    const std::vector<std::pair<float, float> >*& points
);

const SegmentIndex* get_segment_index(PathPointList* path, bool closed);

bool get_path_bounds(
    PathPointList* path,
    float padding,
    float& min_x,
    float& min_y,
    float& max_x,
    float& max_y
);

void initialize_sample_mask(
    const BufferInfo& buffer_info,
    float min_x,
    float min_y,
    float max_x,
    float max_y,
    float padding,
    SampleMask& mask
);

void rasterize_polygon_to_sample_mask(
    const BufferInfo& buffer_info,
    const std::vector<std::pair<float, float> >& poly,
    SampleMask& mask
);

const SampleMask* get_fill_sample_mask(
    const BufferInfo& buffer_info,
    PathPointList* path
);

uint8_t sample_count(uint16_t samples);

void sample_position(
    const BufferInfo& buffer_info,
    int pixel_x,
    int pixel_y,
    int sample_x,
    int sample_y,
    float& x,
    float& y
);
