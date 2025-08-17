#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "buffer.h"
#include "utils.h"

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

    // UIKit semantics: clockwise => decreasing angles
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

void rasterize_polygon_to_coverage(
    const BufferInfo& buffer_info,
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

bool point_in_polygon(
    float x, float y, const std::vector<std::pair<float, float>>& poly
) {
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