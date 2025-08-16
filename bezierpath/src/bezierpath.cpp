// Extension lib defines
#define LIB_NAME "BezierPath"
#define MODULE_NAME "bezierpath"
#ifndef DLIB_LOG_DOMAIN
#define DLIB_LOG_DOMAIN LIB_NAME
#endif

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
#include "shadow.h"
#include "stroke.h"

// LUA FUNCTIONS

static int fill_color_lua(lua_State *L) {
    int top = lua_gettop(L);
    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);
    int32_t r = luaL_checknumber(L, 2);
    int32_t g = luaL_checknumber(L, 3);
    int32_t b = luaL_checknumber(L, 4);
    int32_t a = 0;
    if (lua_isnumber(L, 5) == 1)
    {
        a = luaL_checknumber(L, 5);
    }
    if (a == 0 && buffer_info.channels == 4)
    {
        assert(top == lua_gettop(L));
        return 0;
    }

    fill_color(buffer_info, r, g, b, a);

    assert(top == lua_gettop(L));
    return 0;
}

static int reset_path_lua(lua_State* L) {
    int top = lua_gettop(L);
    int32_t path_handle = luaL_checknumber(L, 1);

    PathPointList* path_points = get_path_points(path_handle);
    if (path_points) {
        path_points->points.clear();
        path_points->is_closed = false;
    }

    assert(top == lua_gettop(L));
    return 0;
}

static int add_point_lua(lua_State* L) {
    int top = lua_gettop(L);

    int32_t path_handle = luaL_checknumber(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);

    PathPoint p;
    p.type = POINT_TYPE_LINEAR;
    p.x = x;
    p.y = y;
    p.cx = 0.0f;
    p.cy = 0.0f;

    PathPointList* path_points = get_path_points(path_handle);
    if (path_points) {
        path_points->points.push_back(p);
    }

    assert(top == lua_gettop(L));
    return 0;
}

static int add_quad_curve_point_lua(lua_State* L) {
    int top = lua_gettop(L);

    int32_t path_handle = luaL_checknumber(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    float cx = luaL_checknumber(L, 4);
    float cy = luaL_checknumber(L, 5);

    PathPoint p;
    p.type = POINT_TYPE_QUAD_CURVE;
    p.x = x;
    p.y = y;
    p.cx = cx;
    p.cy = cy;

    PathPointList* path_points = get_path_points(path_handle);
    if (path_points) {
        path_points->points.push_back(p);
    }

    assert(top == lua_gettop(L));
    return 0;
}

static int set_path_closed_lua(lua_State* L) {
    int top = lua_gettop(L);
    int32_t path_handle = luaL_checknumber(L, 1);

    PathPointList* path_points = get_path_points(path_handle);
    if (path_points) {
        path_points->is_closed = lua_toboolean(L, 2);
    }

    assert(top == lua_gettop(L));
    return 0;
}

static int init_buffer_lua(lua_State* L) {
    int top = lua_gettop(L);
    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);

    if (buffer_info.width <= 0 || buffer_info.height <= 0 || buffer_info.channels <= 0) {
        return 0; // invalid dimensions
    }

    // Initialize to zero
    std::memset(buffer_info.bytes, 0, buffer_info.src_size);

    assert(top == lua_gettop(L));
    return 0;
}

static int fill_path_lua(lua_State* L) {
    int top = lua_gettop(L);

    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);
    int r = luaL_checknumber(L, 2);
    int g = luaL_checknumber(L, 3);
    int b = luaL_checknumber(L, 4);
    int a = luaL_optnumber(L, 5, 255);

    fill_path(buffer_info, r, g, b, a);

    assert(top == lua_gettop(L));
    return 0;
}

static int mask_path_lua(lua_State* L) {
    int top = lua_gettop(L);

    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);
    bool inverted = lua_toboolean(L, 2);

    mask_path(buffer_info, inverted);

    assert(top == lua_gettop(L));
    return 0;
}

static int fill_path_gradient_lua(lua_State* L) {
    int top = lua_gettop(L);

    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);

    int r1 = luaL_checknumber(L, 2);
    int g1 = luaL_checknumber(L, 3);
    int b1 = luaL_checknumber(L, 4);
    int a1 = luaL_checknumber(L, 5);

    int r2 = luaL_checknumber(L, 6);
    int g2 = luaL_checknumber(L, 7);
    int b2 = luaL_checknumber(L, 8);
    int a2 = luaL_checknumber(L, 9);

    float angle_deg = luaL_checknumber(L, 10);

    fill_path_gradient(buffer_info, r1, g1, b1, a1, r2, g2, b2, a2, angle_deg);

    assert(top == lua_gettop(L));
    return 0;
}

static int fill_shadow_lua(lua_State* L) {
    int top = lua_gettop(L);

    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);

    int offset_x = luaL_checknumber(L, 2);
    int offset_y = luaL_checknumber(L, 3);
    int radius   = luaL_checknumber(L, 4);
    bool inner   = lua_toboolean(L, 5);

    int r = luaL_checknumber(L, 6);
    int g = luaL_checknumber(L, 7);
    int b = luaL_checknumber(L, 8);
    int a = luaL_checknumber(L, 9);

    fill_shadow(buffer_info, offset_x, offset_y, radius, r, g, b, a, inner);

    assert(top == lua_gettop(L));
    return 0;
}

static int stroke_path_lua(lua_State* L) {
    int top = lua_gettop(L);

    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);

    float stroke_width = luaL_checknumber(L, 2);
    int r = luaL_checknumber(L, 3);
    int g = luaL_checknumber(L, 4);
    int b = luaL_checknumber(L, 5);
    int a = luaL_checknumber(L, 6);

    StrokePosition position = CENTER;
    if (lua_isstring(L, 7))
    {
        const char* position_str = luaL_checkstring(L, 7);
        if (strcmp(position_str, "center") == 0) {
            position = CENTER;
        } else if (strcmp(position_str, "inside") == 0) {
            position = INSIDE;
        } else if (strcmp(position_str, "outside") == 0) {
            position = OUTSIDE;
        } else {
            return luaL_error(L, "Invalid stroke position: %s", position_str);
        }
    }

    CapStyle cap_style = CAP_BUTT;
    if (lua_isstring(L, 8))
    {
        const char* style = luaL_checkstring(L, 8);
        if (strcmp(style, "round") == 0) cap_style = CAP_ROUND;
        else if (strcmp(style, "square") == 0) cap_style = CAP_SQUARE;
        else if (strcmp(style, "butt") != 0) return luaL_error(L, "Invalid cap style: %s", style);
    }

    JoinStyle join_style = JOIN_MITER;
    if (lua_isstring(L, 9))
    {
        const char* join = luaL_checkstring(L, 9);
        if (strcmp(join, "round") == 0) join_style = JOIN_ROUND;
        else if (strcmp(join, "bevel") == 0) join_style = JOIN_BEVEL;
        else if (strcmp(join, "miter") != 0) return luaL_error(L, "Invalid join style: %s", join);
    }

    stroke_path(buffer_info, stroke_width, position, r, g, b, a, cap_style, join_style);

    assert(top == lua_gettop(L));
    return 0;
}

static int stroke_path_glow_lua(lua_State* L) {
    int top = lua_gettop(L);

    BufferInfo buffer_info = read_and_validate_buffer_info(L, 1);

    float stroke_width = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    int r = luaL_checknumber(L, 5);
    int g = luaL_checknumber(L, 6);
    int b = luaL_checknumber(L, 7);
    int a = luaL_checknumber(L, 8);

    StrokePosition position = CENTER;
    if (lua_isstring(L, 4))
    {
        const char* position_str = luaL_checkstring(L, 4);
        if (strcmp(position_str, "center") == 0) {
            position = CENTER;
        } else if (strcmp(position_str, "inside") == 0) {
            position = INSIDE;
        } else if (strcmp(position_str, "outside") == 0) {
            position = OUTSIDE;
        } else {
            return luaL_error(L, "Invalid stroke position: %s", position_str);
        }
    }

    stroke_path_glow(buffer_info, stroke_width, radius, position, r, g, b, a);

    assert(top == lua_gettop(L));
    return 0;
}

static int create_path_lua(lua_State* L) {
    DM_LUA_STACK_CHECK(L, 1);
    const int handle = create_path();
    lua_pushinteger(L, handle);
    return 1;
}

static int delete_path_lua(lua_State* L) {
    int top = lua_gettop(L);
    int handle = luaL_checknumber(L, 1);
    delete_path(handle);
    assert(top == lua_gettop(L));
    return 0;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] = {
    {"fill_color", fill_color_lua},
    {"create_path", create_path_lua},
    {"delete_path", delete_path_lua},
    {"reset_path", reset_path_lua},
    {"add_point", add_point_lua},
    {"add_quad_curve_point", add_quad_curve_point_lua},
    {"set_path_closed", set_path_closed_lua},
    {"init_buffer", init_buffer_lua},
    {"fill_path", fill_path_lua},
    {"fill_path_gradient", fill_path_gradient_lua},
    {"fill_shadow", fill_shadow_lua},
    {"mask_path", mask_path_lua},
    {"stroke_path", stroke_path_lua},
    {"stroke_path_glow", stroke_path_glow_lua},
    {0, 0}
};

static void LuaInit(lua_State *L) {
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeExtension(dmExtension::AppParams *params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeExtension(dmExtension::Params *params) {
    LuaInit(params->m_L);
    printf("Registered %s Extension\n", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeExtension(dmExtension::AppParams *params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeExtension(dmExtension::Params *params) {
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(BezierPath, LIB_NAME, AppInitializeExtension, AppFinalizeExtension, InitializeExtension, 0, 0, FinalizeExtension)
