#pragma once

struct BufferInfo {
    dmBuffer::HBuffer buffer;
    int width;
    int height;
    int channels;
    bool premultiply_alpha;
    uint8_t *bytes;
    uint32_t src_size;
    uint32_t path_handle;
};

enum PointType {
    POINT_TYPE_LINEAR,
    POINT_TYPE_QUAD_CURVE
};

struct PathPoint {
    PointType type;
    float x;
    float y;
    float cx; // control point x (used only if type == POINT_TYPE_QUAD_CURVE)
    float cy; // control point y
};

struct PathPointList {
    std::vector<PathPoint> points;
    bool is_closed = false;
};

// Global variables
inline std::vector<PathPointList> g_path_points;

int create_path();
void delete_path(int index);

PathPointList* get_path_points(int index);

BufferInfo read_and_validate_buffer_info(lua_State *L, int index);