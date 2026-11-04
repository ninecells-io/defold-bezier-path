#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <dmsdk/sdk.h>
#include "buffer.h"
#include "fill.h"
#include "shadow.h"
#include "stroke.h"
#include "utils.h"

static PathPoint linear(float x, float y) {
    PathPoint point = {};
    point.x = x;
    point.y = y;
    point.type = POINT_TYPE_LINEAR;
    return point;
}

static void set_rect(uint32_t handle, float x0, float y0, float x1, float y1) {
    PathPointList* path = get_path_points(handle);
    assert(path);
    path->points.clear();
    path->points.push_back(linear(x0, y0));
    path->points.push_back(linear(x1, y0));
    path->points.push_back(linear(x1, y1));
    path->points.push_back(linear(x0, y1));
    path->is_closed = true;
    invalidate_path(path);
}

static BufferInfo make_buffer(
    std::vector<uint8_t>& pixels,
    int width, int height, int channels,
    uint32_t handle,
    float origin_x = 0.0f, float origin_y = 0.0f
) {
    pixels.assign((size_t)width * height * channels, 0);
    BufferInfo info = {};
    info.width = width;
    info.height = height;
    info.channels = channels;
    info.premultiply_alpha = false;
    info.bytes = pixels.data();
    info.src_size = (uint32_t)pixels.size();
    info.path_handle = handle;
    info.origin_x = origin_x;
    info.origin_y = origin_y;
    return info;
}

static void test_stable_handles_and_lifecycle() {
    clear_all_paths();
    const uint32_t first = create_path();
    const uint32_t removed = create_path();
    const uint32_t last = create_path();
    assert(first != removed && removed != last && first != last);

    set_rect(first, 1, 1, 4, 4);
    set_rect(last, 10, 10, 14, 14);
    delete_path(removed);
    assert(g_path_points.find(removed) == g_path_points.end());
    assert(get_path_points(first)->points[0].x == 1.0f);
    assert(get_path_points(last)->points[0].x == 10.0f);

    const uint32_t replacement = create_path();
    assert(replacement != removed);
    assert(get_path_points(first)->points[0].x == 1.0f);
    assert(get_path_points(last)->points[0].x == 10.0f);

    for (int i = 0; i < 10000; ++i) {
        const uint32_t handle = create_path();
        delete_path(handle);
    }
    assert(g_path_points.size() == 3);
    delete_path(first);
    delete_path(last);
    delete_path(replacement);
    assert(g_path_points.empty());
}

static void test_cache_invalidation() {
    const uint32_t handle = create_path();
    set_rect(handle, 4, 5, 20, 22);
    PathPointList* path = get_path_points(handle);
    const uint32_t revision = path->revision;

    const std::vector<std::pair<float, float> >* points = nullptr;
    assert(get_flattened_path(path, points));
    assert(get_segment_index(path, true));
    std::vector<uint8_t> pixels;
    BufferInfo buffer_info = make_buffer(pixels, 32, 32, 4, handle);
    assert(get_fill_sample_mask(buffer_info, path));
    assert(path->flattened_valid && path->segment_index_valid && path->fill_mask_valid);

    path->points[1].x = 24.0f;
    invalidate_path(path);
    assert(path->revision != revision);
    assert(!path->flattened_valid && !path->segment_index_valid && !path->fill_mask_valid);
    assert(get_flattened_path(path, points));
    assert(path->max_x == 24.0f);
    delete_path(handle);
}

static void render_crop_fixture(const BufferInfo& buffer_info) {
    fill_path_gradient(buffer_info, 20, 30, 40, 230, 220, 230, 240, 170, 37.0f);
    stroke_path(buffer_info, 2.5f, CENTER, 210, 80, 30, 190, CAP_ROUND, JOIN_ROUND);
}

static void test_cropped_render_matches_full_canvas() {
    const uint32_t handle = create_path();
    set_rect(handle, 13.25f, 17.5f, 45.75f, 50.25f);

    std::vector<uint8_t> full;
    BufferInfo full_info = make_buffer(full, 64, 64, 4, handle);
    render_crop_fixture(full_info);

    const int origin_x = 10;
    const int origin_y = 14;
    const int crop_width = 40;
    const int crop_height = 40;
    std::vector<uint8_t> crop;
    BufferInfo crop_info = make_buffer(
        crop, crop_width, crop_height, 4, handle,
        (float)origin_x, (float)origin_y
    );
    render_crop_fixture(crop_info);

    std::vector<uint8_t> composited(full.size(), 0);
    const int destination_y = 64 - (origin_y + crop_height);
    for (int y = 0; y < crop_height; ++y) {
        for (int x = 0; x < crop_width; ++x) {
            const size_t source = ((size_t)y * crop_width + x) * 4;
            const size_t destination = ((size_t)(destination_y + y) * 64 + origin_x + x) * 4;
            std::memcpy(&composited[destination], &crop[source], 4);
        }
    }
    assert(full == composited);
    delete_path(handle);
}

static void test_edge_cases_and_memory_bound() {
    const uint32_t handle = create_path();
    std::vector<uint8_t> pixels;
    BufferInfo buffer_info = make_buffer(pixels, 64, 64, 4, handle);

    fill_path(buffer_info, 255, 255, 255, 255);
    stroke_path(buffer_info, 2, CENTER, 255, 255, 255, 255, CAP_BUTT, JOIN_MITER);
    assert(std::find(pixels.begin(), pixels.end(), (uint8_t)1) == pixels.end());

    set_rect(handle, -100, -100, -50, -50);
    fill_path(buffer_info, 255, 255, 255, 255);
    for (size_t i = 0; i < pixels.size(); ++i) assert(pixels[i] == 0);

    PathPointList* path = get_path_points(handle);
    path->points.clear();
    path->points.push_back(linear(12, 12));
    path->is_closed = false;
    invalidate_path(path);
    fill_path(buffer_info, 255, 255, 255, 255);
    stroke_path(buffer_info, 2, CENTER, 255, 255, 255, 255, CAP_BUTT, JOIN_MITER);
    stroke_path_glow(buffer_info, 2, 0, CENTER, 255, 255, 255, 255);

    std::vector<uint8_t> rgb;
    BufferInfo rgb_info = make_buffer(rgb, 32, 32, 3, handle);
    set_rect(handle, 4, 4, 28, 28);
    fill_path(rgb_info, 20, 80, 160, 255);
    assert(std::find_if(rgb.begin(), rgb.end(), [](uint8_t value) { return value != 0; }) != rgb.end());

    std::fill(pixels.begin(), pixels.end(), 0);
    set_rect(handle, 8, 8, 56, 56);
    fill_shadow(buffer_info, 0, 0, 0, 0, 0, 0, 128, false);
    assert(std::find_if(pixels.begin(), pixels.end(), [](uint8_t value) { return value != 0; }) != pixels.end());

    fill_color(buffer_info, 100, 110, 120, 255);
    mask_path(buffer_info, false);
    assert(pixels[((size_t)32 * 64 + 32) * 4 + 3] == 2);
    assert(pixels[3] == 255);
    fill_color(buffer_info, 100, 110, 120, 255);
    mask_path(buffer_info, true);
    assert(pixels[((size_t)32 * 64 + 32) * 4 + 3] == 255);
    assert(pixels[3] == 2);

    std::vector<uint8_t> large;
    BufferInfo large_info = make_buffer(large, 512, 512, 4, handle);
    set_rect(handle, 8, 8, 504, 504);
    fill_shadow(large_info, 0, 0, 10, 0, 0, 0, 128, false);
    const RasterScratch& scratch = get_raster_scratch();
    const size_t scratch_bytes =
        scratch.mask.samples.capacity() * sizeof(uint16_t) +
        scratch.values.capacity() * sizeof(float) +
        scratch.temp.capacity() * sizeof(float);
    const size_t cached_mask_bytes = get_path_points(handle)->fill_mask.samples.capacity() * sizeof(uint16_t);
    assert(scratch_bytes + cached_mask_bytes <= 4u * 1024u * 1024u);
    delete_path(handle);
}

static void test_round_caps_follow_stroke_direction() {
    const uint32_t handle = create_path();
    // Horizontal, vertical, diagonal, and reversed strokes must all form capsules.
    const float directions[][2] = {{1,0},{0,1},{1,1},{1,-1},{-1,0},{0,-1},{-1,-1},{-1,1}};
    for (const auto& direction : directions) {
        const float length = std::hypot(direction[0], direction[1]);
        const float dx = direction[0] / length, dy = direction[1] / length;
        const float ax = 32-dx*16, ay = 32-dy*16;
        const float bx = 32+dx*16, by = 32+dy*16;
        PathPointList* path = get_path_points(handle);
        path->points.clear();
        path->points.push_back(linear(ax,ay));
        path->points.push_back(linear(bx,by));
        path->is_closed = false;
        invalidate_path(path);
        std::vector<uint8_t> pixels;
        BufferInfo info = make_buffer(pixels,64,64,4,handle);
        stroke_path(info,12,CENTER,255,255,255,255,CAP_ROUND,JOIN_ROUND);
        for (int y=0; y<64; ++y) {
            for (int x=0; x<64; ++x) {
                const float px = x+0.5f, py = 64-(y+0.5f);
                const float along = std::max(0.0f,std::min(32.0f,(px-ax)*dx+(py-ay)*dy));
                const float distance = std::hypot(px-(ax+along*dx),py-(ay+along*dy));
                const uint8_t alpha = pixels[((size_t)y*64+x)*4+3];
                // Leave the antialiased boundary unconstrained; test solid interiors
                // and exteriors against the analytic capsule, including both caps.
                if (distance < 5.0f) assert(alpha == 255);
                if (distance > 7.0f) assert(alpha == 0);
            }
        }
    }
    delete_path(handle);
}

int main() {
    test_stable_handles_and_lifecycle();
    test_cache_invalidation();
    test_cropped_render_matches_full_canvas();
    test_edge_cases_and_memory_bound();
    test_round_caps_follow_stroke_direction();
    clear_all_paths();
    std::puts("native raster tests passed");
    return 0;
}
