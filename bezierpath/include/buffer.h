#pragma once

#include <stdint.h>
#include <unordered_map>
#include <utility>
#include <vector>

struct SampleMask {
    int x;
    int y;
    int width;
    int height;
    std::vector<uint16_t> samples;

    SampleMask()
    : x(0), y(0), width(0), height(0) {}

    void clear() {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        samples.clear();
    }

    bool empty() const {
        return width <= 0 || height <= 0 || samples.empty();
    }
};

struct Segment {
    float ax;
    float ay;
    float bx;
    float by;
    float min_x;
    float min_y;
    float max_x;
    float max_y;
};

struct SegmentTreeNode {
    float min_x;
    float min_y;
    float max_x;
    float max_y;
    int left;
    int right;
    int start;
    int count;
};

class SegmentIndex {
public:
    SegmentIndex();

    void build(
        const std::vector<std::pair<float, float> >& points,
        bool closed
    );
    void clear();
    float nearest_distance_squared(float x, float y) const;
    bool empty() const;

private:
    int build_node(int start, int count);
    float nearest_node(int node_index, float x, float y, float best) const;

    std::vector<Segment> m_Segments;
    std::vector<SegmentTreeNode> m_Nodes;
};

struct BufferInfo {
    dmBuffer::HBuffer buffer;
    int width;
    int height;
    int channels;
    bool premultiply_alpha;
    uint8_t *bytes;
    uint32_t src_size;
    uint32_t path_handle;
    float origin_x;
    float origin_y;
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
    bool is_closed;
    uint32_t revision;

    bool flattened_valid;
    std::vector<std::pair<float, float> > flattened_points;
    std::vector<uint8_t> flattened_corner_flags;
    float min_x;
    float min_y;
    float max_x;
    float max_y;

    bool segment_index_valid;
    bool segment_index_closed;
    SegmentIndex segment_index;

    bool fill_mask_valid;
    uint32_t fill_mask_revision;
    int fill_mask_width;
    int fill_mask_height;
    float fill_mask_origin_x;
    float fill_mask_origin_y;
    SampleMask fill_mask;

    PathPointList()
    : is_closed(false)
    , revision(1)
    , flattened_valid(false)
    , min_x(0.0f)
    , min_y(0.0f)
    , max_x(0.0f)
    , max_y(0.0f)
    , segment_index_valid(false)
    , segment_index_closed(false)
    , fill_mask_valid(false)
    , fill_mask_revision(0)
    , fill_mask_width(0)
    , fill_mask_height(0)
    , fill_mask_origin_x(0.0f)
    , fill_mask_origin_y(0.0f) {}
};

// Global variables
extern std::unordered_map<uint32_t, PathPointList> g_path_points;

uint32_t create_path();
void delete_path(uint32_t index);
void clear_all_paths();

PathPointList* get_path_points(uint32_t index);
void invalidate_path(PathPointList* path);

BufferInfo read_and_validate_buffer_info(lua_State *L, int index);
