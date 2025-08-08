#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "buffer.h"
#include "utils.h"

// --- helper: squared distance from point to infinite line through a-b
static inline float distance_point_to_line(
    float px, float py,
    float ax, float ay,
    float bx, float by)
{
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

void flatten_path(
    std::vector<std::pair<float, float>>& points,
    float tolerance_px,            // e.g. 0.25f
    int   max_depth,               // e.g. 10
    std::vector<uint8_t>* corner_flags_out
) {
    points.clear();
    if (corner_flags_out) corner_flags_out->clear();
    if (g_path_points.empty()) return;

    const size_t n = g_path_points.size();
    for (size_t i = 0; i < n; ++i)
    {
        const PathPoint& p0 = g_path_points[i];
        const PathPoint& p1 = g_path_points[(i + 1) % n];

        if (p1.type == POINT_TYPE_LINEAR)
        {
            // emit the start of the segment
            points.emplace_back(p0.x, p0.y);
            if (corner_flags_out) corner_flags_out->push_back(1);
        }
        else if (p1.type == POINT_TYPE_BEZIER)
        {
            // Quadratic: p0 -> control (p1.cx, p1.cy) -> p1
            flatten_quadratic_adaptive(
                p0.x, p0.y, p1.cx, p1.cy, p1.x, p1.y,
                tolerance_px, 0, max_depth,
                points, corner_flags_out,
                /*emit_start_point=*/true,
                /*end_is_corner=*/true
            );
        }
    }
}

void rasterize_polygon_to_coverage(
    const std::vector<std::pair<float,float>>& poly,
    std::vector<float>& coverage,
    int supersample
) {
    const int s_width  = buffer_info.width  * supersample;
    const int s_height = buffer_info.height * supersample;

    // Compute bounds in device pixels
    float minx =  1e30f, miny =  1e30f;
    float maxx = -1e30f, maxy = -1e30f;
    for (auto& p : poly) {
        minx = std::min(minx, p.first);
        miny = std::min(miny, p.second);
        maxx = std::max(maxx, p.first);
        maxy = std::max(maxy, p.second);
    }

    // Expand by half a supersample to be safe, then clamp
    int x0 = std::max(0, (int)std::floor((minx - 1.0f) * supersample));
    int y0 = std::max(0, (int)std::floor((buffer_info.height - maxy - 1.0f) * supersample));
    int x1 = std::min(s_width,  (int)std::ceil ((maxx + 1.0f) * supersample));
    int y1 = std::min(s_height, (int)std::ceil ((buffer_info.height - miny + 1.0f) * supersample));

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
            if (point_in_polygon(fx, fy, poly))
                coverage[y * s_width + x] = 1.0f;
        }
    }
}

bool point_in_polygon(float x, float y, const std::vector<std::pair<float, float>>& poly)
{
    bool inside = false;
    size_t n = poly.size();

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