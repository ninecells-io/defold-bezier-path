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

void gaussian_blur(
    std::vector<float>& image, int width, int height, int radius
) {
    if (radius <= 0)
        return;

    int kernel_size = 2 * radius + 1;
    std::vector<float> kernel(kernel_size);
    float sigma = radius / 2.0f;
    float two_sigma2 = 2.0f * sigma * sigma;

    float sum = 0.0f;
    for (int i = 0; i < kernel_size; ++i)
    {
        int x = i - radius;
        kernel[i] = expf(-x * x / two_sigma2);
        sum += kernel[i];
    }
    for (float& k : kernel) k /= sum;

    std::vector<float> temp(width * height, 0.0f);

    // Horizontal pass
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float accum = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                int sx = clamp(x + k, 0, width - 1);
                accum += image[y * width + sx] * kernel[k + radius];
            }
            temp[y * width + x] = accum;
        }
    }

    // Vertical pass
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float accum = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                int sy = clamp(y + k, 0, height - 1);
                accum += temp[sy * width + x] * kernel[k + radius];
            }
            image[y * width + x] = accum;
        }
    }
}

void fill_shadow(
    const BufferInfo& buffer_info,
    int offset_x, int offset_y,
    int blur_radius,
    int r, int g, int b, int a
) {
    const int supersample = 4; // same super sampling factor as other fills
    const int width       = buffer_info.width;
    const int height      = buffer_info.height;
    const int channels    = buffer_info.channels;
    const bool premul     = buffer_info.premultiply_alpha;

    PathPointList* path_points = get_path_points(buffer_info.path_handle);
    if (!path_points || path_points->points.empty() || width <= 0 || height <= 0)
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(path_points->points, flat_path);
    if (flat_path.empty())
        return;

    // 1) Supersampled coverage via rasterizer
    const int s_width  = width  * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage((size_t)s_width * (size_t)s_height, 0.0f);
    rasterize_polygon_to_coverage(buffer_info, flat_path, coverage, supersample);

    // 2) Downsample coverage -> pixel mask in [0..1]
    std::vector<float> mask((size_t)width * (size_t)height, 0.0f);
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
            mask[(size_t)y * (size_t)width + (size_t)x] = sum * inv_samples;
        }
    }

    // 3) Blur the mask
    gaussian_blur(mask, width, height, blur_radius);

    // 4) Composite the colored shadow using the (offset) blurred mask
    uint8_t* dst = buffer_info.bytes;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sx = x + offset_x;
            const int sy = y + offset_y;

            if (sx < 0 || sy < 0 || sx >= width || sy >= height)
                continue;

            // Preserve original bottom-left (y-up) offset semantics
            const int flipped_sy = height - sy - 1;

            const float alpha_f = clamp(mask[(size_t)y * (size_t)width + (size_t)x], 0.0f, 1.0f);
            const uint8_t sa = (uint8_t)(a * alpha_f);

            uint8_t sr = (uint8_t)r;
            uint8_t sg = (uint8_t)g;
            uint8_t sb = (uint8_t)b;

            if (premul && sa < 255) {
                const float scale = sa / 255.0f;
                sr = (uint8_t)(sr * scale);
                sg = (uint8_t)(sg * scale);
                sb = (uint8_t)(sb * scale);
            }

            const int idx = (flipped_sy * width + sx) * channels;

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