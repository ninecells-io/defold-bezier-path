#include <dmsdk/sdk.h>
#include <cstdio>
#include "buffer.h"

std::unordered_map<uint32_t, PathPointList> g_path_points;
static uint32_t g_next_path_handle = 1;

uint32_t create_path() {
    uint32_t handle;
    do {
        handle = g_next_path_handle++;
        if (handle == 0) handle = g_next_path_handle++;
    } while (g_path_points.find(handle) != g_path_points.end());
    g_path_points.emplace(handle, PathPointList());
    return handle;
}

void delete_path(uint32_t handle) {
    g_path_points.erase(handle);
}

void clear_all_paths() {
    g_path_points.clear();
    g_path_points.rehash(0);
    g_next_path_handle = 1;
}

PathPointList* get_path_points(uint32_t handle) {
    std::unordered_map<uint32_t, PathPointList>::iterator found = g_path_points.find(handle);
    if (found == g_path_points.end()) {
        std::printf("The path points handle %u is invalid.", handle);
        return nullptr;
    }
    return &found->second;
}

void invalidate_path(PathPointList* path) {
    if (!path) return;
    ++path->revision;
    if (path->revision == 0) path->revision = 1;
    path->flattened_valid = false;
    path->flattened_points.clear();
    path->flattened_corner_flags.clear();
    path->segment_index_valid = false;
    path->segment_index.clear();
    path->fill_mask_valid = false;
    path->fill_mask.clear();
}
