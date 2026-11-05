#pragma once

#include <stdint.h>

struct lua_State;

namespace dmBuffer {
    typedef uint32_t HBuffer;
}

#ifdef BEZIERPATH_BUFFER_TEST
#include <stddef.h>
enum { LUA_TNIL, LUA_TBOOLEAN, LUA_TNUMBER, LUA_TSTRING, LUA_TTABLE, LUA_TUSERDATA };
void luaL_checktype(lua_State*, int, int);
void lua_getfield(lua_State*, int, const char*);
double lua_tonumber(lua_State*, int);
int lua_tointeger(lua_State*, int);
int lua_isboolean(lua_State*, int);
int lua_toboolean(lua_State*, int);
int lua_isnumber(lua_State*, int);
void lua_pop(lua_State*, int);
int luaL_error(lua_State*, const char*, ...);
namespace dmBuffer {
    enum Result { RESULT_OK, RESULT_INVALID_BUFFER };
    bool IsBufferValid(HBuffer);
    Result GetBytes(HBuffer, void**, uint32_t*);
}
namespace dmScript {
    struct LuaHBuffer { dmBuffer::HBuffer m_Buffer; };
    LuaHBuffer* CheckBuffer(lua_State*, int);
}
#endif
