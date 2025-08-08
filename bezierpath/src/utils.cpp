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
    bool emit_start_point)
{
    // flatness: distance of control to chord
    float d = distance_point_to_line(cx, cy, x0, y0, x2, y2);
    bool flat_enough = (d <= tolerance_px) || (depth >= max_depth);

    if (flat_enough) {
        if (emit_start_point) {
            out_pts.emplace_back(x0, y0);
            if (flags_out) flags_out->push_back(1);  // start/end of curve = original corners
        }
        out_pts.emplace_back(x2, y2);
        if (flags_out) flags_out->push_back(1);
        return;
    }

    // de Casteljau split at t=0.5
    float x01 = 0.5f * (x0 + cx);
    float y01 = 0.5f * (y0 + cy);
    float x12 = 0.5f * (cx + x2);
    float y12 = 0.5f * (cy + y2);
    float x_mid = 0.5f * (x01 + x12);
    float y_mid = 0.5f * (y01 + y12);

    // left:  (x0,y0) - (x01,y01) - (x_mid,y_mid)
    // right: (x_mid,y_mid) - (x12,y12) - (x2,y2)
    flatten_quadratic_adaptive(
        x0, y0, x01, y01, x_mid, y_mid,
        tolerance_px, depth + 1, max_depth, out_pts, flags_out, emit_start_point
    );

    // For the right half, don't re-emit the midpoint as a "corner".
    // We'll emit only its end (x2,y2) as a corner in the base case.
    flatten_quadratic_adaptive(
        x_mid, y_mid, x12, y12, x2, y2,
        tolerance_px, depth + 1, max_depth, out_pts, flags_out, false
    );
}

// Adaptive version.
// NOTE: tolerance_px is in *your path units* (you’re using pixel coords, so pixels).
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
                /*emit_start_point=*/true
            );
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

        bool intersect = ((yi > y) != (yj > y)) &&
                         (x < (xj - xi) * (y - yi) / (yj - yi + 1e-6f) + xi);
        if (intersect)
            inside = !inside;
    }

    return inside;
}