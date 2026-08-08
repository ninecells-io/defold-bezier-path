#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <dmsdk/sdk.h>
#ifdef LEGACY_RASTER
struct BufferInfo {
    dmBuffer::HBuffer buffer;
    int width;
    int height;
    int channels;
    bool premultiply_alpha;
    uint8_t* bytes;
    uint32_t src_size;
    uint32_t path_handle;
};
enum PointType { POINT_TYPE_LINEAR, POINT_TYPE_QUAD_CURVE, POINT_TYPE_ARC };
struct PathPoint {
    float x, y;
    PointType type;
    float cx, cy;
    float arc_cx, arc_cy, arc_radius, arc_start, arc_end;
    uint8_t arc_clockwise;
};
struct PathPointList {
    std::vector<PathPoint> points;
    bool is_closed = false;
};
void fill_path(const BufferInfo&, int, int, int, int);
void fill_path_gradient(const BufferInfo&, int, int, int, int, int, int, int, int, float);
void fill_path_gradient_shape(const BufferInfo&, int, int, int, int, int, int, int, int);
void fill_shadow(const BufferInfo&, int, int, int, int, int, int, int, bool);
void stroke_path(const BufferInfo&, float, int, int, int, int, int, int, int);
void stroke_path_glow(const BufferInfo&, float, float, int, int, int, int, int);
#ifndef M_TAU
#define M_TAU 6.2831853071795864769f
#endif
#else
#include "buffer.h"
#include "fill.h"
#include "shadow.h"
#include "stroke.h"
#include "utils.h"
#endif

static PathPointList g_Path;

#ifdef LEGACY_RASTER
PathPointList* get_path_points(int handle) {
#else
PathPointList* get_path_points(uint32_t handle) {
#endif
    return handle == 1 ? &g_Path : nullptr;
}

void invalidate_path(PathPointList* path) {
#ifdef LEGACY_RASTER
    (void)path;
#else
    if (!path) return;
    ++path->revision;
    path->flattened_valid = false;
    path->flattened_points.clear();
    path->flattened_corner_flags.clear();
    path->segment_index_valid = false;
    path->segment_index.clear();
    path->fill_mask_valid = false;
    path->fill_mask.clear();
#endif
}

static PathPoint linear(float x, float y) {
    PathPoint point = {};
    point.x = x;
    point.y = y;
    point.type = POINT_TYPE_LINEAR;
    return point;
}

static PathPoint arc(float cx, float cy, float radius, float start, float end) {
    PathPoint point = {};
    point.x = cx + radius * cosf(end);
    point.y = cy + radius * sinf(end);
    point.type = POINT_TYPE_ARC;
    point.arc_cx = cx;
    point.arc_cy = cy;
    point.arc_radius = radius;
    point.arc_start = start;
    point.arc_end = end;
    return point;
}

static void set_points(const std::vector<PathPoint>& points, bool closed) {
    g_Path.points = points;
    g_Path.is_closed = closed;
    invalidate_path(&g_Path);
}

static void set_rect(float x0, float y0, float x1, float y1) {
    std::vector<PathPoint> points;
    points.push_back(linear(x0, y0));
    points.push_back(linear(x1, y0));
    points.push_back(linear(x1, y1));
    points.push_back(linear(x0, y1));
    set_points(points, true);
}

static void set_circle(float cx, float cy, float radius) {
    std::vector<PathPoint> points;
    points.push_back(linear(cx + radius, cy));
    points.push_back(arc(cx, cy, radius, 0.0f, M_TAU));
    set_points(points, true);
}

static void set_line(float x0, float y0, float x1, float y1) {
    std::vector<PathPoint> points;
    points.push_back(linear(x0, y0));
    points.push_back(linear(x1, y1));
    set_points(points, false);
}

template <typename Function>
static double benchmark(int repetitions, Function function) {
    function();
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int i = 0; i < repetitions; ++i) function();
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / repetitions;
}

struct PrimitiveTimings {
    double fill_rect;
    double fill_circle;
    double gradient_rect;
    double gradient_circle;
    double shape_rect;
    double shape_circle;
    double shadow_rect;
    double shadow_circle;
    double stroke_rect;
    double stroke_line;
    double glow_circle;
};

static PrimitiveTimings run_primitives(int size) {
    std::vector<uint8_t> pixels((size_t)size * size * 4);
    BufferInfo buffer_info = {};
    buffer_info.width = size;
    buffer_info.height = size;
    buffer_info.channels = 4;
    buffer_info.premultiply_alpha = true;
    buffer_info.bytes = pixels.data();
    buffer_info.src_size = (uint32_t)pixels.size();
    buffer_info.path_handle = 1;
    const int repetitions = size <= 64 ? 20 : (size <= 256 ? 5 : 2);
    const int blur_radius = std::max(1, size / 25);
    const float stroke_width = (float)std::max(1, size / 128);
    const auto clear = [&]() { memset(pixels.data(), 0, pixels.size()); };

    set_rect(size * 0.1f, size * 0.1f, size * 0.9f, size * 0.9f);
    const double fill_rect = benchmark(repetitions, [&]() { clear(); fill_path(buffer_info, 230, 220, 210, 255); });
    const double gradient_rect = benchmark(repetitions, [&]() { clear(); fill_path_gradient(buffer_info, 20, 30, 40, 255, 220, 230, 240, 255, 45); });
    const double shape_rect = benchmark(repetitions, [&]() { clear(); fill_path_gradient_shape(buffer_info, 20, 30, 40, 255, 220, 230, 240, 255); });
    const double shadow_rect = benchmark(repetitions, [&]() { clear(); fill_shadow(buffer_info, 0, 0, blur_radius, 0, 0, 0, 128, false); });
    const double stroke_rect = benchmark(repetitions, [&]() { clear(); stroke_path(buffer_info, stroke_width, 0, 255, 255, 255, 255, 0, 0); });

    set_circle(size * 0.5f, size * 0.5f, size * 0.4f);
    const double fill_circle = benchmark(repetitions, [&]() { clear(); fill_path(buffer_info, 230, 220, 210, 255); });
    const double gradient_circle = benchmark(repetitions, [&]() { clear(); fill_path_gradient(buffer_info, 20, 30, 40, 255, 220, 230, 240, 255, 45); });
    const double shape_circle = benchmark(repetitions, [&]() { clear(); fill_path_gradient_shape(buffer_info, 20, 30, 40, 255, 220, 230, 240, 255); });
    const double shadow_circle = benchmark(repetitions, [&]() { clear(); fill_shadow(buffer_info, 0, 0, blur_radius, 0, 0, 0, 128, true); });
    const double glow_circle = benchmark(repetitions, [&]() { clear(); stroke_path_glow(buffer_info, stroke_width, (float)blur_radius, 0, 0, 0, 0, 128); });

    set_line(size * 0.05f, size * 0.5f, size * 0.95f, size * 0.5f);
    const double stroke_line = benchmark(repetitions, [&]() { clear(); stroke_path(buffer_info, stroke_width, 0, 255, 255, 255, 255, 0, 0); });

    std::printf("size=%d compact-full-mask=%.2f MiB\n", size, (double)size * size * 2 / 1048576.0);
    std::printf("  fill rect %.3f ms | circle %.3f ms\n", fill_rect, fill_circle);
    std::printf("  linear gradient rect %.3f ms | circle %.3f ms\n", gradient_rect, gradient_circle);
    std::printf("  shape gradient rect %.3f ms | circle %.3f ms\n", shape_rect, shape_circle);
    std::printf("  shadow rect %.3f ms | circle %.3f ms\n", shadow_rect, shadow_circle);
    std::printf("  stroke rect %.3f ms | line %.3f ms | glow circle %.3f ms\n", stroke_rect, stroke_line, glow_circle);
    PrimitiveTimings timings = {
        fill_rect, fill_circle,
        gradient_rect, gradient_circle,
        shape_rect, shape_circle,
        shadow_rect, shadow_circle,
        stroke_rect, stroke_line, glow_circle
    };
    return timings;
}

static double run_classic_board() {
    const int size = 512;
    std::vector<uint8_t> pixels((size_t)size * size * 4);
    BufferInfo buffer_info = {};
    buffer_info.width = size;
    buffer_info.height = size;
    buffer_info.channels = 4;
    buffer_info.premultiply_alpha = true;
    buffer_info.bytes = pixels.data();
    buffer_info.src_size = (uint32_t)pixels.size();
    buffer_info.path_handle = 1;
    const auto clear = [&]() { memset(pixels.data(), 0, pixels.size()); };

    const double total = benchmark(3, [&]() {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const float x0 = 10.0f + column * 164.0f;
                const float y0 = 10.0f + row * 164.0f;
                set_rect(x0, y0, x0 + 164.0f, y0 + 164.0f);
                clear();
                fill_path_gradient(buffer_info, 220, 220, 220, 255, 240, 240, 240, 255, 135);
                clear();
                fill_path(buffer_info, 220, 220, 220, 30);
                clear();
                stroke_path(buffer_info, 2.0f, 0, 255, 255, 255, 84, 0, 0);
            }
        }
        clear();
        for (int i = 1; i < 9; ++i) {
            const float position = 10.0f + i * (492.0f / 9.0f);
            set_line(position, 10, position, 502);
            stroke_path(buffer_info, 1.4f, 0, 0, 0, 0, 50, 0, 0);
            set_line(10, position, 502, position);
            stroke_path(buffer_info, 1.4f, 0, 0, 0, 0, 50, 0, 0);
        }
        set_rect(10, 10, 502, 502);
        clear();
        fill_shadow(buffer_info, 0, 0, 10, 0, 0, 0, 102, false);
        clear();
        stroke_path(buffer_info, 1, 0, 255, 255, 255, 84, 1, 1);
    });
    std::printf("Classic representative total: %.3f ms\n", total);
    return total;
}

int main() {
    const PrimitiveTimings small = run_primitives(64);
    run_primitives(256);
    const PrimitiveTimings large = run_primitives(512);
    const double classic = run_classic_board();

#ifdef LEGACY_RASTER
    return 0;
#else
    // Gates use the recorded legacy ARM64 -O2 timings from this harness.
    bool passed = true;
    const auto require_at_most = [&](const char* name, double value, double maximum) {
        if (value > maximum) {
            std::fprintf(stderr, "PERF GATE FAILED: %s %.3f ms > %.3f ms\n", name, value, maximum);
            passed = false;
        }
    };

    require_at_most("64 fill rectangle", small.fill_rect, 0.238 * 1.10);
    require_at_most("64 fill circle", small.fill_circle, 1.081 * 1.10);
    require_at_most("64 gradient rectangle", small.gradient_rect, 0.372 * 1.10);
    require_at_most("64 gradient circle", small.gradient_circle, 1.007 * 1.10);
    require_at_most("64 shape rectangle", small.shape_rect, 1.406 * 1.10);
    require_at_most("64 shape circle", small.shape_circle, 6.050 * 1.10);
    require_at_most("64 shadow rectangle", small.shadow_rect, 1.325 * 1.10);
    require_at_most("64 shadow circle", small.shadow_circle, 0.890 * 1.10);
    require_at_most("64 stroke rectangle", small.stroke_rect, 0.089 * 1.10);
    require_at_most("64 stroke line", small.stroke_line, 0.027 * 1.10);
    require_at_most("64 glow circle", small.glow_circle, 5.749 * 1.10);

    require_at_most("512 fill circle", large.fill_circle, 131.486 / 5.0);
    require_at_most("512 gradient circle", large.gradient_circle, 137.116 / 5.0);
    require_at_most("512 shape circle", large.shape_circle, 978.956 / 5.0);
    require_at_most("512 shadow circle", large.shadow_circle, 136.473 / 5.0);
    require_at_most("512 glow circle", large.glow_circle, 934.129 / 5.0);
    require_at_most("Classic representative", classic, 31.0);
    return passed ? 0 : 1;
#endif
}
