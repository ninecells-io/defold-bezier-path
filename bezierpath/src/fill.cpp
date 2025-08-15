#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "utils.h"
#include "buffer.h"
#include "fill.h"

void fill_color(
    const BufferInfo& buffer_info,
    int r, int g, int b, int a
) {
    int width = buffer_info.width;
    int height = buffer_info.height;
    int channels = buffer_info.channels;
    bool premul = buffer_info.premultiply_alpha;

    // Clamp color values
    r = clamp(r, 0, 255);
    g = clamp(g, 0, 255);
    b = clamp(b, 0, 255);
    a = clamp(a, 0, 255);

    // If alpha is 0 and we are using RGBA, fill with 0
    uint8_t pr = r, pg = g, pb = b, pa = a;

    if (channels == 4 && premul && a < 255) {
        float alpha_scale = a / 255.0f;
        pr = static_cast<uint8_t>(r * alpha_scale);
        pg = static_cast<uint8_t>(g * alpha_scale);
        pb = static_cast<uint8_t>(b * alpha_scale);
    }

    uint8_t* ptr = buffer_info.bytes;
    size_t total_pixels = width * height;

    if (channels == 4) {
        for (size_t i = 0; i < total_pixels; ++i) {
            *ptr++ = pr;
            *ptr++ = pg;
            *ptr++ = pb;
            *ptr++ = pa;
        }
    }
    else if (channels == 3) {
        for (size_t i = 0; i < total_pixels; ++i) {
            *ptr++ = pr;
            *ptr++ = pg;
            *ptr++ = pb;
        }
    }
}

// --- fill_path (uses rasterize_polygon_to_coverage) ---
void fill_path(
    const BufferInfo& buffer_info,
    int r, int g, int b, int a
) {
    const int supersample = 4;
    const int width       = buffer_info.width;
    const int height      = buffer_info.height;
    const int channels    = buffer_info.channels;
    const bool premul     = buffer_info.premultiply_alpha;

    PathPointList* path_points = get_path_points(buffer_info.path_handle);
    if (!path_points || path_points->points.empty())
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(path_points->points, flat_path);
    if (flat_path.empty())
        return;

    const int s_width  = width  * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage((size_t)s_width * (size_t)s_height, 0.0f);

    // Build coverage with your fast rasterizer
    rasterize_polygon_to_coverage(buffer_info, flat_path, coverage, supersample);

    // Downsample and write to buffer
    uint8_t* dst = buffer_info.bytes;
    const float inv_samples = 1.0f / float(supersample * supersample);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int sy = 0; sy < supersample; ++sy) {
                const int cy = y * supersample + sy;
                const size_t row_off = (size_t)cy * (size_t)s_width;
                for (int sx = 0; sx < supersample; ++sx) {
                    const int cx = x * supersample + sx;
                    sum += coverage[row_off + (size_t)cx];
                }
            }

            float alpha = std::min(sum * inv_samples, 1.0f);
            if (alpha <= 0.0f)
                continue;

            const int idx = (y * width + x) * channels;
            uint8_t final_a = static_cast<uint8_t>(a * alpha);
            uint8_t final_r = static_cast<uint8_t>(r);
            uint8_t final_g = static_cast<uint8_t>(g);
            uint8_t final_b = static_cast<uint8_t>(b);

            if (premul && final_a < 255) {
                const float a_scale = final_a / 255.0f;
                final_r = (uint8_t)(final_r * a_scale);
                final_g = (uint8_t)(final_g * a_scale);
                final_b = (uint8_t)(final_b * a_scale);
            }

            if (channels == 4) {
                const float src_a = final_a / 255.0f;
                const float inv_a = 1.0f - src_a;

                const uint8_t dst_r = dst[idx + 0];
                const uint8_t dst_g = dst[idx + 1];
                const uint8_t dst_b = dst[idx + 2];
                const uint8_t dst_a = dst[idx + 3];

                dst[idx + 0] = (uint8_t)(final_r * src_a + dst_r * inv_a);
                dst[idx + 1] = (uint8_t)(final_g * src_a + dst_g * inv_a);
                dst[idx + 2] = (uint8_t)(final_b * src_a + dst_b * inv_a);
                dst[idx + 3] = (uint8_t)(final_a + dst_a * inv_a);
            } else if (channels == 3) {
                dst[idx + 0] = final_r;
                dst[idx + 1] = final_g;
                dst[idx + 2] = final_b;
            }
        }
    }
}

void fill_path_gradient(
    const BufferInfo& buffer_info, 
    int r1, int g1, int b1, int a1,
    int r2, int g2, int b2, int a2,
    float angle_deg
) {
    const int supersample = 4;
    const int width       = buffer_info.width;
    const int height      = buffer_info.height;
    const int channels    = buffer_info.channels;
    const bool premul     = buffer_info.premultiply_alpha;

    PathPointList* path_points = get_path_points(buffer_info.path_handle);
    if (!path_points || path_points->points.empty() || width <= 0 || height <= 0 || channels < 3)
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(path_points->points, flat_path);
    if (flat_path.empty())
        return;

    const int s_width  = width  * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage((size_t)s_width * (size_t)s_height, 0.0f);

    // Build coverage via rasterizer
    rasterize_polygon_to_coverage(buffer_info, flat_path, coverage, supersample);

    // Gradient direction
    const float angle_rad = angle_deg * (float)M_PI / 180.0f;
    const float gx = cosf(angle_rad);
    const float gy = sinf(angle_rad);

    const float inv_ss  = 1.0f / (float)supersample;
    const float half_ss = 0.5f * inv_ss;

    // Compute min_t / max_t only over covered samples
    float min_t =  std::numeric_limits<float>::infinity();
    float max_t = -std::numeric_limits<float>::infinity();

    for (int cy = 0; cy < s_height; ++cy) {
        const size_t row_off = (size_t)cy * (size_t)s_width;
        for (int cx = 0; cx < s_width; ++cx) {
            if (coverage[row_off + (size_t)cx] > 0.0f) {
                const float fx = (float)cx * inv_ss + half_ss;
                const float fy = (float)(s_height - cy - 1) * inv_ss + half_ss;
                const float t  = fx * gx + fy * gy;
                if (t < min_t) min_t = t;
                if (t > max_t) max_t = t;
            }
        }
    }
    if (!std::isfinite(min_t) || !std::isfinite(max_t))
        return;

    const float range_t = std::max(1e-6f, max_t - min_t);

    // Downsample & blend
    uint8_t* dst = buffer_info.bytes;
    const float inv_samples = 1.0f / (float)(supersample * supersample);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum_coverage = 0.0f;
            float t_weighted   = 0.0f;

            for (int sy = 0; sy < supersample; ++sy) {
                const int cy = y * supersample + sy;
                const size_t row_off = (size_t)cy * (size_t)s_width;
                for (int sx = 0; sx < supersample; ++sx) {
                    const int cx = x * supersample + sx;
                    const float cov = coverage[row_off + (size_t)cx];
                    if (cov > 0.0f) {
                        const float fx = (float)cx * inv_ss + half_ss;
                        const float fy = (float)(s_height - cy - 1) * inv_ss + half_ss;
                        float t = (fx * gx + fy * gy - min_t) / range_t; // [0..1]
                        t = clamp(t, 0.0f, 1.0f);
                        t_weighted   += t * cov;
                        sum_coverage += cov;
                    }
                }
            }

            const float alpha = std::min(sum_coverage * inv_samples, 1.0f);
            if (alpha <= 0.0f)
                continue;

            const float t = t_weighted / (sum_coverage + 1e-6f);

            uint8_t sr = (uint8_t)((1.0f - t) * r1 + t * r2);
            uint8_t sg = (uint8_t)((1.0f - t) * g1 + t * g2);
            uint8_t sb = (uint8_t)((1.0f - t) * b1 + t * b2);
            uint8_t sa = (uint8_t)((1.0f - t) * a1 + t * a2);

            sa = (uint8_t)(sa * alpha);

            if (premul && sa < 255) {
                const float a_scale = sa / 255.0f;
                sr = (uint8_t)(sr * a_scale);
                sg = (uint8_t)(sg * a_scale);
                sb = (uint8_t)(sb * a_scale);
            }

            const int idx = (y * width + x) * channels;

            if (channels == 4) {
                const float src_a = sa / 255.0f;
                const float inv_a = 1.0f - src_a;

                const uint8_t dst_r = dst[idx + 0];
                const uint8_t dst_g = dst[idx + 1];
                const uint8_t dst_b = dst[idx + 2];
                const uint8_t dst_a = dst[idx + 3];

                dst[idx + 0] = (uint8_t)(sr * src_a + dst_r * inv_a);
                dst[idx + 1] = (uint8_t)(sg * src_a + dst_g * inv_a);
                dst[idx + 2] = (uint8_t)(sb * src_a + dst_b * inv_a);
                dst[idx + 3] = (uint8_t)(sa + dst_a * inv_a);
            } else if (channels == 3) {
                dst[idx + 0] = sr;
                dst[idx + 1] = sg;
                dst[idx + 2] = sb;
            }
        }
    }
}

void mask_path(
    const BufferInfo& buffer_info,
    bool inverted // false = mask inside, true = mask outside
) {
    const int supersample = 4;
    const int width       = buffer_info.width;
    const int height      = buffer_info.height;
    const int channels    = buffer_info.channels;

    if (channels != 4 || width <= 0 || height <= 0)
        return;

    PathPointList* path_points = get_path_points(buffer_info.path_handle);
    if (!path_points || path_points->points.empty())
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(path_points->points, flat_path);
    if (flat_path.empty())
        return;

    const int s_width  = width  * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage((size_t)s_width * (size_t)s_height, 0.0f);

    // Build coverage once (antialiased samples)
    rasterize_polygon_to_coverage(buffer_info, flat_path, coverage, supersample);

    uint8_t* dst       = buffer_info.bytes;
    const bool premul  = buffer_info.premultiply_alpha;
    const uint8_t target_alpha = 2; // the mask value
    const float inv_samples = 1.0f / float(supersample * supersample);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {

            // Average coverage in [0..1]
            float sum = 0.0f;
            for (int sy = 0; sy < supersample; ++sy) {
                const int cy = y * supersample + sy;
                const size_t row_off = (size_t)cy * (size_t)s_width;
                for (int sx = 0; sx < supersample; ++sx) {
                    const int cx = x * supersample + sx;
                    sum += coverage[row_off + (size_t)cx];
                }
            }
            const float c = sum * inv_samples;              // inside coverage
            const float w = inverted ? (1.0f - c) : c;      // blend weight toward alpha=2

            if (w <= 0.0f)
                continue; // nothing to change for this pixel

            const int idx   = (y * width + x) * channels;
            const uint8_t old_a = dst[idx + 3];

            // Blend alpha toward 2 with weight w (AA edge)
            // Optional safety: don't *increase* alpha if old_a < 2. Uncomment to enforce:
            // const uint8_t tgt = (uint8_t)std::min<int>(target_alpha, old_a);
            const uint8_t tgt = target_alpha;

            float new_a_f = (1.0f - w) * old_a + w * tgt;
            // Round and clamp
            uint8_t new_a = (uint8_t)clamp<int>(int(std::lround(new_a_f)), 0, 255);

            if (new_a == old_a)
                continue;

            if (premul) {
                if (old_a == 0) {
                    // keep premultiplied color consistent (alpha 0 → color 0)
                    dst[idx + 0] = 0;
                    dst[idx + 1] = 0;
                    dst[idx + 2] = 0;
                } else {
                    // scale RGB so unpremultiplied color stays the same
                    const float scale = (float)new_a / (float)old_a;
                    int r = (int)std::lround(dst[idx + 0] * scale);
                    int g = (int)std::lround(dst[idx + 1] * scale);
                    int b = (int)std::lround(dst[idx + 2] * scale);
                    dst[idx + 0] = (uint8_t)clamp(r, 0, 255);
                    dst[idx + 1] = (uint8_t)clamp(g, 0, 255);
                    dst[idx + 2] = (uint8_t)clamp(b, 0, 255);
                }
            }

            dst[idx + 3] = new_a; // write blended alpha
        }
    }
}

