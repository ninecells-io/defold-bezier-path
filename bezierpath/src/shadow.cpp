#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "utils.h"
#include "buffer.h"
#include "shadow.h"

// --- gaussian_blur: interior/border split + symmetric taps (no per-tap clamp on interior)

static inline float fast_exp(float x) { return expf(x); } // keep separate in case you swap impls

void gaussian_blur(std::vector<float>& image, int width, int height, int radius)
{
    if (radius <= 0 || width <= 0 || height <= 0) return;

    const int kernel_size = 2 * radius + 1;
    std::vector<float> kernel(kernel_size);
    const float sigma = radius / 2.0f;
    const float two_sigma2 = 2.0f * sigma * sigma;

    // Build normalized 1D kernel
    float sum = 0.0f;
    for (int i = 0; i < kernel_size; ++i) {
        const int x = i - radius;
        const float w = fast_exp(-(x * x) / two_sigma2);
        kernel[i] = w;
        sum += w;
    }
    const float inv_sum = 1.0f / sum;
    for (int i = 0; i < kernel_size; ++i) kernel[i] *= inv_sum;

    std::vector<float> temp((size_t)width * (size_t)height);

    // ---------- Horizontal pass ----------
    for (int y = 0; y < height; ++y) {
        const int row = y * width;

        // If the row is too narrow for an interior region, do the slow (clamped) version for all pixels
        if (width <= 2 * radius) {
            for (int x = 0; x < width; ++x) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = std::min(std::max(x + k, 0), width - 1);
                    acc += image[row + sx] * kernel[k + radius];
                }
                temp[row + x] = acc;
            }
            continue;
        }

        // Left border
        for (int x = 0; x < radius; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::min(std::max(x + k, 0), width - 1);
                acc += image[row + sx] * kernel[k + radius];
            }
            temp[row + x] = acc;
        }

        // Interior (hot path): symmetric taps, contiguous, no clamps
        for (int x = radius; x < width - radius; ++x) {
            float acc = image[row + x] * kernel[radius]; // center
            for (int k = 1; k <= radius; ++k) {
                const float w = kernel[radius + k];
                acc += (image[row + x - k] + image[row + x + k]) * w;
            }
            temp[row + x] = acc;
        }

        // Right border
        for (int x = width - radius; x < width; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::min(std::max(x + k, 0), width - 1);
                acc += image[row + sx] * kernel[k + radius];
            }
            temp[row + x] = acc;
        }
    }

    // ---------- Vertical pass ----------
    for (int x = 0; x < width; ++x) {

        if (height <= 2 * radius) {
            for (int y = 0; y < height; ++y) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int sy = std::min(std::max(y + k, 0), height - 1);
                    acc += temp[sy * width + x] * kernel[k + radius];
                }
                image[y * width + x] = acc;
            }
            continue;
        }

        // Top border
        for (int y = 0; y < radius; ++y) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::min(std::max(y + k, 0), height - 1);
                acc += temp[sy * width + x] * kernel[k + radius];
            }
            image[y * width + x] = acc;
        }

        // Interior
        for (int y = radius; y < height - radius; ++y) {
            const int idx = y * width + x;
            float acc = temp[idx] * kernel[radius];
            for (int k = 1; k <= radius; ++k) {
                const float w = kernel[radius + k];
                acc += (temp[idx - k * width] + temp[idx + k * width]) * w;
            }
            image[idx] = acc;
        }

        // Bottom border
        for (int y = height - radius; y < height; ++y) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::min(std::max(y + k, 0), height - 1);
                acc += temp[sy * width + x] * kernel[k + radius];
            }
            image[y * width + x] = acc;
        }
    }
}

// --- fill_shadow: cheaper downsample, ROI composite, branchless integer blend

// helper
static inline uint8_t lerp8(uint8_t dst, uint8_t src, uint8_t a) {
    // dst + ((src - dst) * a + 127) / 255
    int d = dst, s = src, A = a;
    return (uint8_t)(d + (((s - d) * A + 127) / 255));
}

void fill_shadow(
    const BufferInfo& buffer_info,
    int offset_x, int offset_y,
    int blur_radius,
    int r, int g, int b, int a,
    bool inner // false=outer drop shadow, true=inner shadow
) {
    const int supersample = 4;
    const int width    = buffer_info.width;
    const int height   = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul  = buffer_info.premultiply_alpha;

    PathPointList* path_points = get_path_points(buffer_info.path_handle);
    if (!path_points || path_points->points.empty() || width <= 0 || height <= 0)
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(path_points->points, flat_path);
    if (flat_path.empty())
        return;

    // --- 1) Supersampled coverage
    const int s_width  = width  * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage((size_t)s_width * (size_t)s_height, 0.0f);
    rasterize_polygon_to_coverage(buffer_info, flat_path, coverage, supersample);

    // --- 2) Downsample to mask M in [0..1]
    std::vector<float> mask((size_t)width * (size_t)height, 0.0f);
    const float inv_samples = 1.0f / float(supersample * supersample);
    for (int y = 0; y < height; ++y) {
        const int cy0 = y * supersample;
        float* mrow = &mask[(size_t)y * (size_t)width];
        for (int x = 0; x < width; ++x) {
            const int cx0 = x * supersample;
            float sum = 0.0f;
            const float* crow0 = &coverage[(size_t)(cy0 + 0) * (size_t)s_width + (size_t)cx0];
            const float* crow1 = &coverage[(size_t)(cy0 + 1) * (size_t)s_width + (size_t)cx0];
            const float* crow2 = &coverage[(size_t)(cy0 + 2) * (size_t)s_width + (size_t)cx0];
            const float* crow3 = &coverage[(size_t)(cy0 + 3) * (size_t)s_width + (size_t)cx0];
            sum += crow0[0] + crow0[1] + crow0[2] + crow0[3];
            sum += crow1[0] + crow1[1] + crow1[2] + crow1[3];
            sum += crow2[0] + crow2[1] + crow2[2] + crow2[3];
            sum += crow3[0] + crow3[1] + crow3[2] + crow3[3];
            mrow[x] = sum * inv_samples;
        }
    }

    // --- 3) Blur M -> B
    std::vector<float> blurred = mask;
    gaussian_blur(blurred, width, height, blur_radius);

    // --- 4) Composite (Defold y=0 bottom: NO Y FLIP)
    uint8_t* dst = buffer_info.bytes;

    // Premultiplied color LUT
    uint8_t lut_r[256], lut_g[256], lut_b[256];
    if (premul) {
        for (int sa = 0; sa < 256; ++sa) {
            lut_r[sa] = (uint8_t)((r * sa + 127) / 255);
            lut_g[sa] = (uint8_t)((g * sa + 127) / 255);
            lut_b[sa] = (uint8_t)((b * sa + 127) / 255);
        }
    } else {
        for (int sa = 0; sa < 256; ++sa) {
            lut_r[sa] = (uint8_t)r;
            lut_g[sa] = (uint8_t)g;
            lut_b[sa] = (uint8_t)b;
        }
    }

    if (!inner) {
        // ---------- OUTER shadow ----------
        // ROI (avoid per-pixel bounds checks)
        const int x0 = std::max(0, -offset_x);
        const int y0 = std::max(0, -offset_y);
        const int x1 = std::min(width,  width  - offset_x);
        const int y1 = std::min(height, height - offset_y);
        if (x0 >= x1 || y0 >= y1) return;

        if (channels == 4) {
            for (int y = y0; y < y1; ++y) {
                const int sy       = y + offset_y;                 // no flip
                const int dst_row  = (sy * width) * channels;      // y grows upward
                const int mask_row = y * width;
                for (int x = x0; x < x1; ++x) {
                    float alpha_f = blurred[mask_row + x];
                    if (alpha_f < 0.f) alpha_f = 0.f;
                    if (alpha_f > 1.f) alpha_f = 1.f;
                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);

                    const int idx = dst_row + (x + offset_x) * channels;
                    const uint8_t sr = lut_r[sa], sg = lut_g[sa], sb = lut_b[sa];

                    dst[idx + 0] = lerp8(dst[idx + 0], sr, sa);
                    dst[idx + 1] = lerp8(dst[idx + 1], sg, sa);
                    dst[idx + 2] = lerp8(dst[idx + 2], sb, sa);

                    const uint8_t da = dst[idx + 3];
                    dst[idx + 3] = (uint8_t)(da + (((255 - da) * sa + 127) / 255));
                }
            }
        } else if (channels == 3) {
            for (int y = y0; y < y1; ++y) {
                const int sy       = y + offset_y;                 // no flip
                const int dst_row  = (sy * width) * channels;
                const int mask_row = y * width;
                for (int x = x0; x < x1; ++x) {
                    float alpha_f = blurred[mask_row + x];
                    if (alpha_f < 0.f) alpha_f = 0.f;
                    if (alpha_f > 1.f) alpha_f = 1.f;
                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);

                    uint8_t sr = (premul && sa < 255) ? (uint8_t)((r * sa + 127) / 255) : (uint8_t)r;
                    uint8_t sg = (premul && sa < 255) ? (uint8_t)((g * sa + 127) / 255) : (uint8_t)g;
                    uint8_t sb = (premul && sa < 255) ? (uint8_t)((b * sa + 127) / 255) : (uint8_t)b;

                    const int idx = dst_row + (x + offset_x) * channels;
                    dst[idx + 0] = sr; dst[idx + 1] = sg; dst[idx + 2] = sb;
                }
            }
        }
    } else {
        // ---------- INNER shadow ----------
        // alpha(x,y) = M(x,y) * (1 - B(x - off_x, y - off_y))
        if (channels == 4) {
            for (int y = 0; y < height; ++y) {
                const int dst_row  = (y * width) * channels; // no flip
                const int mask_row = y * width;

                for (int x = 0; x < width; ++x) {
                    const float m = mask[mask_row + x];
                    if (m <= 0.f) continue;

                    int sx = x - offset_x;
                    int sy = y - offset_y;
                    if (sx < 0) sx = 0; else if (sx >= width)  sx = width  - 1;
                    if (sy < 0) sy = 0; else if (sy >= height) sy = height - 1;

                    float bval = blurred[sy * width + sx];
                    float alpha_f = m * (1.0f - bval);
                    if (alpha_f <= 0.f) continue;
                    if (alpha_f > 1.f) alpha_f = 1.f;

                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);
                    const int idx = dst_row + x * channels;

                    const uint8_t sr = lut_r[sa], sg = lut_g[sa], sb = lut_b[sa];

                    dst[idx + 0] = lerp8(dst[idx + 0], sr, sa);
                    dst[idx + 1] = lerp8(dst[idx + 1], sg, sa);
                    dst[idx + 2] = lerp8(dst[idx + 2], sb, sa);

                    const uint8_t da = dst[idx + 3];
                    dst[idx + 3] = (uint8_t)(da + (((255 - da) * sa + 127) / 255));
                }
            }
        } else if (channels == 3) {
            for (int y = 0; y < height; ++y) {
                const int dst_row  = (y * width) * channels; // no flip
                const int mask_row = y * width;

                for (int x = 0; x < width; ++x) {
                    const float m = mask[mask_row + x];
                    if (m <= 0.f) continue;

                    int sx = x - offset_x;
                    int sy = y - offset_y;
                    if (sx < 0) sx = 0; else if (sx >= width)  sx = width  - 1;
                    if (sy < 0) sy = 0; else if (sy >= height) sy = height - 1;

                    float bval = blurred[sy * width + sx];
                    float alpha_f = m * (1.0f - bval);
                    if (alpha_f <= 0.f) continue;
                    if (alpha_f > 1.f) alpha_f = 1.f;

                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);

                    uint8_t sr = (premul && sa < 255) ? (uint8_t)((r * sa + 127) / 255) : (uint8_t)r;
                    uint8_t sg = (premul && sa < 255) ? (uint8_t)((g * sa + 127) / 255) : (uint8_t)g;
                    uint8_t sb = (premul && sa < 255) ? (uint8_t)((b * sa + 127) / 255) : (uint8_t)b;

                    const int idx = dst_row + x * channels;
                    dst[idx + 0] = sr; dst[idx + 1] = sg; dst[idx + 2] = sb;
                }
            }
        }
    }
}