#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "buffer.h"

BufferInfo read_and_validate_buffer_info(lua_State *L, int index)
{
    BufferInfo buffer_info;

    luaL_checktype(L, index, LUA_TTABLE);
    lua_getfield(L, index, "buffer");
    lua_getfield(L, index, "width");
    lua_getfield(L, index, "height");
    lua_getfield(L, index, "channels");
    lua_getfield(L, index, "premultiply_alpha");
    lua_getfield(L, index, "path_handle");
    dmScript::LuaHBuffer *lua_buffer = dmScript::CheckBuffer(L, -6);
    buffer_info.buffer = lua_buffer->m_Buffer;

    if (!dmBuffer::IsBufferValid(buffer_info.buffer))
    {
        luaL_error(L, "Buffer is invalid");
    }

    dmBuffer::Result result = dmBuffer::GetBytes(buffer_info.buffer, (void **)&buffer_info.bytes, &buffer_info.src_size);
    if (result != dmBuffer::RESULT_OK)
    {
        luaL_error(L, "Buffer is invalid");
    }

    buffer_info.width = lua_tointeger(L, -5);
    if (buffer_info.width == 0)
    {
        luaL_error(L, "'width' of the buffer should be an integer and > 0");
    }

    buffer_info.height = lua_tointeger(L, -4);
    if (buffer_info.height == 0)
    {
        luaL_error(L, "'height' of the buffer should be an integer and > 0");
    }

    buffer_info.channels = lua_tointeger(L, -3);
    if (buffer_info.channels == 0)
    {
        luaL_error(L, "'channels' of should be an integer and 3 or 4");
    }

    if (lua_isboolean(L, -1) == 1)
    {
        buffer_info.premultiply_alpha = lua_toboolean(L, -2);
    }
    else
    {
        buffer_info.premultiply_alpha = false;
    }

    buffer_info.path_handle = lua_tointeger(L, -1);

    lua_pop(L, 6);

    return buffer_info;
}

int create_path() {
    g_path_points.emplace_back(); // default PathPointList (empty, is_closed=false)
    return static_cast<int>(g_path_points.size() - 1);
}

// Deletes the path at the given index (if valid)
void delete_path(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_path_points.size()) {
        return; // silently ignore invalid index
    }
    g_path_points.erase(g_path_points.begin() + index);
}

void clear_all_paths() {
    // Clear all path point lists
    g_path_points.clear();

    // Force capacity release
    std::vector<PathPointList>().swap(g_path_points);
}

// Returns a copy of the PathPointList at the given index
PathPointList* get_path_points(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_path_points.size()) {
        printf("The path points handle %d is invalid.", index);
        return nullptr;
    }
    return &g_path_points[index];
}