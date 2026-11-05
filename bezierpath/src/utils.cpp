#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <limits>
#include <string>
#include <stack>
#include "buffer.h"
#include "utils.h"

RasterScratch& get_raster_scratch() {
    static RasterScratch scratch;
    return scratch;
}

// --- begin: min/max polyfill (safe for old C++ and Windows) ------------------
#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX 1
#endif

template<typename T> static inline T bp_min(T a, T b){ return (b < a)? b : a; }
template<typename T> static inline T bp_max(T a, T b){ return (a < b)? b : a; }
static inline float bp_minf(float a,float b){ return (b < a)? b : a; }
static inline float bp_maxf(float a,float b){ return (a < b)? b : a; }
static inline float bp_clampf(float x,float lo,float hi){ return bp_maxf(lo, bp_minf(x, hi)); }

// --- end: min/max polyfill ---------------------------------------------------

// --- helper: squared distance from point to infinite line through a-b
static inline float distance_point_to_line(
    float px, float py,
    float ax, float ay,
    float bx, float by
) {
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float len = std::hypot(vx, vy);
    if (len == 0.0f) return std::hypot(wx, wy);
    // area of parallelogram / base length = perpendicular distance
    float cross = std::fabs(vx * wy - vy * wx);
    return cross / len;
}

// --- helper: adaptive subdivision for one quadratic Bezier (p0, c, p2)
static void flatten_quadratic_adaptive(
    float x0, float y0,
    float cx, float cy,
    float x2, float y2,
    float tolerance_px, int depth, int max_depth,
    std::vector<std::pair<float,float>>& out_pts,
    std::vector<uint8_t>* flags_out,
    bool emit_start_point,
    bool end_is_corner                            
) {
    float d = distance_point_to_line(cx, cy, x0, y0, x2, y2);
    bool flat_enough = (d <= tolerance_px) || (depth >= max_depth);

    if (flat_enough) {
        if (emit_start_point) {
            out_pts.emplace_back(x0, y0);
            if (flags_out) flags_out->push_back(1);     // start of the curve is a real corner
        }
        out_pts.emplace_back(x2, y2);
        if (flags_out) flags_out->push_back(end_is_corner ? 1 : 0); // interior ≠ corner
        return;
    }

    // de Casteljau split at t=0.5
    float x01 = 0.5f * (x0 + cx), y01 = 0.5f * (y0 + cy);
    float x12 = 0.5f * (cx + x2), y12 = 0.5f * (cy + y2);
    float x_mid = 0.5f * (x01 + x12), y_mid = 0.5f * (y01 + y12);

    // left: end at midpoint -> NOT a corner
    flatten_quadratic_adaptive(
        x0, y0, x01, y01, x_mid, y_mid,
        tolerance_px, depth + 1, max_depth, out_pts, flags_out,
        /*emit_start_point=*/emit_start_point,
        /*end_is_corner=*/false
    );

    // right: start is midpoint (not emitted), end inherits corner-ness
    flatten_quadratic_adaptive(
        x_mid, y_mid, x12, y12, x2, y2,
        tolerance_px, depth + 1, max_depth, out_pts, flags_out,
        /*emit_start_point=*/false,
        /*end_is_corner=*/end_is_corner
    );
}

static inline int bp_isfinite(float x){ return std::isfinite(x) ? 1 : 0; }

void flatten_arc(
    float cx, float cy,
    float radius,
    float start_angle,
    float end_angle,
    bool  clockwise,
    std::vector<std::pair<float,float>>& out_pts,
    float tolerance_px,
    bool  emit_start_point
) {
    if (radius <= 0.0f) return;

    // Arc direction: clockwise => decreasing angles
    float sweep = end_angle - start_angle;
    if (clockwise) { while (sweep > 0.0f) sweep -= M_TAU; }
    else           { while (sweep < 0.0f) sweep += M_TAU; }
    if (fabsf(sweep) < 1e-7f) return;

    // Sagitta error bound: s = r*(1 - cos(Δ/2)) <= tol  => Δ_max = 2*acos(1 - tol/r)
    float tol   = bp_maxf(1e-6f, tolerance_px);
    float denom = bp_maxf(radius, tol);
    float c     = bp_clampf(1.0f - tol/denom, -1.0f, 1.0f);
    float max_d = 2.0f * acosf(c);
    if (!(max_d > 0.0f) || !bp_isfinite(max_d)) max_d = M_PI / 16.0f; // fallback
    max_d = bp_minf(max_d, M_PI / 2.0f); // ≤ 90°

    int   segments = (int)ceilf(fabsf(sweep) / max_d);
    if (segments < 1) segments = 1;
    float delta = sweep / (float)segments;

    auto pt = [&](float a){ return std::pair<float,float>(cx + radius * cosf(a), cy + radius * sinf(a)); };

    float a = start_angle;
    if (emit_start_point) out_pts.emplace_back(pt(a));
    for (int i = 0; i < segments; ++i) {
        a += delta;
        out_pts.emplace_back(pt(a));
    }
}

void flatten_path(
    const std::vector<PathPoint>& path,
    std::vector<std::pair<float, float>>& points,
    float tolerance_px,            // e.g. 0.25f
    int   max_depth,               // e.g. 10
    std::vector<uint8_t>* corner_flags_out
) {
    points.clear();
    if (corner_flags_out) corner_flags_out->clear();
    if (path.empty()) return;

    const size_t n = path.size();
    for (size_t i = 0; i < n; ++i)
    {
        const PathPoint& p0 = path[i];
        const PathPoint& p1 = path[(i + 1) % n];

        if (p1.type == POINT_TYPE_LINEAR)
        {
            // (existing code)
            points.emplace_back(p0.x, p0.y);
            if (corner_flags_out) corner_flags_out->push_back(1);
        }
        else if (p1.type == POINT_TYPE_QUAD_CURVE)
        {
            // (existing code using p1.cx/p1.cy)
            flatten_quadratic_adaptive(
                p0.x, p0.y, p1.cx, p1.cy, p1.x, p1.y,
                tolerance_px, 0, max_depth,
                points, corner_flags_out,
                /*emit_start_point=*/true,
                /*end_is_corner=*/true
            );
        }
        else if (p1.type == POINT_TYPE_ARC)
        {
            // ARC params live on the ENDPOINT (p1), same convention as QUAD
            std::vector<std::pair<float,float>> tmp;
            flatten_arc(
                p1.arc_cx, p1.arc_cy,
                fabsf(p1.arc_radius),
                p1.arc_start, p1.arc_end,
                (p1.arc_clockwise != 0),
                tmp, tolerance_px,
                /*emit_start_point=*/true
            );
            if (!tmp.empty()) {
                // interior points are smooth (0); mark the *last* point as a corner (1)
                if (corner_flags_out) {
                    if (tmp.size() > 1) {
                        for (size_t k = 0; k < tmp.size() - 1; ++k) corner_flags_out->push_back(0);
                    }
                    corner_flags_out->push_back(1);
                }
                points.insert(points.end(), tmp.begin(), tmp.end());
            }
        }
    }
}

bool get_flattened_path(
    PathPointList* path,
    const std::vector<std::pair<float, float> >*& points
) {
    points = nullptr;
    if (!path || path->points.empty()) return false;

    if (!path->flattened_valid) {
        flatten_path(path->points, path->flattened_points, 0.25f, 10, &path->flattened_corner_flags);
        if (path->flattened_points.empty()) return false;

        // Curves emit their endpoint, and the following segment emits it again.
        // Keep one vertex so stroke joins/caps see the adjacent nonzero segments.
        // The tolerance also absorbs trigonometric roundoff at arc endpoints.
        std::vector<std::pair<float, float> >& flat = path->flattened_points;
        std::vector<uint8_t>& flags = path->flattened_corner_flags;
        const auto same_vertex = [](const std::pair<float, float>& a,
                                    const std::pair<float, float>& b) {
            return std::hypot(a.first - b.first, a.second - b.second) <= 1e-4f;
        };
        size_t count = 0;
        for (size_t i = 0; i < flat.size(); ++i) {
            if (count > 0 && same_vertex(flat[count - 1], flat[i])) {
                flags[count - 1] |= flags[i];
            } else {
                flat[count] = flat[i];
                flags[count] = flags[i];
                ++count;
            }
        }
        if (path->is_closed && count > 1 && same_vertex(flat[0], flat[count - 1])) {
            flags[0] |= flags[count - 1];
            --count;
        }
        flat.resize(count);
        flags.resize(count);

        path->min_x = path->max_x = path->flattened_points[0].first;
        path->min_y = path->max_y = path->flattened_points[0].second;
        for (size_t i = 1; i < path->flattened_points.size(); ++i) {
            const std::pair<float, float>& point = path->flattened_points[i];
            path->min_x = std::min(path->min_x, point.first);
            path->min_y = std::min(path->min_y, point.second);
            path->max_x = std::max(path->max_x, point.first);
            path->max_y = std::max(path->max_y, point.second);
        }
        path->flattened_valid = true;
    }

    points = &path->flattened_points;
    return !points->empty();
}

const SegmentIndex* get_segment_index(PathPointList* path, bool closed) {
    const std::vector<std::pair<float, float> >* points = nullptr;
    if (!get_flattened_path(path, points) || points->size() < 2) return nullptr;
    if (!path->segment_index_valid || path->segment_index_closed != closed) {
        path->segment_index.build(*points, closed);
        path->segment_index_valid = true;
        path->segment_index_closed = closed;
    }
    return path->segment_index.empty() ? nullptr : &path->segment_index;
}

bool get_path_bounds(
    PathPointList* path,
    float padding,
    float& min_x,
    float& min_y,
    float& max_x,
    float& max_y
) {
    const std::vector<std::pair<float, float> >* points = nullptr;
    if (!get_flattened_path(path, points)) return false;
    padding = std::max(0.0f, padding);
    min_x = path->min_x - padding;
    min_y = path->min_y - padding;
    max_x = path->max_x + padding;
    max_y = path->max_y + padding;
    return true;
}

void initialize_sample_mask(
    const BufferInfo& buffer_info,
    float min_x,
    float min_y,
    float max_x,
    float max_y,
    float padding,
    SampleMask& mask
) {
    mask.clear();
    if (buffer_info.width <= 0 || buffer_info.height <= 0 ||
        !std::isfinite(min_x) || !std::isfinite(min_y) ||
        !std::isfinite(max_x) || !std::isfinite(max_y) ||
        max_x < min_x || max_y < min_y) {
        return;
    }

    const int x0 = std::max(0, (int)std::floor(min_x - buffer_info.origin_x - padding));
    const int x1 = std::min(buffer_info.width,
        (int)std::ceil(max_x - buffer_info.origin_x + padding));
    const int y0 = std::max(0, (int)std::floor(
        buffer_info.height - (max_y - buffer_info.origin_y) - padding));
    const int y1 = std::min(buffer_info.height, (int)std::ceil(
        buffer_info.height - (min_y - buffer_info.origin_y) + padding));

    if (x0 >= x1 || y0 >= y1) return;

    mask.x = x0;
    mask.y = y0;
    mask.width = x1 - x0;
    mask.height = y1 - y0;
    mask.samples.assign((size_t)mask.width * (size_t)mask.height, 0);
}

void sample_position(
    const BufferInfo& buffer_info,
    int pixel_x,
    int pixel_y,
    int sample_x,
    int sample_y,
    float& x,
    float& y
) {
    const float inv_ss = 0.25f;
    const int supersampled_x = pixel_x * 4 + sample_x;
    const int supersampled_y = pixel_y * 4 + sample_y;
    x = buffer_info.origin_x + (float)supersampled_x * inv_ss + 0.5f * inv_ss;
    y = buffer_info.origin_y +
        (float)(buffer_info.height * 4 - supersampled_y - 1) * inv_ss + 0.5f * inv_ss;
}

uint8_t sample_count(uint16_t samples) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_popcount((unsigned int)samples);
#else
    samples = (uint16_t)(samples - ((samples >> 1) & 0x5555));
    samples = (uint16_t)((samples & 0x3333) + ((samples >> 2) & 0x3333));
    return (uint8_t)((((samples + (samples >> 4)) & 0x0F0F) * 0x0101) >> 8);
#endif
}

void rasterize_polygon_to_sample_mask(
    const BufferInfo& buffer_info,
    const std::vector<std::pair<float, float> >& poly,
    SampleMask& mask
) {
    if (poly.size() < 3 || mask.empty()) return;

    std::vector<float>& intersections = get_raster_scratch().intersections;
    intersections.clear();
    intersections.reserve(poly.size());

    const int sample_x_min = std::max(0, mask.x * 4);
    const int sample_x_max = std::min(buffer_info.width * 4, (mask.x + mask.width) * 4);

    for (int py = mask.y; py < mask.y + mask.height; ++py) {
        for (int sy = 0; sy < 4; ++sy) {
            float unused_x = 0.0f;
            float fy = 0.0f;
            sample_position(buffer_info, 0, py, 0, sy, unused_x, fy);
            intersections.clear();

            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
                const float xi = poly[i].first;
                const float yi = poly[i].second;
                const float xj = poly[j].first;
                const float yj = poly[j].second;
                if ((yi > fy) != (yj > fy)) {
                    // Evaluate shared edges in the same direction regardless
                    // of polygon winding, including floating-point rounding.
                    const float low_x = yi < yj ? xi : xj;
                    const float low_y = yi < yj ? yi : yj;
                    const float high_x = yi < yj ? xj : xi;
                    const float high_y = yi < yj ? yj : yi;
                    intersections.push_back(
                        // This edge crosses the scanline, so yj != yi. Adding
                        // an epsilon here shifts shared edges in opposite
                        // directions and can leave uncovered stroke samples.
                        (high_x - low_x) * (fy - low_y) / (high_y - low_y) + low_x
                    );
                }
            }

            if (intersections.size() < 2) continue;
            std::sort(intersections.begin(), intersections.end());

            for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
                const float left = intersections[i];
                const float right = intersections[i + 1];
                int q0 = (int)std::ceil((left - buffer_info.origin_x) * 4.0f - 0.5f);
                int q1 = (int)std::ceil((right - buffer_info.origin_x) * 4.0f - 0.5f);
                q0 = std::max(q0, sample_x_min);
                q1 = std::min(q1, sample_x_max);

                for (int q = q0; q < q1; ++q) {
                    const int px = q >> 2;
                    const int sx = q & 3;
                    const size_t index = (size_t)(py - mask.y) * (size_t)mask.width +
                        (size_t)(px - mask.x);
                    mask.samples[index] |= (uint16_t)(1u << (sy * 4 + sx));
                }
            }
        }
    }
}

const SampleMask* get_fill_sample_mask(
    const BufferInfo& buffer_info,
    PathPointList* path
) {
    if (!path) return nullptr;
    if (path->fill_mask_valid &&
        path->fill_mask_revision == path->revision &&
        path->fill_mask_width == buffer_info.width &&
        path->fill_mask_height == buffer_info.height &&
        path->fill_mask_origin_x == buffer_info.origin_x &&
        path->fill_mask_origin_y == buffer_info.origin_y) {
        return &path->fill_mask;
    }

    const std::vector<std::pair<float, float> >* points = nullptr;
    if (!get_flattened_path(path, points) || points->size() < 3) return nullptr;

    initialize_sample_mask(
        buffer_info,
        path->min_x,
        path->min_y,
        path->max_x,
        path->max_y,
        1.0f,
        path->fill_mask
    );
    rasterize_polygon_to_sample_mask(buffer_info, *points, path->fill_mask);

    path->fill_mask_valid = true;
    path->fill_mask_revision = path->revision;
    path->fill_mask_width = buffer_info.width;
    path->fill_mask_height = buffer_info.height;
    path->fill_mask_origin_x = buffer_info.origin_x;
    path->fill_mask_origin_y = buffer_info.origin_y;
    return &path->fill_mask;
}

static inline float point_segment_distance_squared(
    float px, float py, const Segment& segment
) {
    const float vx = segment.bx - segment.ax;
    const float vy = segment.by - segment.ay;
    const float wx = px - segment.ax;
    const float wy = py - segment.ay;
    const float length_squared = vx * vx + vy * vy;
    if (length_squared <= 1e-12f) return wx * wx + wy * wy;
    float t = (vx * wx + vy * wy) / length_squared;
    t = bp_clampf(t, 0.0f, 1.0f);
    const float dx = px - (segment.ax + t * vx);
    const float dy = py - (segment.ay + t * vy);
    return dx * dx + dy * dy;
}

static inline float bounds_distance_squared(
    float x, float y, const SegmentTreeNode& node
) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (x < node.min_x) dx = node.min_x - x;
    else if (x > node.max_x) dx = x - node.max_x;
    if (y < node.min_y) dy = node.min_y - y;
    else if (y > node.max_y) dy = y - node.max_y;
    return dx * dx + dy * dy;
}

SegmentIndex::SegmentIndex() {}

void SegmentIndex::clear() {
    m_Segments.clear();
    m_Nodes.clear();
}

bool SegmentIndex::empty() const {
    return m_Segments.empty();
}

void SegmentIndex::build(
    const std::vector<std::pair<float, float> >& points,
    bool closed
) {
    m_Segments.clear();
    m_Nodes.clear();
    if (points.size() < 2) return;

    const size_t segment_count = closed ? points.size() : points.size() - 1;
    m_Segments.reserve(segment_count);
    for (size_t i = 0; i < segment_count; ++i) {
        const std::pair<float, float>& a = points[i];
        const std::pair<float, float>& b = points[(i + 1) % points.size()];
        Segment segment;
        segment.ax = a.first;
        segment.ay = a.second;
        segment.bx = b.first;
        segment.by = b.second;
        segment.min_x = std::min(segment.ax, segment.bx);
        segment.min_y = std::min(segment.ay, segment.by);
        segment.max_x = std::max(segment.ax, segment.bx);
        segment.max_y = std::max(segment.ay, segment.by);
        m_Segments.push_back(segment);
    }
    build_node(0, (int)m_Segments.size());
}

int SegmentIndex::build_node(int start, int count) {
    SegmentTreeNode node;
    node.min_x = node.min_y = std::numeric_limits<float>::infinity();
    node.max_x = node.max_y = -std::numeric_limits<float>::infinity();
    node.left = -1;
    node.right = -1;
    node.start = start;
    node.count = count;
    for (int i = start; i < start + count; ++i) {
        node.min_x = std::min(node.min_x, m_Segments[i].min_x);
        node.min_y = std::min(node.min_y, m_Segments[i].min_y);
        node.max_x = std::max(node.max_x, m_Segments[i].max_x);
        node.max_y = std::max(node.max_y, m_Segments[i].max_y);
    }

    const int node_index = (int)m_Nodes.size();
    m_Nodes.push_back(node);
    if (count <= 8) return node_index;

    const bool split_x = (node.max_x - node.min_x) >= (node.max_y - node.min_y);
    const int middle = start + count / 2;
    std::nth_element(
        m_Segments.begin() + start,
        m_Segments.begin() + middle,
        m_Segments.begin() + start + count,
        [split_x](const Segment& a, const Segment& b) {
            const float center_a = split_x ? (a.min_x + a.max_x) : (a.min_y + a.max_y);
            const float center_b = split_x ? (b.min_x + b.max_x) : (b.min_y + b.max_y);
            return center_a < center_b;
        }
    );

    const int left = build_node(start, middle - start);
    const int right = build_node(middle, start + count - middle);
    m_Nodes[node_index].left = left;
    m_Nodes[node_index].right = right;
    m_Nodes[node_index].count = 0;
    return node_index;
}

float SegmentIndex::nearest_node(
    int node_index, float x, float y, float best
) const {
    const SegmentTreeNode& node = m_Nodes[node_index];
    if (bounds_distance_squared(x, y, node) >= best) return best;

    if (node.left < 0) {
        for (int i = node.start; i < node.start + node.count; ++i) {
            best = std::min(best, point_segment_distance_squared(x, y, m_Segments[i]));
        }
        return best;
    }

    const float left_distance = bounds_distance_squared(x, y, m_Nodes[node.left]);
    const float right_distance = bounds_distance_squared(x, y, m_Nodes[node.right]);
    if (left_distance < right_distance) {
        best = nearest_node(node.left, x, y, best);
        best = nearest_node(node.right, x, y, best);
    } else {
        best = nearest_node(node.right, x, y, best);
        best = nearest_node(node.left, x, y, best);
    }
    return best;
}

float SegmentIndex::nearest_distance_squared(float x, float y) const {
    if (m_Nodes.empty()) return std::numeric_limits<float>::infinity();
    return nearest_node(0, x, y, std::numeric_limits<float>::infinity());
}

void rasterize_polygon_to_coverage(
    const BufferInfo& buffer_info,
    const std::vector<std::pair<float,float>>& poly,
    std::vector<float>& coverage,
    int supersample
) {
    if (supersample != 4 || poly.size() < 3) return;
    float min_x = poly[0].first;
    float min_y = poly[0].second;
    float max_x = min_x;
    float max_y = min_y;
    for (size_t i = 1; i < poly.size(); ++i) {
        min_x = std::min(min_x, poly[i].first);
        min_y = std::min(min_y, poly[i].second);
        max_x = std::max(max_x, poly[i].first);
        max_y = std::max(max_y, poly[i].second);
    }

    SampleMask mask;
    initialize_sample_mask(buffer_info, min_x, min_y, max_x, max_y, 1.0f, mask);
    rasterize_polygon_to_sample_mask(buffer_info, poly, mask);
    const int s_width = buffer_info.width * 4;
    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            const uint16_t bits = mask.samples[(size_t)y * mask.width + x];
            const int pixel_x = mask.x + x;
            const int pixel_y = mask.y + y;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    if (bits & (uint16_t)(1u << (sy * 4 + sx))) {
                        coverage[(pixel_y * 4 + sy) * s_width + pixel_x * 4 + sx] = 1.0f;
                    }
                }
            }
        }
    }
}

bool point_in_polygon(
    float x, float y, const std::vector<std::pair<float, float>>& poly
) {
    bool inside = false;
    size_t n = poly.size();
    if (n < 3) return false;

    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        float xi = poly[i].first, yi = poly[i].second;
        float xj = poly[j].first, yj = poly[j].second;

        bool intersect = (
            (yi > y) != (yj > y)
        ) && (
            x < (xj - xi) * (y - yi) / (yj - yi + 1e-6f) + xi
        );
        if (intersect)
            inside = !inside;
    }

    return inside;
}
