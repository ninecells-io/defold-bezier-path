package.path = "../?.lua;" .. package.path

package.preload["helpers.color_helper"] = function()
    return {
        black = function(alpha)
            return { r = 0, g = 0, b = 0, a = alpha or 1 }
        end
    }
end

package.preload["helpers.gui_helper"] = function()
    return {
        create_box_node = function()
            return {}
        end
    }
end

local next_handle = 1
local live_paths = {}
local native = {
    create_path = function()
        local handle = next_handle
        next_handle = next_handle + 1
        live_paths[handle] = true
        return handle
    end,
    delete_path = function(handle)
        assert(live_paths[handle])
        live_paths[handle] = nil
    end,
    get_path_bounds = function(handle, padding)
        assert(live_paths[handle])
        return { x = 10 - padding, y = 20 - padding, width = 30 + padding * 2, height = 40 + padding * 2 }
    end,
    init_buffer = function() end,
    fill_color = function() end,
}
_G.bezierpath = native

_G.hash = function(value) return value end
_G.hash_to_hex = function(value) return value end
_G.image = { TYPE_RGB = 1, TYPE_RGBA = 2 }
_G.buffer = {
    VALUE_TYPE_UINT8 = 1,
    create = function(count, declaration)
        return { count = count, declaration = declaration, bytes = "pixels" }
    end,
    get_bytes = function(value)
        return value.bytes
    end,
}

local texture_calls = {}
_G.gui = {
    new_texture = function(name, width, height)
        texture_calls[#texture_calls + 1] = { name = name, width = width, height = height }
        return true
    end,
    delete_texture = function() end,
    set_texture = function() end,
}

local api = dofile("../bezierpath/bezierpath.lua")

local first, second, third = api.with_path(nil, function(handle)
    assert(live_paths[handle])
    return "one", nil, "three"
end)
assert(first == "one" and second == nil and third == "three")
assert(next(live_paths) == nil)

local ok, message = pcall(function()
    api.with_path({}, function()
        error("expected callback failure")
    end)
end)
assert(not ok and message:find("expected callback failure", 1, true))
assert(next(live_paths) == nil)

local handle = api.create_path()
local bounds = api.get_path_bounds(handle, 2)
assert(bounds.x == 8 and bounds.y == 18 and bounds.width == 34 and bounds.height == 44)
api.delete_path(handle)

local seen_params
local pixels, region = api.draw_texture_region(
    { x = 10.25, y = 20.75, width = 5.1, height = 6.1 },
    function(buffer_info, params)
        seen_params = params
        assert(buffer_info.width == 6 and buffer_info.height == 7)
        assert(buffer_info.origin_x == 10 and buffer_info.origin_y == 20)
    end,
    { marker = true }
)
assert(pixels == "pixels" and seen_params.marker)
assert(region.x == 10 and region.y == 20 and region.width == 6 and region.height == 7)

local texture, texture_region = api.create_texture_region(
    "cropped",
    { x = 3.2, y = 4.2, width = 7.2, height = 8.2 },
    function() end
)
assert(type(texture) == "string" and texture:match("^bezierpath_"))
assert(texture_region.x == 3 and texture_region.y == 4)
assert(texture_region.width == 8 and texture_region.height == 9)
assert(texture_calls[#texture_calls].width == 8 and texture_calls[#texture_calls].height == 9)

print("Lua API tests passed")
