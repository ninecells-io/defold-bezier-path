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

void draw_circle_cap(float cx, float cy, float dx, float dy, float radius,
                            std::vector<float>& coverage, int supersample, int segments = 16)
{
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
void draw_stroke_cap(float cx, float cy, float dx, float dy, float width, int cap_style,
                             std::vector<float>& coverage, int supersample)
{
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

void draw_bevel_join(const std::pair<float,float>& p,
                            const std::pair<float,float>& a,
                            const std::pair<float,float>& b,
                            std::vector<float>& coverage, int supersample)
{
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

void draw_round_join(const std::pair<float,float>& c,
                            float r, float a0, float a1,
                            std::vector<float>& coverage, int supersample,
                            int segments = 16)
{
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

    printf("steps=%d\n", steps);
    printf("s_height=%d\n", s_height);
    printf("s_width=%d\n", s_width);



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
// Handles Defold's Y-down by doing the vector/angle math in a mirrored Y-up "math space".
inline void draw_join_at_vertex(const std::pair<float,float>& p0,
                                       const std::pair<float,float>& p1,
                                       const std::pair<float,float>& p2,
                                       float offset_in, float offset_out,
                                       int join_style,
                                       std::vector<float>& coverage, int supersample)
{
    // --- Step 1: Mirror Y into math space (Y-up) for geometry calculations ---
    auto mirrorY = [](const std::pair<float,float>& p) {
        return std::pair<float,float>(p.first, -p.second);
    };
    auto P0 = mirrorY(p0);
    auto P1 = mirrorY(p1);
    auto P2 = mirrorY(p2);

    // --- Step 2: Compute unit directions in Y-up space ---
    float v1x = P1.first - P0.first;
    float v1y = P1.second - P0.second;
    float L1  = std::hypot(v1x, v1y);
    if (L1 == 0.0f) return;
    v1x /= L1; v1y /= L1;

    float v2x = P2.first - P1.first;
    float v2y = P2.second - P1.second;
    float L2  = std::hypot(v2x, v2y);
    if (L2 == 0.0f) return;
    v2x /= L2; v2y /= L2;

    // --- Step 3: Normals in Y-up space ---
    float n1x = -v1y, n1y =  v1x;
    float n2x = -v2y, n2y =  v2x;

    // --- Step 4: Determine turn direction in Y-up space ---
    float cross_val = v1x * v2y - v1y * v2x;
    if (fabsf(cross_val) < 1e-6f)
        return; // collinear segments → no join
    bool left_turn = (cross_val < 0.0f);

    // --- Step 5: Convert normals back to Y-down for polygon coords ---
    auto toWorld = [](float nx, float ny) {
        return std::pair<float,float>(nx, -ny);
    };


        printf("left_turn=%d\n", left_turn);
        printf("join_style=%d\n", join_style);

    if (join_style == JOIN_BEVEL)
    {
        if (left_turn && offset_out > 0.0f)
        {
            auto N1 = toWorld(n1x, n1y);
            auto N2 = toWorld(n2x, n2y);
            draw_bevel_join(p1,
                { p1.first + N1.first * offset_out, p1.second + N1.second * offset_out },
                { p1.first + N2.first * offset_out, p1.second + N2.second * offset_out },
                coverage, supersample);
        }
        else if (!left_turn && offset_in > 0.0f)
        {
            auto N1 = toWorld(-n1x, -n1y);
            auto N2 = toWorld(-n2x, -n2y);
            draw_bevel_join(p1,
                { p1.first + N1.first * offset_in, p1.second + N1.second * offset_in },
                { p1.first + N2.first * offset_in, p1.second + N2.second * offset_in },
                coverage, supersample);
        }
        return;
    }

    if (join_style == JOIN_ROUND)
    {
        auto normalise_ccw = [&](float a0, float a1) {
            while (a1 <= a0) a1 += M_PI_2;
            return std::make_pair(a0, a1);
        };
        auto normalise_cw = [&](float a0, float a1) {
            while (a1 >= a0) a1 -= M_PI_2;
            return std::make_pair(a0, a1);
        };
        auto flipY_angle = [&](float a) {
            return -a; // Y-up → Y-down
        };

        if (left_turn && offset_out > 0.0f)
        {
printf("offset_out=%f\n", offset_out);
            float a0 = atan2f(n1y, n1x);
            float a1 = atan2f(n2y, n2x);
            auto angs = normalise_ccw(a0, a1);
            draw_round_join(p1, offset_out,
                            flipY_angle(angs.first), flipY_angle(angs.second),
                            coverage, supersample);
        }
        else if (!left_turn && offset_in > 0.0f)
        {
printf("offset_in=%f\n", offset_in);
            float m1x = -n1x, m1y = -n1y;
            float m2x = -n2x, m2y = -n2y;
            float a0 = atan2f(m1y, m1x);
            float a1 = atan2f(m2y, m2x);
            auto angs = normalise_cw(a0, a1);
            draw_round_join(p1, offset_in,
                            flipY_angle(angs.first), flipY_angle(angs.second),
                            coverage, supersample);
        }
    }
}

void stroke_path(float width, int position_mode, int r, int g, int b, int a, int cap_style, int join_style)
{
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
        /*for (int y = 0; y < s_height; ++y)
        {
            for (int x = 0; x < s_width; ++x)
            {
                float fx = (float)x / supersample + 0.5f / supersample;
                float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;
                if (point_in_polygon(fx, fy, quad))
                    coverage[y * s_width + x] = 1.0f;
            }
        }*/

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