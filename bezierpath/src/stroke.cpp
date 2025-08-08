#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "utils.h"
#include "buffer.h"
#include "stroke.h"

void draw_circle_cap(
    float cx, float cy, float dx, float dy, float radius,
    std::vector<float>& coverage, int supersample, int segments = 16
) {
    const int s_width = buffer_info.width * supersample;
    const int s_height = buffer_info.height * supersample;

    // Compute perpendicular vector
    float nx = -dy;
    float ny = dx;

    std::vector<std::pair<float, float>> arc_points;
    arc_points.emplace_back(cx, cy); // Center

    for (int i = 0; i <= segments; ++i)
    {
        float angle = -M_PI_2 + (M_PI * i / segments); // From -90° to +90°
        float rx = cosf(angle);
        float ry = sinf(angle);

        // Rotate relative to dx/dy and nx/ny
        float x = rx * dx + ry * nx;
        float y = rx * dy + ry * ny;

        arc_points.emplace_back(cx + x * radius, cy + y * radius);
    }

    // Rasterize arc polygon into buffer
    for (int y = 0; y < s_height; ++y)
    {
        for (int x = 0; x < s_width; ++x)
        {
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;

            if (point_in_polygon(fx, fy, arc_points))
            {
                coverage[y * s_width + x] = 1.0f;
            }
        }
    }
}

// Stroke rendering with support for cap and join styles
void draw_stroke_cap(
    float cx, float cy, float dx, float dy, float width, int cap_style,
    std::vector<float>& coverage, int supersample
) {
    float nx = -dy;
    float ny = dx;
    const float half_w = width / 2.0f;
    const int s_width = buffer_info.width * supersample;
    const int s_height = buffer_info.height * supersample;

    if (cap_style == 1) // round
    {
        draw_circle_cap(cx, cy, dx, dy, half_w, coverage, supersample);
    }
    else if (cap_style == 2) // square
    {
        float px = cx + dx * half_w;
        float py = cy + dy * half_w;

        std::vector<std::pair<float, float>> cap = {
            {px + nx * half_w, py + ny * half_w},
            {px - nx * half_w, py - ny * half_w},
            {cx - nx * half_w, cy - ny * half_w},
            {cx + nx * half_w, cy + ny * half_w}
        };

        for (int y = 0; y < s_height; ++y)
        {
            for (int x = 0; x < s_width; ++x)
            {
                float fx = (float)x / supersample + 0.5f / supersample;
                float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
                if (point_in_polygon(fx, fy, cap))
                    coverage[y * s_width + x] = 1.0f;
            }
        }
    }
}

void draw_bevel_join(
    const std::pair<float,float>& p,
    const std::pair<float,float>& a,
    const std::pair<float,float>& b,
    std::vector<float>& coverage, int supersample
) {
    const int s_width  = buffer_info.width  * supersample;
    const int s_height = buffer_info.height * supersample;

    std::vector<std::pair<float,float>> poly = { a, b, p }; // triangle
    for (int y = 0; y < s_height; ++y)
    {
        for (int x = 0; x < s_width; ++x)
        {
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
            if (point_in_polygon(fx, fy, poly))
                coverage[y * s_width + x] = 1.0f;
        }
    }
}

void draw_round_join(
    const std::pair<float,float>& c,
    float r, float a0, float a1,
    std::vector<float>& coverage, int supersample,
    int segments = 16
) {
    // Normalize sweep to go the short way *in the intended direction*.
    // We assume a0->a1 is already chosen to be the exterior arc (see below).
    const int s_width  = buffer_info.width  * supersample;
    const int s_height = buffer_info.height * supersample;

    std::vector<std::pair<float,float>> arc_pts;
    arc_pts.emplace_back(c.first, c.second);

    // Ensure consistent direction (CCW if a1 > a0, CW if a1 < a0)
    float sweep = a1 - a0;
    if (sweep >  M_PI) sweep -= 2.0f * M_PI;
    if (sweep < -M_PI) sweep += 2.0f * M_PI;

    int steps = std::max(segments, 4);

    for (int i = 0; i <= steps; ++i)
    {
        float t = (float)i / steps;
        float a = a0 + sweep * t;
        arc_pts.emplace_back(c.first + cosf(a) * r, c.second + sinf(a) * r);
    }

    for (int y = 0; y < s_height; ++y)
    {
        for (int x = 0; x < s_width; ++x)
        {
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
            if (point_in_polygon(fx, fy, arc_pts))
                coverage[y * s_width + x] = 1.0f;
        }
    }
}

// Draw a join at vertex p1 between segments (p0->p1) and (p1->p2).
// Uses the same left-normal convention nx=-dy, ny=dx as elsewhere.
// For a left-turn we join on the "left/outside" using offset_out.
// For a right-turn we join on the "right/outside" using offset_in.
inline void draw_join_at_vertex(
    const std::pair<float,float>& p0,
    const std::pair<float,float>& p1,
    const std::pair<float,float>& p2,
    float offset_in, float offset_out,
    int join_style,
    std::vector<float>& coverage,
    int supersample
) {
    // Directions of incoming and outgoing segments
    float v0x = p1.first  - p0.first;
    float v0y = p1.second - p0.second;
    float v1x = p2.first  - p1.first;
    float v1y = p2.second - p1.second;

    float len0 = std::hypot(v0x, v0y);
    float len1 = std::hypot(v1x, v1y);
    if (len0 <= 0.0f || len1 <= 0.0f) return;

    v0x /= len0; v0y /= len0;
    v1x /= len1; v1y /= len1;

    // Left normals for each segment (consistent with the rest of the codebase)
    float n0x = -v0y, n0y =  v0x;
    float n1x = -v1y, n1y =  v1x;

    // Cross product to determine turning direction at the corner
    // (sign convention matches our vector math space used everywhere else)
    float cross = v0x * v1y - v0y * v1x;

    // Choose which side is the *exterior* of the bend and corresponding radius
    bool left_turn = (cross < 0.0f);

    // Outer points at the corner on each offset edge
    std::pair<float,float> A, B;   // endpoints of the wedge on the exterior side
    float radius = 0.0f;
    float a0 = 0.0f, a1 = 0.0f;    // angles for round join

    if (left_turn) {
        if (offset_out <= 0.0f) return; // nothing to draw on this side
        radius = offset_out;
        A = { p1.first + n0x * offset_out, p1.second + n0y * offset_out };
        B = { p1.first + n1x * offset_out, p1.second + n1y * offset_out };
        // Arc goes along the circle of normals on the left side
        a0 = std::atan2(n0y, n0x);
        a1 = std::atan2(n1y, n1x);
    } else {
        if (offset_in <= 0.0f) return; // nothing to draw on this side
        radius = offset_in;
        // Right side is the negative of the left normal
        A = { p1.first - n0x * offset_in, p1.second - n0y * offset_in };
        B = { p1.first - n1x * offset_in, p1.second - n1y * offset_in };
        // Use the right-side normals (i.e., -n) for the arc angles
        a0 = std::atan2(-n0y, -n0x);
        a1 = std::atan2(-n1y, -n1x);
    }

    if (join_style == JOIN_BEVEL) {
        // Simple triangle between the two exterior offset endpoints and the vertex.
        // (The strip fill from the segments covers the rest.)
        draw_bevel_join(p1, A, B, coverage, supersample);
    } else if (join_style == JOIN_ROUND) {
        // Exterior arc centered at the vertex, radius = chosen exterior offset.
        // draw_round_join will normalize sweep to the shortest path while keeping direction.
        draw_round_join(p1, radius, a0, a1, coverage, supersample);
    }
}

void stroke_path(
    float width, int position_mode,
    int r, int g, int b, int a,
    int cap_style, int join_style
) {
    const int img_w = buffer_info.width;
    const int img_h = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul = buffer_info.premultiply_alpha;

    if (g_path_points.empty())
        return;

    std::vector<std::pair<float, float>> flat_path;
    std::vector<uint8_t> corner_flags; // 1 = original corner (line-line or bezier endpoint), 0 = interior bezier sample
    flatten_path(flat_path, 16, &corner_flags);

    const float half_w = width / 2.0f;
    const int supersample = 4;
    const int s_width = img_w * supersample;
    const int s_height = img_h * supersample;

    float offset_in  = 0.0f;
    float offset_out = 0.0f;
    switch (position_mode) {
        case 0: // centered
            offset_in  = half_w;
            offset_out = half_w;
            break;
        case 1: // outside
            offset_in  = 0.0f;
            offset_out = width;
            break;
        case 2: // inside
            offset_in  = width;
            offset_out = 0.0f;
            break;
    }

    std::vector<float> coverage(s_width * s_height, 0.0f);

    for (size_t i = 0; i + 1 < flat_path.size(); ++i)
    {
        const auto& p0 = flat_path[i];
        const auto& p1 = flat_path[i + 1];

        float dx = p1.first - p0.first;
        float dy = p1.second - p0.second;
        float len = std::hypot(dx, dy);
        if (len == 0.0f) continue;

        dx /= len; dy /= len;

        float nx = -dy, ny = dx;

        float offset_in = 0.0f, offset_out = 0.0f;
        if (position_mode == 0) { offset_in = half_w; offset_out = half_w; }
        else if (position_mode == 1) { offset_in = 0.0f; offset_out = width; }
        else if (position_mode == 2) { offset_in = width; offset_out = 0.0f; }

        float ax = p0.first + nx * offset_out;
        float ay = p0.second + ny * offset_out;
        float bx = p1.first + nx * offset_out;
        float by = p1.second + ny * offset_out;
        float cx = p1.first - nx * offset_in;
        float cy = p1.second - ny * offset_in;
        float dx_ = p0.first - nx * offset_in;
        float dy_ = p0.second - ny * offset_in;

        std::vector<std::pair<float, float>> quad = { {ax, ay}, {bx, by}, {cx, cy}, {dx_, dy_} };

        // Rasterize strip
        for (int y = 0; y < s_height; ++y)
        {
            for (int x = 0; x < s_width; ++x)
            {
                float fx = (float)x / supersample + 0.5f / supersample;
                float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
                if (point_in_polygon(fx, fy, quad))
                    coverage[y * s_width + x] = 1.0f;
            }
        }

        // Caps (unchanged)
        if (!path_closed)
        {
            if (i == 0) draw_stroke_cap(p0.first, p0.second, -dx, -dy, width, cap_style, coverage, supersample);
            if (i + 2 == flat_path.size()) draw_stroke_cap(p1.first, p1.second,  dx,  dy, width, cap_style, coverage, supersample);
        }

        // ---------------------------
        // Joins (ROUND / BEVEL)
        // ---------------------------
        if (join_style != JOIN_MITER)
        {
            // Create a join at p1 between [p0,p1] and [p1,p2]
            if (i + 2 < flat_path.size())
            {

                // Only at real corners (skip interior Bezier samples if you have flags)
                if (!corner_flags.empty() && !corner_flags[i + 1])
                {
                    // no join at this interior bezier sample
                }
                else
                {
                    const auto& p2 = flat_path[i + 2];
                    draw_join_at_vertex(p0, p1, p2, offset_in, offset_out, join_style, coverage, supersample);
                }
            }
        }
    }

    // If the path is closed, also join the last->first->second corner
    // If path is closed, add the last segment from last -> first
    if (path_closed && flat_path.size() >= 2)
    {
        const auto& p0 = flat_path[flat_path.size() - 1]; // last point
        const auto& p1 = flat_path[0];                    // first point

        float dx = p1.first - p0.first;
        float dy = p1.second - p0.second;
        float len = std::hypot(dx, dy);
        if (len > 0.0f)
        {
            dx /= len;
            dy /= len;
            float nx = -dy, ny = dx;

            float ax = p0.first + nx * offset_out;
            float ay = p0.second + ny * offset_out;
            float bx = p1.first + nx * offset_out;
            float by = p1.second + ny * offset_out;
            float cx = p1.first - nx * offset_in;
            float cy = p1.second - ny * offset_in;
            float dx_ = p0.first - nx * offset_in;
            float dy_ = p0.second - ny * offset_in;

            std::vector<std::pair<float, float>> quad = {
                {ax, ay}, {bx, by}, {cx, cy}, {dx_, dy_}
            };

            // Rasterize strip into coverage
            for (int y = 0; y < s_height; ++y)
            {
                for (int x = 0; x < s_width; ++x)
                {
                    float fx = (float)x / supersample + 0.5f / supersample;
                    float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
                    if (point_in_polygon(fx, fy, quad))
                        coverage[y * s_width + x] = 1.0f;
                }
            }
        }

        // And also draw the joins between the last segment and the first segment
        if (join_style != JOIN_MITER && flat_path.size() >= 3)
        {
            auto draw_join = [&](int i0, int k, int k2) {
                if (corner_flags.empty() || corner_flags[k])
                {
                    draw_join_at_vertex(
                        flat_path[i0], flat_path[k], flat_path[k2],
                        offset_in, offset_out, join_style, coverage, supersample
                    );
                }
            };

            draw_join(flat_path.size() - 2, flat_path.size() - 1, 0);
            draw_join(flat_path.size() - 1, 0, 1);
        }
    }

    // Final buffer write
    uint8_t* dst = buffer_info.bytes;
    float inv_samples = 1.0f / (supersample * supersample);

    for (int y = 0; y < img_h; ++y)
    {
        for (int x = 0; x < img_w; ++x)
        {
            float sum = 0.0f;

            for (int sy = 0; sy < supersample; ++sy)
            {
                for (int sx = 0; sx < supersample; ++sx)
                {
                    int cx = x * supersample + sx;
                    int cy = y * supersample + sy;
                    sum += coverage[cx + cy * s_width];
                }
            }

            float alpha = std::min(sum * inv_samples, 1.0f);
            if (alpha <= 0.0f) continue;

            uint8_t sa = static_cast<uint8_t>(a * alpha);
            uint8_t sr = r, sg = g, sb = b;

            if (premul && sa < 255)
            {
                float scale = sa / 255.0f;
                sr = static_cast<uint8_t>(sr * scale);
                sg = static_cast<uint8_t>(sg * scale);
                sb = static_cast<uint8_t>(sb * scale);
            }

            int idx = (y * img_w + x) * channels;

            if (channels == 4)
            {
                float src_a = sa / 255.0f;
                float inv_a = 1.0f - src_a;

                dst[idx + 0] = static_cast<uint8_t>(sr * src_a + dst[idx + 0] * inv_a);
                dst[idx + 1] = static_cast<uint8_t>(sg * src_a + dst[idx + 1] * inv_a);
                dst[idx + 2] = static_cast<uint8_t>(sb * src_a + dst[idx + 2] * inv_a);
                dst[idx + 3] = static_cast<uint8_t>(sa + dst[idx + 3] * inv_a);
            }
            else if (channels == 3)
            {
                dst[idx + 0] = sr;
                dst[idx + 1] = sg;
                dst[idx + 2] = sb;
            }
        }
    }
}

// Soft glow outside the stroke.
// width        : stroke width in pixels
// radius       : glow thickness outside the outer stroke edge
// position_mode: StrokePosition { CENTER = 0, INSIDE, OUTSIDE }
// color        : r,g,b,a (a is max opacity right at the outer stroke edge)
void stroke_path_glow(float width, float radius, int position_mode, int r, int g, int b, int a)
{
    if (radius <= 0.0f || a <= 0) return;
    if (g_path_points.empty()) return;

    const int img_w = buffer_info.width;
    const int img_h = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul = buffer_info.premultiply_alpha;

    const int supersample = 4;
    const int s_width  = img_w * supersample;
    const int s_height = img_h * supersample;

    // Flatten path like stroke_path
    std::vector<std::pair<float, float>> flat_path;
    std::vector<uint8_t> corner_flags;
    flatten_path(flat_path, 16, &corner_flags);
    if (flat_path.size() < 2) return;

    // Distance helper (point -> segment)
    auto dist_point_seg = [](float px, float py,
                             float ax, float ay,
                             float bx, float by) -> float
    {
        float vx = bx - ax, vy = by - ay;
        float wx = px - ax, wy = py - ay;
        float vv = vx*vx + vy*vy;
        if (vv <= 0.0f) return std::hypot(px - ax, py - ay);
        float t = (vx*wx + vy*wy) / vv;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        float qx = ax + t*vx, qy = ay + t*vy;
        return std::hypot(px - qx, py - qy);
    };

    // Build segment list (respect path_closed)
    struct Seg { float ax, ay, bx, by; };
    std::vector<Seg> segs;
    segs.reserve(flat_path.size() + 1);
    for (size_t i = 0; i + 1 < flat_path.size(); ++i)
        segs.push_back({ flat_path[i].first, flat_path[i].second,
                         flat_path[i+1].first, flat_path[i+1].second });
    if (path_closed && flat_path.size() >= 2)
        segs.push_back({ flat_path.back().first,  flat_path.back().second,
                         flat_path.front().first, flat_path.front().second });

    // Where the glow begins (outer edge of stroke on the outside/left side)
    float base_offset = 0.0f;
    switch (position_mode) {
        case 0: /* CENTER  */ base_offset = 0.5f * std::max(0.0f, width); break;
        case 1: /* INSIDE  */ base_offset = 0.0f;                         break;
        case 2: /* OUTSIDE */ base_offset = std::max(0.0f, width);        break;
        default:              base_offset = 0.5f * std::max(0.0f, width); break;
    }

    // Supersampled glow buffer
    std::vector<float> glow(s_width * s_height, 0.0f);

    for (int y = 0; y < s_height; ++y)
    {
        for (int x = 0; x < s_width; ++x)
        {
            // Sample center (Y flipped to match your rasterization)
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;

            // Min distance to centerline segments
            float dmin = 1e30f;
            for (const auto& s : segs)
            {
                float d = dist_point_seg(fx, fy, s.ax, s.ay, s.bx, s.by);
                if (d < dmin) dmin = d;
                if (dmin <= base_offset) break; // inside stroke; no glow
            }

            // Only outside the stroke edge: (base_offset, base_offset + radius)
            if (dmin > base_offset && dmin < base_offset + radius)
            {
                float t = (dmin - base_offset) / radius;           // 0 at edge, 1 at edge+radius
                float sm = 1.0f - (t * t * (3.0f - 2.0f * t));     // smoothstep falloff 1..0
                glow[y * s_width + x] = sm;
            }
        }
    }

    // Composite glow into destination
    uint8_t* dst = buffer_info.bytes;
    const float inv_samples = 1.0f / (supersample * supersample);

    for (int y = 0; y < img_h; ++y)
    {
        for (int x = 0; x < img_w; ++x)
        {
            float sum = 0.0f;
            for (int sy = 0; sy < supersample; ++sy)
            for (int sx = 0; sx < supersample; ++sx)
            {
                int cx = x * supersample + sx;
                int cy = y * supersample + sy;
                sum += glow[cx + cy * s_width];
            }

            float cov = std::min(sum * inv_samples, 1.0f);
            if (cov <= 0.0f) continue;

            uint8_t sa = static_cast<uint8_t>(std::round(std::min(255.0f, a * cov)));
            uint8_t sr = r, sg = g, sb = b;

            if (premul && sa < 255)
            {
                float scale = sa / 255.0f;
                sr = static_cast<uint8_t>(std::round(sr * scale));
                sg = static_cast<uint8_t>(std::round(sg * scale));
                sb = static_cast<uint8_t>(std::round(sb * scale));
            }

            int idx = (y * img_w + x) * channels;

            if (channels == 4)
            {
                float src_a = sa / 255.0f;
                float inv_a = 1.0f - src_a;

                dst[idx + 0] = static_cast<uint8_t>(sr * src_a + dst[idx + 0] * inv_a);
                dst[idx + 1] = static_cast<uint8_t>(sg * src_a + dst[idx + 1] * inv_a);
                dst[idx + 2] = static_cast<uint8_t>(sb * src_a + dst[idx + 2] * inv_a);
                dst[idx + 3] = static_cast<uint8_t>(sa + dst[idx + 3] * inv_a);
            }
            else if (channels == 3)
            {
                dst[idx + 0] = sr;
                dst[idx + 1] = sg;
                dst[idx + 2] = sb;
            }
        }
    }
}
