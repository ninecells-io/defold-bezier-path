#pragma once

struct BufferInfo
{
    dmBuffer::HBuffer buffer;
    int width;
    int height;
    int channels;
    bool premultiply_alpha;
    uint8_t *bytes;
    uint32_t src_size;
};

enum PointType
{
    POINT_TYPE_LINEAR,
    POINT_TYPE_BEZIER
};

struct PathPoint
{
    PointType type;
    float x;
    float y;
    float cx; // control point x (used only if type == POINT_TYPE_BEZIER)
    float cy; // control point y
};

// Global variables
inline std::vector<PathPoint> g_path_points;
inline bool path_closed = false;
inline BufferInfo buffer_info;

void read_and_validate_buffer_info(lua_State *L, int index);