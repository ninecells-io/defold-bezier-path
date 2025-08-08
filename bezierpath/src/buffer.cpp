#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "buffer.h"

void read_and_validate_buffer_info(lua_State *L, int index)
{
    luaL_checktype(L, index, LUA_TTABLE);
    lua_getfield(L, index, "buffer");
    lua_getfield(L, index, "width");
    lua_getfield(L, index, "height");
    lua_getfield(L, index, "channels");
    lua_getfield(L, index, "premultiply_alpha");
    dmScript::LuaHBuffer *lua_buffer = dmScript::CheckBuffer(L, -5);
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

    buffer_info.width = lua_tointeger(L, -4);
    if (buffer_info.width == 0)
    {
        luaL_error(L, "'width' of the buffer should be an integer and > 0");
    }

    buffer_info.height = lua_tointeger(L, -3);
    if (buffer_info.height == 0)
    {
        luaL_error(L, "'height' of the buffer should be an integer and > 0");
    }

    buffer_info.channels = lua_tointeger(L, -2);
    if (buffer_info.channels == 0)
    {
        luaL_error(L, "'channels' of should be an integer and 3 or 4");
    }

    if (lua_isboolean(L, -1) == 1)
    {
        buffer_info.premultiply_alpha = lua_toboolean(L, -1);
    }
    else
    {
        buffer_info.premultiply_alpha = false;
    }

    lua_pop(L, 5);
}