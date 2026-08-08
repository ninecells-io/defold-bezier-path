#include <dmsdk/sdk.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "utils.h"
#include "buffer.h"
#include "fill.h"

static inline void blend_color(
    uint8_t* dst,
    int idx,
    int channels,
    bool premul,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a
) {
    uint8_t sr = r;
    uint8_t sg = g;
    uint8_t sb = b;
    if (premul && a < 255) {
        const float scale = a / 255.0f;
        sr = (uint8_t)(sr * scale);
        sg = (uint8_t)(sg * scale);
        sb = (uint8_t)(sb * scale);
    }

    if (channels == 4) {
        const float src_a = a / 255.0f;
        const float inv_a = 1.0f - src_a;
        dst[idx + 0] = (uint8_t)(sr * src_a + dst[idx + 0] * inv_a);
        dst[idx + 1] = (uint8_t)(sg * src_a + dst[idx + 1] * inv_a);
        dst[idx + 2] = (uint8_t)(sb * src_a + dst[idx + 2] * inv_a);
        dst[idx + 3] = (uint8_t)(a + dst[idx + 3] * inv_a);
    } else if (channels == 3) {
        dst[idx + 0] = sr;
        dst[idx + 1] = sg;
        dst[idx + 2] = sb;
    }
}

void fill_color(
    const BufferInfo& buffer_info,
    int r, int g, int b, int a
) {
    const int width = buffer_info.width;
    const int height = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul = buffer_info.premultiply_alpha;

    r = clamp(r, 0, 255);
    g = clamp(g, 0, 255);
    b = clamp(b, 0, 255);
    a = clamp(a, 0, 255);

    uint8_t pr = (uint8_t)r;
    uint8_t pg = (uint8_t)g;
    uint8_t pb = (uint8_t)b;
    const uint8_t pa = (uint8_t)a;
    if (channels == 4 && premul && a < 255) {
        const float alpha_scale = a / 255.0f;
        pr = (uint8_t)(r * alpha_scale);
        pg = (uint8_t)(g * alpha_scale);
        pb = (uint8_t)(b * alpha_scale);
    }

    uint8_t* ptr = buffer_info.bytes;
    const size_t total_pixels = (size_t)width * (size_t)height;
    if (channels == 4) {
        for (size_t i = 0; i < total_pixels; ++i) {
            *ptr++ = pr;
            *ptr++ = pg;
            *ptr++ = pb;
            *ptr++ = pa;
        }
    } else if (channels == 3) {
        for (size_t i = 0; i < total_pixels; ++i) {
            *ptr++ = pr;
            *ptr++ = pg;
            *ptr++ = pb;
        }
    }
}

void fill_path(
    const BufferInfo& buffer_info,
    int r, int g, int b, int a
) {
    PathPointList* path = get_path_points(buffer_info.path_handle);
    const SampleMask* mask = get_fill_sample_mask(buffer_info, path);
    if (!mask || mask->empty()) return;

    uint8_t* dst = buffer_info.bytes;
    for (int local_y = 0; local_y < mask->height; ++local_y) {
        const int y = mask->y + local_y;
        for (int local_x = 0; local_x < mask->width; ++local_x) {
            const uint16_t samples = mask->samples[(size_t)local_y * mask->width + local_x];
            const uint8_t count = sample_count(samples);
            if (count == 0) continue;
            const float alpha = count * (1.0f / 16.0f);
            const uint8_t final_a = (uint8_t)(a * alpha);
            const int x = mask->x + local_x;
            const int idx = (y * buffer_info.width + x) * buffer_info.channels;
            blend_color(
                dst, idx, buffer_info.channels, buffer_info.premultiply_alpha,
                (uint8_t)r, (uint8_t)g, (uint8_t)b, final_a
            );
        }
    }
}

void fill_path_gradient(
    const BufferInfo& buffer_info,
    int r1, int g1, int b1, int a1,
    int r2, int g2, int b2, int a2,
    float angle_deg
) {
    if (buffer_info.channels < 3) return;
    PathPointList* path = get_path_points(buffer_info.path_handle);
    const SampleMask* mask = get_fill_sample_mask(buffer_info, path);
    if (!mask || mask->empty()) return;

    const float angle_rad = angle_deg * (float)M_PI / 180.0f;
    const float gx = cosf(angle_rad);
    const float gy = sinf(angle_rad);
    float min_t = std::numeric_limits<float>::infinity();
    float max_t = -std::numeric_limits<float>::infinity();

    for (int local_y = 0; local_y < mask->height; ++local_y) {
        const int y = mask->y + local_y;
        for (int local_x = 0; local_x < mask->width; ++local_x) {
            const int x = mask->x + local_x;
            const uint16_t bits = mask->samples[(size_t)local_y * mask->width + local_x];
            if (bits == 0xffffu) {
                const int min_sx = gx >= 0.0f ? 0 : 3;
                const int max_sx = gx >= 0.0f ? 3 : 0;
                const int min_sy = gy >= 0.0f ? 3 : 0;
                const int max_sy = gy >= 0.0f ? 0 : 3;
                float min_x, min_y, max_x, max_y;
                sample_position(buffer_info, x, y, min_sx, min_sy, min_x, min_y);
                sample_position(buffer_info, x, y, max_sx, max_sy, max_x, max_y);
                min_t = std::min(min_t, min_x * gx + min_y * gy);
                max_t = std::max(max_t, max_x * gx + max_y * gy);
                continue;
            }
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    if (!(bits & (uint16_t)(1u << (sy * 4 + sx)))) continue;
                    float fx, fy;
                    sample_position(buffer_info, x, y, sx, sy, fx, fy);
                    const float t = fx * gx + fy * gy;
                    min_t = std::min(min_t, t);
                    max_t = std::max(max_t, t);
                }
            }
        }
    }
    if (!std::isfinite(min_t) || !std::isfinite(max_t)) return;
    const float range_t = std::max(1e-6f, max_t - min_t);

    uint8_t* dst = buffer_info.bytes;
    for (int local_y = 0; local_y < mask->height; ++local_y) {
        const int y = mask->y + local_y;
        for (int local_x = 0; local_x < mask->width; ++local_x) {
            const int x = mask->x + local_x;
            const uint16_t bits = mask->samples[(size_t)local_y * mask->width + local_x];
            const uint8_t count = sample_count(bits);
            if (count == 0) continue;

            float t_weighted = 0.0f;
            if (bits == 0xffffu) {
                const float fx = buffer_info.origin_x + x + 0.5f;
                const float fy = buffer_info.origin_y + buffer_info.height - y - 0.5f;
                t_weighted = 16.0f * clamp((fx * gx + fy * gy - min_t) / range_t, 0.0f, 1.0f);
            } else {
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        if (!(bits & (uint16_t)(1u << (sy * 4 + sx)))) continue;
                        float fx, fy;
                        sample_position(buffer_info, x, y, sx, sy, fx, fy);
                        float t = (fx * gx + fy * gy - min_t) / range_t;
                        t_weighted += clamp(t, 0.0f, 1.0f);
                    }
                }
            }

            const float coverage = count * (1.0f / 16.0f);
            const float t = t_weighted / (count + 1e-6f);
            const uint8_t sr = (uint8_t)((1.0f - t) * r1 + t * r2);
            const uint8_t sg = (uint8_t)((1.0f - t) * g1 + t * g2);
            const uint8_t sb = (uint8_t)((1.0f - t) * b1 + t * b2);
            uint8_t sa = (uint8_t)((1.0f - t) * a1 + t * a2);
            sa = (uint8_t)(sa * coverage);
            const int idx = (y * buffer_info.width + x) * buffer_info.channels;
            blend_color(dst, idx, buffer_info.channels, buffer_info.premultiply_alpha, sr, sg, sb, sa);
        }
    }
}

void fill_path_gradient_shape(
    const BufferInfo& buffer_info,
    int r_outer, int g_outer, int b_outer, int a_outer,
    int r_inner, int g_inner, int b_inner, int a_inner
) {
    if (buffer_info.channels < 3) return;
    PathPointList* path = get_path_points(buffer_info.path_handle);
    const SampleMask* mask = get_fill_sample_mask(buffer_info, path);
    const std::vector<std::pair<float, float> >* points = nullptr;
    if (!mask || mask->empty() || !get_flattened_path(path, points) || points->size() < 3) return;

    const SegmentIndex* segments = get_segment_index(path, true);
    if (!segments) return;

    std::vector<float>& distance_sums = get_raster_scratch().values;
    distance_sums.assign(mask->samples.size(), 0.0f);
    float max_distance = 0.0f;
    for (int local_y = 0; local_y < mask->height; ++local_y) {
        const int y = mask->y + local_y;
        for (int local_x = 0; local_x < mask->width; ++local_x) {
            const size_t pixel = (size_t)local_y * mask->width + local_x;
            const uint16_t bits = mask->samples[pixel];
            if (bits == 0) continue;
            const int x = mask->x + local_x;
            float sum = 0.0f;
            if (bits == 0xffffu) {
                // A checkerboard half of the 4x4 grid gives an unbiased
                // average for fully covered pixels. Boundary pixels retain
                // all 16 samples; fixture error stays at max channel delta 1.
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        if (((sx + sy) & 1) != 0) continue;
                        float fx, fy;
                        sample_position(buffer_info, x, y, sx, sy, fx, fy);
                        const float distance = std::sqrt(segments->nearest_distance_squared(fx, fy));
                        sum += distance * 2.0f;
                        max_distance = std::max(max_distance, distance);
                    }
                }
            } else {
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        if (!(bits & (uint16_t)(1u << (sy * 4 + sx)))) continue;
                        float fx, fy;
                        sample_position(buffer_info, x, y, sx, sy, fx, fy);
                        const float distance = std::sqrt(segments->nearest_distance_squared(fx, fy));
                        sum += distance;
                        max_distance = std::max(max_distance, distance);
                    }
                }
            }
            distance_sums[pixel] = sum;
        }
    }
    if (max_distance <= 1e-6f) return;

    uint8_t* dst = buffer_info.bytes;
    for (int local_y = 0; local_y < mask->height; ++local_y) {
        const int y = mask->y + local_y;
        for (int local_x = 0; local_x < mask->width; ++local_x) {
            const size_t pixel = (size_t)local_y * mask->width + local_x;
            const uint8_t count = sample_count(mask->samples[pixel]);
            if (count == 0) continue;
            float t = (distance_sums[pixel] / (count + 1e-6f)) / max_distance;
            t = clamp(t, 0.0f, 1.0f);
            const float coverage = count * (1.0f / 16.0f);
            const uint8_t sr = (uint8_t)((1.0f - t) * r_outer + t * r_inner);
            const uint8_t sg = (uint8_t)((1.0f - t) * g_outer + t * g_inner);
            const uint8_t sb = (uint8_t)((1.0f - t) * b_outer + t * b_inner);
            uint8_t sa = (uint8_t)((1.0f - t) * a_outer + t * a_inner);
            sa = (uint8_t)(sa * coverage);
            const int x = mask->x + local_x;
            const int idx = (y * buffer_info.width + x) * buffer_info.channels;
            blend_color(dst, idx, buffer_info.channels, buffer_info.premultiply_alpha, sr, sg, sb, sa);
        }
    }
}

void mask_path(
    const BufferInfo& buffer_info,
    bool inverted
) {
    if (buffer_info.channels != 4 || buffer_info.width <= 0 || buffer_info.height <= 0) return;
    PathPointList* path = get_path_points(buffer_info.path_handle);
    const SampleMask* mask = get_fill_sample_mask(buffer_info, path);
    if (!mask) return;

    uint8_t* dst = buffer_info.bytes;
    const uint8_t target_alpha = 2;
    const auto apply_mask = [&](int x, int y, float weight) {
        if (weight <= 0.0f) return;
        const int idx = (y * buffer_info.width + x) * 4;
        const uint8_t old_alpha = dst[idx + 3];
        const uint8_t new_alpha = (uint8_t)clamp<int>(
            (int)std::lround((1.0f - weight) * old_alpha + weight * target_alpha),
            0, 255
        );
        if (new_alpha == old_alpha) return;

        if (buffer_info.premultiply_alpha) {
            if (old_alpha == 0) {
                dst[idx + 0] = 0;
                dst[idx + 1] = 0;
                dst[idx + 2] = 0;
            } else {
                const float scale = (float)new_alpha / old_alpha;
                dst[idx + 0] = (uint8_t)clamp<int>((int)std::lround(dst[idx + 0] * scale), 0, 255);
                dst[idx + 1] = (uint8_t)clamp<int>((int)std::lround(dst[idx + 1] * scale), 0, 255);
                dst[idx + 2] = (uint8_t)clamp<int>((int)std::lround(dst[idx + 2] * scale), 0, 255);
            }
        }
        dst[idx + 3] = new_alpha;
    };

    if (inverted) {
        const int roi_x0 = mask->x;
        const int roi_y0 = mask->y;
        const int roi_x1 = mask->x + mask->width;
        const int roi_y1 = mask->y + mask->height;
        for (int y = 0; y < buffer_info.height; ++y) {
            if (y < roi_y0 || y >= roi_y1) {
                for (int x = 0; x < buffer_info.width; ++x) apply_mask(x, y, 1.0f);
            } else {
                for (int x = 0; x < roi_x0; ++x) apply_mask(x, y, 1.0f);
                for (int x = roi_x1; x < buffer_info.width; ++x) apply_mask(x, y, 1.0f);
            }
        }
    }

    for (int local_y = 0; local_y < mask->height; ++local_y) {
        const int y = mask->y + local_y;
        for (int local_x = 0; local_x < mask->width; ++local_x) {
            const float coverage = sample_count(
                mask->samples[(size_t)local_y * mask->width + local_x]
            ) * (1.0f / 16.0f);
            apply_mask(mask->x + local_x, y, inverted ? (1.0f - coverage) : coverage);
        }
    }
}
