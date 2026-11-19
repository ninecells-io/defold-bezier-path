#pragma once

#include "buffer.h"

enum RasterOperation {
    RASTER_FILL,
    RASTER_GRADIENT,
    RASTER_SHADOW,
    RASTER_MASK,
    RASTER_MASKED_SHADOW,
    RASTER_STROKE
};

struct RasterParameters {
    int color[4];
    int end_color[4];
    float angle;
    int offset_x;
    int offset_y;
    int radius;
    bool inner_or_inverted;
    uint32_t mask_handle;
    float stroke_width;
    int stroke_position;
    int stroke_cap;
    int stroke_join;
};

void initialize_raster_jobs();
void update_raster_jobs();
void finalize_raster_jobs();

// Internal Lua protocol: begin returns an id, nil for a small or unsupported
// draw, or nil, "busy" while a previous job is being retired.
int begin_raster_job(lua_State* L, const BufferInfo& buffer, RasterOperation operation, const RasterParameters& params);
int poll_raster_job(lua_State* L);
int cancel_raster_job(lua_State* L);
int set_raster_job_timing(lua_State* L);
