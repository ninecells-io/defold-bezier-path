#include <dmsdk/sdk.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "utils.h"
#include "buffer.h"
#include "stroke.h"

static void draw_circle_cap(
    const BufferInfo& buffer_info,
    float cx, float cy, float dx, float dy, float radius,
    SampleMask& mask, int segments = 16
) {
    const float nx = -dy;
    const float ny = dx;
    std::vector<std::pair<float, float> > arc_points;
    arc_points.reserve((size_t)segments + 2);
    arc_points.push_back(std::make_pair(cx, cy));
    for (int i = 0; i <= segments; ++i) {
        // dx/dy points outward from the endpoint; center the semicircle on it.
        const float angle = -M_PI * 0.5f + (M_PI * i / segments);
        const float rx = cosf(angle);
        const float ry = sinf(angle);
        const float x = rx * dx + ry * nx;
        const float y = rx * dy + ry * ny;
        arc_points.push_back(std::make_pair(cx + x * radius, cy + y * radius));
    }
    rasterize_polygon_to_sample_mask(buffer_info, arc_points, mask);
}

static void draw_stroke_cap(
    const BufferInfo& buffer_info,
    float cx, float cy, float dx, float dy,
    float width, int cap_style, SampleMask& mask
) {
    const float nx = -dy;
    const float ny = dx;
    const float half_w = width * 0.5f;
    if (cap_style == CAP_ROUND) {
        draw_circle_cap(buffer_info, cx, cy, dx, dy, half_w, mask);
    } else if (cap_style == CAP_SQUARE) {
        const float px = cx + dx * half_w;
        const float py = cy + dy * half_w;
        std::vector<std::pair<float, float> > cap;
        cap.push_back(std::make_pair(px + nx * half_w, py + ny * half_w));
        cap.push_back(std::make_pair(px - nx * half_w, py - ny * half_w));
        cap.push_back(std::make_pair(cx - nx * half_w, cy - ny * half_w));
        cap.push_back(std::make_pair(cx + nx * half_w, cy + ny * half_w));
        rasterize_polygon_to_sample_mask(buffer_info, cap, mask);
    }
}

static void draw_bevel_join(
    const BufferInfo& buffer_info,
    const std::pair<float, float>& p,
    const std::pair<float, float>& a,
    const std::pair<float, float>& b,
    SampleMask& mask
) {
    std::vector<std::pair<float, float> > triangle;
    triangle.push_back(a);
    triangle.push_back(b);
    triangle.push_back(p);
    rasterize_polygon_to_sample_mask(buffer_info, triangle, mask);
}

static void draw_round_join(
    const BufferInfo& buffer_info,
    const std::pair<float, float>& center,
    float radius, float a0, float a1,
    const std::pair<float, float>& a,
    const std::pair<float, float>& b,
    SampleMask& mask,
    int segments = 16
) {
    std::vector<std::pair<float, float> > points;
    points.reserve((size_t)segments + 2);
    points.push_back(center);
    float sweep = a1 - a0;
    if (sweep > M_PI) sweep -= M_TAU;
    if (sweep < -M_PI) sweep += M_TAU;
    const int steps = std::max(segments, 4);
    // Reuse the segment endpoints exactly; reconstructing them with sin/cos
    // can leave subpixel cracks where the sector meets a segment quad.
    points.push_back(a);
    for (int i = 1; i < steps; ++i) {
        const float t = (float)i / steps;
        const float angle = a0 + sweep * t;
        points.push_back(std::make_pair(
            center.first + cosf(angle) * radius,
            center.second + sinf(angle) * radius
        ));
    }
    points.push_back(b);
    rasterize_polygon_to_sample_mask(buffer_info, points, mask);
}

static void draw_join_at_vertex(
    const BufferInfo& buffer_info,
    const std::pair<float, float>& p0,
    const std::pair<float, float>& p1,
    const std::pair<float, float>& p2,
    float offset_in, float offset_out,
    int join_style,
    SampleMask& mask
) {
    float d0x = p1.first - p0.first;
    float d0y = p1.second - p0.second;
    float d1x = p2.first - p1.first;
    float d1y = p2.second - p1.second;
    const float len0 = std::hypot(d0x, d0y);
    const float len1 = std::hypot(d1x, d1y);
    if (len0 == 0.0f || len1 == 0.0f) return;
    d0x /= len0;
    d0y /= len0;
    d1x /= len1;
    d1y /= len1;

    const float n0x = -d0y;
    const float n0y = d0x;
    const float n1x = -d1y;
    const float n1y = d1x;
    const float cross = d0x * d1y - d0y * d1x;
    if (fabsf(cross) < 1e-6f) return;

    std::pair<float, float> a;
    std::pair<float, float> b;
    float radius;
    float a0;
    float a1;
    const bool left_turn = cross < 0.0f;
    if (left_turn) {
        radius = offset_out;
        if (radius <= 0.0f) return;
        a = std::make_pair(p1.first + n0x * radius, p1.second + n0y * radius);
        b = std::make_pair(p1.first + n1x * radius, p1.second + n1y * radius);
        a0 = std::atan2(n0y, n0x);
        a1 = std::atan2(n1y, n1x);
    } else {
        radius = offset_in;
        if (radius <= 0.0f) return;
        a = std::make_pair(p1.first - n0x * radius, p1.second - n0y * radius);
        b = std::make_pair(p1.first - n1x * radius, p1.second - n1y * radius);
        a0 = std::atan2(-n0y, -n0x);
        a1 = std::atan2(-n1y, -n1x);
    }

    if (join_style == JOIN_MITER) {
        const float denominator = 1.0f + d0x * d1x + d0y * d1y;
        if (denominator > 1e-6f) {
            const float mx = ((a.first - p1.first) + (b.first - p1.first)) / denominator;
            const float my = ((a.second - p1.second) + (b.second - p1.second)) / denominator;
            // Limit sharp-corner spikes to four times the outer stroke offset.
            if (mx * mx + my * my <= 16.0f * radius * radius) {
                std::vector<std::pair<float, float> > join;
                join.push_back(p1);
                join.push_back(a);
                join.push_back(std::make_pair(p1.first + mx, p1.second + my));
                join.push_back(b);
                rasterize_polygon_to_sample_mask(buffer_info, join, mask);
                return;
            }
        }
        draw_bevel_join(buffer_info, p1, a, b, mask);
    } else if (join_style == JOIN_BEVEL) {
        draw_bevel_join(buffer_info, p1, a, b, mask);
    } else if (join_style == JOIN_ROUND) {
        draw_round_join(buffer_info, p1, radius, a0, a1, a, b, mask);
    }
}

static inline void blend_stroke_pixel(
    const BufferInfo& buffer_info,
    int x, int y,
    int r, int g, int b, int a,
    float coverage,
    bool round_values
) {
    if (coverage <= 0.0f) return;
    const float alpha_value = std::min(255.0f, a * std::min(coverage, 1.0f));
    uint8_t sa = round_values ? (uint8_t)std::round(alpha_value) : (uint8_t)alpha_value;
    uint8_t sr = (uint8_t)r;
    uint8_t sg = (uint8_t)g;
    uint8_t sb = (uint8_t)b;
    if (buffer_info.premultiply_alpha && sa < 255) {
        const float scale = sa / 255.0f;
        sr = round_values ? (uint8_t)std::round(sr * scale) : (uint8_t)(sr * scale);
        sg = round_values ? (uint8_t)std::round(sg * scale) : (uint8_t)(sg * scale);
        sb = round_values ? (uint8_t)std::round(sb * scale) : (uint8_t)(sb * scale);
    }

    uint8_t* dst = buffer_info.bytes;
    const int idx = (y * buffer_info.width + x) * buffer_info.channels;
    if (buffer_info.channels == 4) {
        const float src_a = sa / 255.0f;
        const float inv_a = 1.0f - src_a;
        const float source_weight = buffer_info.premultiply_alpha ? 1.0f : src_a;
        dst[idx + 0] = (uint8_t)(sr * source_weight + dst[idx + 0] * inv_a);
        dst[idx + 1] = (uint8_t)(sg * source_weight + dst[idx + 1] * inv_a);
        dst[idx + 2] = (uint8_t)(sb * source_weight + dst[idx + 2] * inv_a);
        dst[idx + 3] = (uint8_t)(sa + dst[idx + 3] * inv_a);
    } else if (buffer_info.channels == 3) {
        dst[idx + 0] = sr;
        dst[idx + 1] = sg;
        dst[idx + 2] = sb;
    }
}

void stroke_path(
    const BufferInfo& buffer_info,
    float width, int position_mode,
    int r, int g, int b, int a,
    int cap_style, int join_style
) {
    PathPointList* path = get_path_points(buffer_info.path_handle);
    if (!path || path->points.empty() || width <= 0.0f) return;

    const std::vector<std::pair<float, float> >* flat_path_ptr = nullptr;
    if (!get_flattened_path(path, flat_path_ptr)) return;
    const std::vector<std::pair<float, float> >& flat_path = *flat_path_ptr;
    const std::vector<uint8_t>& corner_flags = path->flattened_corner_flags;
    if (flat_path.size() < 2) return;

    float min_x = flat_path[0].first;
    float min_y = flat_path[0].second;
    float max_x = min_x;
    float max_y = min_y;
    for (size_t i = 1; i < flat_path.size(); ++i) {
        min_x = std::min(min_x, flat_path[i].first);
        min_y = std::min(min_y, flat_path[i].second);
        max_x = std::max(max_x, flat_path[i].first);
        max_y = std::max(max_y, flat_path[i].second);
    }

    SampleMask& mask = get_raster_scratch().mask;
    initialize_sample_mask(buffer_info, min_x, min_y, max_x, max_y, (join_style == JOIN_MITER ? 4.0f : 1.0f) * width + 1.0f, mask);
    if (mask.empty()) return;

    const float half_w = width * 0.5f;
    float offset_in = 0.0f;
    float offset_out = 0.0f;
    if (position_mode == 0) {
        offset_in = half_w;
        offset_out = half_w;
    } else if (position_mode == 1) {
        offset_in = 0.0f;
        offset_out = width;
    } else if (position_mode == 2) {
        offset_in = width;
        offset_out = 0.0f;
    }

    for (size_t i = 0; i + 1 < flat_path.size(); ++i) {
        const std::pair<float, float>& p0 = flat_path[i];
        const std::pair<float, float>& p1 = flat_path[i + 1];
        float dx = p1.first - p0.first;
        float dy = p1.second - p0.second;
        const float length = std::hypot(dx, dy);
        if (length == 0.0f) continue;
        dx /= length;
        dy /= length;
        const float nx = -dy;
        const float ny = dx;

        std::vector<std::pair<float, float> > quad;
        quad.reserve(4);
        quad.push_back(std::make_pair(p0.first + nx * offset_out, p0.second + ny * offset_out));
        quad.push_back(std::make_pair(p1.first + nx * offset_out, p1.second + ny * offset_out));
        quad.push_back(std::make_pair(p1.first - nx * offset_in, p1.second - ny * offset_in));
        quad.push_back(std::make_pair(p0.first - nx * offset_in, p0.second - ny * offset_in));
        rasterize_polygon_to_sample_mask(buffer_info, quad, mask);

        if (!path->is_closed) {
            if (i == 0) draw_stroke_cap(buffer_info, p0.first, p0.second, -dx, -dy, width, cap_style, mask);
            if (i + 2 == flat_path.size()) {
                draw_stroke_cap(buffer_info, p1.first, p1.second, dx, dy, width, cap_style, mask);
            }
        }

        if (i + 2 < flat_path.size()) {
            draw_join_at_vertex(
                buffer_info, p0, p1, flat_path[i + 2],
                offset_in, offset_out,
                (corner_flags.empty() || corner_flags[i + 1]) ? join_style : JOIN_ROUND, mask
            );
        }
    }

    if (path->is_closed && flat_path.size() >= 2) {
        const std::pair<float, float>& p0 = flat_path.back();
        const std::pair<float, float>& p1 = flat_path.front();
        float dx = p1.first - p0.first;
        float dy = p1.second - p0.second;
        const float length = std::hypot(dx, dy);
        if (length > 0.0f) {
            dx /= length;
            dy /= length;
            const float nx = -dy;
            const float ny = dx;
            std::vector<std::pair<float, float> > quad;
            quad.push_back(std::make_pair(p0.first + nx * offset_out, p0.second + ny * offset_out));
            quad.push_back(std::make_pair(p1.first + nx * offset_out, p1.second + ny * offset_out));
            quad.push_back(std::make_pair(p1.first - nx * offset_in, p1.second - ny * offset_in));
            quad.push_back(std::make_pair(p0.first - nx * offset_in, p0.second - ny * offset_in));
            rasterize_polygon_to_sample_mask(buffer_info, quad, mask);
        }

        if (flat_path.size() >= 3) {
            const size_t last = flat_path.size() - 1;
            {
                draw_join_at_vertex(
                    buffer_info, flat_path[last - 1], flat_path[last], flat_path[0],
                    offset_in, offset_out,
                    (corner_flags.empty() || corner_flags[last]) ? join_style : JOIN_ROUND, mask
                );
            }
            {
                draw_join_at_vertex(
                    buffer_info, flat_path[last], flat_path[0], flat_path[1],
                    offset_in, offset_out,
                    (corner_flags.empty() || corner_flags[0]) ? join_style : JOIN_ROUND, mask
                );
            }
        }
    }

    for (int local_y = 0; local_y < mask.height; ++local_y) {
        const int y = mask.y + local_y;
        for (int local_x = 0; local_x < mask.width; ++local_x) {
            const uint8_t count = sample_count(mask.samples[(size_t)local_y * mask.width + local_x]);
            if (count == 0) continue;
            blend_stroke_pixel(
                buffer_info, mask.x + local_x, y,
                r, g, b, a, count * (1.0f / 16.0f), false
            );
        }
    }
}

void stroke_path_glow(
    const BufferInfo& buffer_info,
    float width, float radius, int position_mode,
    int r, int g, int b, int a
) {
    if (radius <= 0.0f || a <= 0) return;
    PathPointList* path = get_path_points(buffer_info.path_handle);
    if (!path || path->points.empty()) return;

    const std::vector<std::pair<float, float> >* flat_path_ptr = nullptr;
    if (!get_flattened_path(path, flat_path_ptr)) return;
    const std::vector<std::pair<float, float> >& flat_path = *flat_path_ptr;
    if (flat_path.size() < 2) return;

    const SegmentIndex* segments = get_segment_index(path, path->is_closed);
    if (!segments) return;

    float base_offset = 0.0f;
    if (position_mode == 0) base_offset = 0.5f * std::max(0.0f, width);
    else if (position_mode == 1) base_offset = 0.0f;
    else if (position_mode == 2) base_offset = std::max(0.0f, width);
    else base_offset = 0.5f * std::max(0.0f, width);

    float min_x = flat_path[0].first;
    float min_y = flat_path[0].second;
    float max_x = min_x;
    float max_y = min_y;
    for (size_t i = 1; i < flat_path.size(); ++i) {
        min_x = std::min(min_x, flat_path[i].first);
        min_y = std::min(min_y, flat_path[i].second);
        max_x = std::max(max_x, flat_path[i].first);
        max_y = std::max(max_y, flat_path[i].second);
    }

    SampleMask& roi = get_raster_scratch().mask;
    initialize_sample_mask(
        buffer_info, min_x, min_y, max_x, max_y,
        base_offset + radius + 1.0f, roi
    );
    if (roi.empty()) return;
    std::vector<float>& coverage = get_raster_scratch().values;
    coverage.assign(roi.samples.size(), 0.0f);
    roi.samples.clear();

    for (int local_y = 0; local_y < roi.height; ++local_y) {
        const int y = roi.y + local_y;
        for (int local_x = 0; local_x < roi.width; ++local_x) {
            const int x = roi.x + local_x;
            const float center_x = buffer_info.origin_x + x + 0.5f;
            const float center_y = buffer_info.origin_y + buffer_info.height - y - 0.5f;
            const float center_distance = std::sqrt(
                segments->nearest_distance_squared(center_x, center_y)
            );
            const float sample_radius = 0.53125f;
            if (center_distance + sample_radius <= base_offset ||
                center_distance - sample_radius >= base_offset + radius) {
                continue;
            }
            float sum = 0.0f;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    float fx, fy;
                    sample_position(buffer_info, x, y, sx, sy, fx, fy);
                    const float distance = std::sqrt(segments->nearest_distance_squared(fx, fy));
                    if (distance > base_offset && distance < base_offset + radius) {
                        const float t = (distance - base_offset) / radius;
                        sum += 1.0f - (t * t * (3.0f - 2.0f * t));
                    }
                }
            }
            coverage[(size_t)local_y * roi.width + local_x] = sum * (1.0f / 16.0f);
        }
    }

    for (int local_y = 0; local_y < roi.height; ++local_y) {
        const int y = roi.y + local_y;
        for (int local_x = 0; local_x < roi.width; ++local_x) {
            const float value = coverage[(size_t)local_y * roi.width + local_x];
            if (value <= 0.0f) continue;
            blend_stroke_pixel(buffer_info, roi.x + local_x, y, r, g, b, a, value, true);
        }
    }
}
