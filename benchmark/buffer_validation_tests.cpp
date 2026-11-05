// Exercise the real Lua/Defold boundary using a small SDK test double.
#include <dmsdk/sdk.h>
#include "buffer.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

struct Value {
    int type;
    double number;
    Value(int t = LUA_TNIL, double n = 0) : type(t), number(n) {}
};
struct lua_State {
    std::vector<Value> stack;
    std::map<std::string, Value> fields;
};
static Value& at(lua_State* L, int i) {
    return L->stack.at(i < 0 ? L->stack.size() + i : i - 1);
}
int luaL_error(lua_State*, const char* format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    throw std::runtime_error(message);
}
void luaL_checktype(lua_State* L, int i, int type) {
    if (at(L, i).type != type) luaL_error(L, "Invalid field type");
}
void lua_getfield(lua_State* L, int i, const char* name) {
    luaL_checktype(L, i, LUA_TTABLE);
    L->stack.push_back(L->fields[name]);
}
double lua_tonumber(lua_State* L, int i) { return at(L, i).number; }
int lua_tointeger(lua_State* L, int i) { return int(lua_tonumber(L, i)); }
int lua_isboolean(lua_State* L, int i) { return at(L, i).type == LUA_TBOOLEAN; }
int lua_toboolean(lua_State* L, int i) { return at(L, i).number != 0; }
int lua_isnumber(lua_State* L, int i) { return at(L, i).type == LUA_TNUMBER; }
void lua_pop(lua_State* L, int n) { L->stack.resize(L->stack.size() - n); }

static uint8_t pixels[32];
static bool valid_buffer = true;
static bool readable_buffer = true;
static uint32_t available_bytes = sizeof(pixels);
bool dmBuffer::IsBufferValid(HBuffer) { return valid_buffer; }
dmBuffer::Result dmBuffer::GetBytes(HBuffer, void** bytes, uint32_t* size) {
    *bytes = pixels;
    *size = available_bytes;
    return readable_buffer ? RESULT_OK : RESULT_INVALID_BUFFER;
}
dmScript::LuaHBuffer* dmScript::CheckBuffer(lua_State* L, int i) {
    luaL_checktype(L, i, LUA_TUSERDATA);
    static LuaHBuffer buffer = {1};
    return &buffer;
}

static lua_State descriptor() {
    lua_State L;
    L.stack.push_back(Value(LUA_TTABLE));
    L.fields = {{"buffer", Value(LUA_TUSERDATA)}, {"width", Value(LUA_TNUMBER, 2)},
        {"height", Value(LUA_TNUMBER, 3)}, {"channels", Value(LUA_TNUMBER, 4)},
        {"premultiply_alpha", Value(LUA_TBOOLEAN, 1)}, {"path_handle", Value(LUA_TNUMBER, 7)},
        {"origin_x", Value(LUA_TNUMBER, -10)}, {"origin_y", Value(LUA_TNUMBER, 20)}};
    return L;
}
static void rejected(lua_State L) {
    try { read_and_validate_buffer_info(&L, 1); }
    catch (const std::runtime_error&) { return; }
    assert(false && "Invalid descriptor was accepted");
}
int main() {
    lua_State L = descriptor();
    BufferInfo info = read_and_validate_buffer_info(&L, 1);
    assert(info.width == 2 && info.height == 3 && info.channels == 4);
    assert(info.bytes == pixels && info.src_size == sizeof(pixels));
    assert(info.premultiply_alpha && info.path_handle == 7);
    assert(info.origin_x == -10 && info.origin_y == 20 && L.stack.size() == 1);
    for (const char* field : {"width", "height", "channels"}) {
        for (int type : {LUA_TNIL, LUA_TSTRING, LUA_TBOOLEAN, LUA_TTABLE}) {
            L = descriptor(); L.fields[field] = Value(type, 2); rejected(L);
        }
        for (double n : {-1.0, 0.0, 1.5, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(), 1e100}) {
            L = descriptor(); L.fields[field] = Value(LUA_TNUMBER, n); rejected(L);
        }
    }
    L = descriptor(); L.fields["channels"] = Value(LUA_TNUMBER, 5); rejected(L);
    L = descriptor(); L.fields["width"] = Value(LUA_TNUMBER, 32768);
    L.fields["height"] = Value(LUA_TNUMBER, 32768); rejected(L);
    available_bytes = 23; rejected(descriptor());
    available_bytes = 24; L = descriptor(); read_and_validate_buffer_info(&L, 1);
    available_bytes = 18; L = descriptor(); L.fields["channels"] = Value(LUA_TNUMBER, 3);
    read_and_validate_buffer_info(&L, 1);
    valid_buffer = false; rejected(descriptor()); valid_buffer = true;
    readable_buffer = false; rejected(descriptor());
    std::puts("native buffer boundary tests passed");
}
