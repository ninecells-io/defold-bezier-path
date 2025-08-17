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
    POINT_TYPE_QUAD_CURVE,
    POINT_TYPE_ARC          // NEW
};

// --- extend PathPoint (keep existing fields as-is)
struct PathPoint {
    float x, y;        // anchor/end (always the segment end)
    PointType type;
    float cx, cy;      // (used for QUADRATIC; ignored otherwise)

    // --- ARC parameters (used only when type == POINT_TYPE_ARC)
    float arc_cx;        // center.x
    float arc_cy;        // center.y
    float arc_radius;    // radius >= 0
    float arc_start;     // start angle [rad]
    float arc_end;       // end angle [rad]
    uint8_t arc_clockwise; // 1 = clockwise (decreasing angles), 0 = ccw
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