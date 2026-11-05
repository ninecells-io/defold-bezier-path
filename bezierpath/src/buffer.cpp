#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "buffer.h"
#include "buffer_validation.h"

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

    luaL_checktype(L, -5, LUA_TNUMBER);
    luaL_checktype(L, -4, LUA_TNUMBER);
    luaL_checktype(L, -3, LUA_TNUMBER);
    const double width = lua_tonumber(L, -5);
    const double height = lua_tonumber(L, -4);
    const double channels = lua_tonumber(L, -3);
    const char* layout_error = validate_buffer_layout(width, height, channels, buffer_info.src_size);
    if (layout_error)
        luaL_error(L, "%s", layout_error);
    buffer_info.width = static_cast<int>(width);
    buffer_info.height = static_cast<int>(height);
    buffer_info.channels = static_cast<int>(channels);

    if (lua_isboolean(L, -2) == 1)
    {
        buffer_info.premultiply_alpha = lua_toboolean(L, -2);
    }
    else
    {
        buffer_info.premultiply_alpha = false;
    }

    buffer_info.path_handle = lua_tointeger(L, -1);

    lua_pop(L, 6);

    buffer_info.origin_x = 0.0f;
    buffer_info.origin_y = 0.0f;

    lua_getfield(L, index, "origin_x");
    if (lua_isnumber(L, -1)) {
        buffer_info.origin_x = (float)lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, index, "origin_y");
    if (lua_isnumber(L, -1)) {
        buffer_info.origin_y = (float)lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    return buffer_info;
}
