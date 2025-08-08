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

void fill_color(int r, int g, int b, int a)
{
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

void fill_path(int r, int g, int b, int a)
{
    const int supersample = 4;
    const int width = buffer_info.width;
    const int height = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul = buffer_info.premultiply_alpha;

    if (g_path_points.empty())
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(flat_path);

    const int s_width = width * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage(s_width * s_height, 0.0f);

    // Fill coverage using point-in-polygon
    for (int y = 0; y < s_height; ++y)
    {
        for (int x = 0; x < s_width; ++x)
        {
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;

            if (point_in_polygon(fx, fy, flat_path))
            {
                coverage[y * s_width + x] = 1.0f;
            }
        }
    }

    // Downsample and write to buffer
    uint8_t* dst = buffer_info.bytes;
    float inv_samples = 1.0f / (supersample * supersample);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
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
            if (alpha <= 0.0f)
                continue; // Don’t overwrite existing pixels

            int idx = (y * width + x) * channels;
            uint8_t final_a = static_cast<uint8_t>(a * alpha);
            uint8_t final_r = static_cast<uint8_t>(r);
            uint8_t final_g = static_cast<uint8_t>(g);
            uint8_t final_b = static_cast<uint8_t>(b);

            if (premul && final_a < 255)
            {
                float a_scale = final_a / 255.0f;
                final_r = static_cast<uint8_t>(final_r * a_scale);
                final_g = static_cast<uint8_t>(final_g * a_scale);
                final_b = static_cast<uint8_t>(final_b * a_scale);
            }

            if (channels == 4)
            {
                float src_a = final_a / 255.0f;
                float inv_a = 1.0f - src_a;

                uint8_t dst_r = dst[idx + 0];
                uint8_t dst_g = dst[idx + 1];
                uint8_t dst_b = dst[idx + 2];
                uint8_t dst_a = dst[idx + 3];

                uint8_t out_r = static_cast<uint8_t>(final_r * src_a + dst_r * inv_a);
                uint8_t out_g = static_cast<uint8_t>(final_g * src_a + dst_g * inv_a);
                uint8_t out_b = static_cast<uint8_t>(final_b * src_a + dst_b * inv_a);
                uint8_t out_a = static_cast<uint8_t>(final_a + dst_a * inv_a);

                dst[idx + 0] = out_r;
                dst[idx + 1] = out_g;
                dst[idx + 2] = out_b;
                dst[idx + 3] = out_a;
            }
            else if (channels == 3)
            {
                dst[idx + 0] = final_r;
                dst[idx + 1] = final_g;
                dst[idx + 2] = final_b;
            }
        }
    }
}

void fill_path_gradient(
    int r1, int g1, int b1, int a1,
    int r2, int g2, int b2, int a2,
    float angle_deg
) {
    const int supersample = 4;
    const int width = buffer_info.width;
    const int height = buffer_info.height;
    const int channels = buffer_info.channels;
    const bool premul = buffer_info.premultiply_alpha;

    if (g_path_points.empty())
        return;

    std::vector<std::pair<float, float>> flat_path;
    flatten_path(flat_path);

    const int s_width = width * supersample;
    const int s_height = height * supersample;
    std::vector<float> coverage(s_width * s_height, 0.0f);

    // Fill coverage using point-in-polygon
    for (int y = 0; y < s_height; ++y)
    {
        for (int x = 0; x < s_width; ++x)
        {
            float fx = (float)x / supersample + 0.5f / supersample;
            float fy = (float)(s_height - y - 1) / supersample + 0.5f / supersample;

            if (point_in_polygon(fx, fy, flat_path))
            {
                coverage[y * s_width + x] = 1.0f;
            }
        }
    }

    // Gradient direction vector
    float angle_rad = angle_deg * M_PI / 180.0f;
    float gx = cosf(angle_rad);
    float gy = sinf(angle_rad);

    // Project corners to compute t-range
    float min_t = INFINITY, max_t = -INFINITY;
    for (int y = 0; y < s_height; y += supersample)
    {
        for (int x = 0; x < s_width; x += supersample)
        {
            float fx = (float)x / supersample;
            float fy = (float)(s_height - y - 1) / supersample;

            float t = fx * gx + fy * gy;
            min_t = std::min(min_t, t);
            max_t = std::max(max_t, t);
        }
    }
    float range_t = std::max(1e-6f, max_t - min_t);

    // Downsample and blend
    uint8_t* dst = buffer_info.bytes;
    float inv_samples = 1.0f / (supersample * supersample);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0f;
            float t_accum = 0.0f;

            for (int sy = 0; sy < supersample; ++sy)
            {
                for (int sx = 0; sx < supersample; ++sx)
                {
                    int cx = x * supersample + sx;
                    int cy = y * supersample + sy;

                    float coverage_val = coverage[cx + cy * s_width];
                    if (coverage_val > 0.0f)
                    {
                        float fx = (float)cx / supersample + 0.5f / supersample;
                        float fy = (float)(s_height - cy - 1) / supersample + 0.5f / supersample;

                        float t = (fx * gx + fy * gy - min_t) / range_t;
                        t = clamp(t, 0.0f, 1.0f);

                        t_accum += t * coverage_val;
                        sum += coverage_val;
                    }
                }
            }

            float alpha = std::min(sum * inv_samples, 1.0f);
            if (alpha <= 0.0f)
                continue;

            float t = t_accum / (sum + 1e-6f);

            uint8_t sr = static_cast<uint8_t>((1 - t) * r1 + t * r2);
            uint8_t sg = static_cast<uint8_t>((1 - t) * g1 + t * g2);
            uint8_t sb = static_cast<uint8_t>((1 - t) * b1 + t * b2);
            uint8_t sa = static_cast<uint8_t>((1 - t) * a1 + t * a2);
            sa = static_cast<uint8_t>(sa * alpha);

            if (premul && sa < 255)
            {
                float a_scale = sa / 255.0f;
                sr = static_cast<uint8_t>(sr * a_scale);
                sg = static_cast<uint8_t>(sg * a_scale);
                sb = static_cast<uint8_t>(sb * a_scale);
            }

            int idx = (y * width + x) * channels;

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