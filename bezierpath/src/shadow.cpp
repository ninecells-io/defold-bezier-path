#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include <unordered_map>
#include <utility>
#include "utils.h"
#include "buffer.h"
#include "shadow.h"

// --- gaussian_blur: interior/border split + symmetric taps (no per-tap clamp on interior)

static inline float fast_exp(float x) { return expf(x); } // keep separate in case you swap impls

static const std::vector<float>& gaussian_kernel(int radius) {
    static std::unordered_map<int, std::vector<float> > kernels;
    std::unordered_map<int, std::vector<float> >::iterator found = kernels.find(radius);
    if (found != kernels.end()) return found->second;

    const int kernel_size = 2 * radius + 1;
    std::vector<float> kernel((size_t)kernel_size);
    const float sigma = radius / 2.0f;
    const float two_sigma2 = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int i = 0; i < kernel_size; ++i) {
        const int x = i - radius;
        const float weight = fast_exp(-(x * x) / two_sigma2);
        kernel[(size_t)i] = weight;
        sum += weight;
    }
    const float inv_sum = 1.0f / sum;
    for (int i = 0; i < kernel_size; ++i) kernel[(size_t)i] *= inv_sum;
    return kernels.emplace(radius, std::move(kernel)).first->second;
}

void gaussian_blur(std::vector<float>& image, int width, int height, int radius, bool clamp_edges)
{
    if (radius <= 0 || width <= 0 || height <= 0) return;

    const std::vector<float>& kernel = gaussian_kernel(radius);
    std::vector<float>& temp = get_raster_scratch().temp;
    temp.assign((size_t)width * (size_t)height, 0.0f);

    auto sample_h = [&](const float* rowp, int sx) -> float {
        if (clamp_edges) {
            if (sx < 0) sx = 0;
            else if (sx >= width) sx = width - 1;
            return rowp[sx];
        } else {
            if ((unsigned)sx >= (unsigned)width) return 0.0f;
            return rowp[sx];
        }
    };

    // ---------- Horizontal pass ----------
    for (int y = 0; y < height; ++y) {
        const int row = y * width;
        const float* rowp = &image[row];

        // Slow path for narrow rows (must handle borders anyway)
        if (width <= 2 * radius) {
            for (int x = 0; x < width; ++x) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    acc += sample_h(rowp, x + k) * kernel[k + radius];
                }
                temp[row + x] = acc;
            }
            continue;
        }

        // Left border
        for (int x = 0; x < radius; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                acc += sample_h(rowp, x + k) * kernel[k + radius];
            }
            temp[row + x] = acc;
        }

        // Interior (hot path): unchanged
        for (int x = radius; x < width - radius; ++x) {
            float acc = rowp[x] * kernel[radius];
            for (int k = 1; k <= radius; ++k) {
                const float w = kernel[radius + k];
                acc += (rowp[x - k] + rowp[x + k]) * w;
            }
            temp[row + x] = acc;
        }

        // Right border
        for (int x = width - radius; x < width; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                acc += sample_h(rowp, x + k) * kernel[k + radius];
            }
            temp[row + x] = acc;
        }
    }

    auto sample_v = [&](int sy, int x) -> float {
        if (clamp_edges) {
            if (sy < 0) sy = 0;
            else if (sy >= height) sy = height - 1;
            return temp[sy * width + x];
        } else {
            if ((unsigned)sy >= (unsigned)height) return 0.0f;
            return temp[sy * width + x];
        }
    };

    // ---------- Vertical pass ----------
    for (int x = 0; x < width; ++x) {

        if (height <= 2 * radius) {
            for (int y = 0; y < height; ++y) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    acc += sample_v(y + k, x) * kernel[k + radius];
                }
                image[y * width + x] = acc;
            }
            continue;
        }

        // Top border
        for (int y = 0; y < radius; ++y) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                acc += sample_v(y + k, x) * kernel[k + radius];
            }
            image[y * width + x] = acc;
        }

        // Interior: unchanged
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
                acc += sample_v(y + k, x) * kernel[k + radius];
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
    const int width    = buffer_info.width;
    const int height   = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul  = buffer_info.premultiply_alpha;

    PathPointList* path_points = get_path_points(buffer_info.path_handle);
    const SampleMask* sample_mask = get_fill_sample_mask(buffer_info, path_points);
    if (!sample_mask || sample_mask->empty() || width <= 0 || height <= 0) return;

    const int radius = std::max(0, blur_radius);
    const int blur_x0 = std::max(0, sample_mask->x - radius);
    const int blur_y0 = std::max(0, sample_mask->y - radius);
    const int blur_x1 = std::min(width, sample_mask->x + sample_mask->width + radius);
    const int blur_y1 = std::min(height, sample_mask->y + sample_mask->height + radius);
    const int blur_width = blur_x1 - blur_x0;
    const int blur_height = blur_y1 - blur_y0;
    if (blur_width <= 0 || blur_height <= 0) return;

    // Only the path bounds plus the blur padding are represented. Together
    // with gaussian_blur's reusable temporary this caps a full 512x512 shadow
    // at two float images plus the 16-bit fill mask (about 2.5 MiB).
    std::vector<float>& blurred = get_raster_scratch().values;
    blurred.assign((size_t)blur_width * (size_t)blur_height, 0.0f);
    for (int local_y = 0; local_y < sample_mask->height; ++local_y) {
        const int y = sample_mask->y + local_y;
        for (int local_x = 0; local_x < sample_mask->width; ++local_x) {
            const int x = sample_mask->x + local_x;
            blurred[(size_t)(y - blur_y0) * blur_width + (x - blur_x0)] = sample_count(
                sample_mask->samples[(size_t)local_y * sample_mask->width + local_x]
            ) * (1.0f / 16.0f);
        }
    }

    gaussian_blur(blurred, blur_width, blur_height, blur_radius, /*clamp_edges=*/ !inner);

    const auto blurred_at = [&](int x, int y) -> float {
        if (x < blur_x0 || y < blur_y0 || x >= blur_x1 || y >= blur_y1) return 0.0f;
        return blurred[(size_t)(y - blur_y0) * blur_width + (x - blur_x0)];
    };

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
        if (channels == 4) {
            for (int y = blur_y0; y < blur_y1; ++y) {
                const int sy = y + offset_y;
                if ((unsigned)sy >= (unsigned)height) continue;
                const int dst_row  = (sy * width) * channels;
                const int blur_row = (y - blur_y0) * blur_width;
                for (int x = blur_x0; x < blur_x1; ++x) {
                    const int dx = x + offset_x;
                    if ((unsigned)dx >= (unsigned)width) continue;
                    float alpha_f = blurred[(size_t)blur_row + (x - blur_x0)];
                    if (alpha_f < 0.f) alpha_f = 0.f;
                    if (alpha_f > 1.f) alpha_f = 1.f;
                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);

                    const int idx = dst_row + dx * channels;
                    const uint8_t sr = lut_r[sa], sg = lut_g[sa], sb = lut_b[sa];

                    dst[idx + 0] = premul
                        ? (uint8_t)std::min(255, (int)sr + (dst[idx + 0] * (255 - sa) + 127) / 255)
                        : lerp8(dst[idx + 0], sr, sa);
                    dst[idx + 1] = premul
                        ? (uint8_t)std::min(255, (int)sg + (dst[idx + 1] * (255 - sa) + 127) / 255)
                        : lerp8(dst[idx + 1], sg, sa);
                    dst[idx + 2] = premul
                        ? (uint8_t)std::min(255, (int)sb + (dst[idx + 2] * (255 - sa) + 127) / 255)
                        : lerp8(dst[idx + 2], sb, sa);

                    const uint8_t da = dst[idx + 3];
                    dst[idx + 3] = (uint8_t)(da + (((255 - da) * sa + 127) / 255));
                }
            }
        } else if (channels == 3) {
            for (int y = blur_y0; y < blur_y1; ++y) {
                const int sy = y + offset_y;
                if ((unsigned)sy >= (unsigned)height) continue;
                const int dst_row  = (sy * width) * channels;
                const int blur_row = (y - blur_y0) * blur_width;
                for (int x = blur_x0; x < blur_x1; ++x) {
                    const int dx = x + offset_x;
                    if ((unsigned)dx >= (unsigned)width) continue;
                    float alpha_f = blurred[(size_t)blur_row + (x - blur_x0)];
                    if (alpha_f < 0.f) alpha_f = 0.f;
                    if (alpha_f > 1.f) alpha_f = 1.f;
                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);

                    uint8_t sr = (premul && sa < 255) ? (uint8_t)((r * sa + 127) / 255) : (uint8_t)r;
                    uint8_t sg = (premul && sa < 255) ? (uint8_t)((g * sa + 127) / 255) : (uint8_t)g;
                    uint8_t sb = (premul && sa < 255) ? (uint8_t)((b * sa + 127) / 255) : (uint8_t)b;

                    const int idx = dst_row + dx * channels;
                    dst[idx + 0] = sr; dst[idx + 1] = sg; dst[idx + 2] = sb;
                }
            }
        }
    } else {
        if (channels == 4) {
            for (int y = sample_mask->y; y < sample_mask->y + sample_mask->height; ++y) {
                const int dst_row  = (y * width) * channels;
                const int local_y = y - sample_mask->y;

                for (int x = sample_mask->x; x < sample_mask->x + sample_mask->width; ++x) {
                    const int local_x = x - sample_mask->x;
                    const float m = sample_count(
                        sample_mask->samples[(size_t)local_y * sample_mask->width + local_x]
                    ) * (1.0f / 16.0f);
                    if (m <= 0.f) continue;

                    int sx = x - offset_x;
                    int sy = y - offset_y;
                    if (sx < 0) sx = 0; else if (sx >= width)  sx = width  - 1;
                    if (sy < 0) sy = 0; else if (sy >= height) sy = height - 1;

                    float bval = blurred_at(sx, sy);
                    float alpha_f = m * (1.0f - bval);
                    if (alpha_f <= 0.f) continue;
                    if (alpha_f > 1.f) alpha_f = 1.f;

                    const uint8_t sa = (uint8_t)((a * alpha_f) + 0.5f);
                    const int idx = dst_row + x * channels;

                    const uint8_t sr = lut_r[sa], sg = lut_g[sa], sb = lut_b[sa];

                    dst[idx + 0] = premul
                        ? (uint8_t)std::min(255, (int)sr + (dst[idx + 0] * (255 - sa) + 127) / 255)
                        : lerp8(dst[idx + 0], sr, sa);
                    dst[idx + 1] = premul
                        ? (uint8_t)std::min(255, (int)sg + (dst[idx + 1] * (255 - sa) + 127) / 255)
                        : lerp8(dst[idx + 1], sg, sa);
                    dst[idx + 2] = premul
                        ? (uint8_t)std::min(255, (int)sb + (dst[idx + 2] * (255 - sa) + 127) / 255)
                        : lerp8(dst[idx + 2], sb, sa);

                    const uint8_t da = dst[idx + 3];
                    dst[idx + 3] = (uint8_t)(da + (((255 - da) * sa + 127) / 255));
                }
            }
        } else if (channels == 3) {
            for (int y = sample_mask->y; y < sample_mask->y + sample_mask->height; ++y) {
                const int dst_row  = (y * width) * channels;
                const int local_y = y - sample_mask->y;

                for (int x = sample_mask->x; x < sample_mask->x + sample_mask->width; ++x) {
                    const int local_x = x - sample_mask->x;
                    const float m = sample_count(
                        sample_mask->samples[(size_t)local_y * sample_mask->width + local_x]
                    ) * (1.0f / 16.0f);
                    if (m <= 0.f) continue;

                    int sx = x - offset_x;
                    int sy = y - offset_y;
                    if (sx < 0) sx = 0; else if (sx >= width)  sx = width  - 1;
                    if (sy < 0) sy = 0; else if (sy >= height) sy = height - 1;

                    float bval = blurred_at(sx, sy);
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
