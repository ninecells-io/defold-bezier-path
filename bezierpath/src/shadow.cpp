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

void gaussian_blur(std::vector<float>& image, int width, int height, int radius)
{
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

void fill_shadow(int offset_x, int offset_y, int blur_radius, int r, int g, int b, int a)
{
    const int width = buffer_info.width;
    const int height = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul = buffer_info.premultiply_alpha;

    if (g_path_points.empty())
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(flat_path);

    std::vector<float> mask(width * height, 0.0f);

    // Fill alpha mask (Y-flipped)
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float fx = x + 0.5f;
            float fy = y + 0.5f;

            if (point_in_polygon(fx, fy, flat_path))
            {
                mask[y * width + x] = 1.0f;
            }
        }
    }

    gaussian_blur(mask, width, height, blur_radius);

    uint8_t* dst = buffer_info.bytes;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int sx = x + offset_x;
            int sy = y + offset_y;

            if (sx < 0 || sy < 0 || sx >= width || sy >= height)
                continue;

            int flipped_sy = height - sy - 1;

            float alpha = clamp(mask[y * width + x], 0.0f, 1.0f);
            uint8_t sa = static_cast<uint8_t>(a * alpha);

            uint8_t sr = static_cast<uint8_t>(r);
            uint8_t sg = static_cast<uint8_t>(g);
            uint8_t sb = static_cast<uint8_t>(b);

            if (premul && sa < 255)
            {
                float scale = sa / 255.0f;
                sr = static_cast<uint8_t>(sr * scale);
                sg = static_cast<uint8_t>(sg * scale);
                sb = static_cast<uint8_t>(sb * scale);
            }

            int idx = (flipped_sy * width + sx) * channels;

            if (channels == 4)
            {
                float src_a = sa / 255.0f;
                float inv_a = 1.0f - src_a;

                uint8_t dst_r = dst[idx + 0];
                uint8_t dst_g = dst[idx + 1];
                uint8_t dst_b = dst[idx + 2];
                uint8_t dst_a = dst[idx + 3];

                dst[idx + 0] = static_cast<uint8_t>(sr * src_a + dst_r * inv_a);
                dst[idx + 1] = static_cast<uint8_t>(sg * src_a + dst_g * inv_a);
                dst[idx + 2] = static_cast<uint8_t>(sb * src_a + dst_b * inv_a);
                dst[idx + 3] = static_cast<uint8_t>(sa + dst_a * inv_a);
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
