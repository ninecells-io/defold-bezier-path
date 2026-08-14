package.path = "./?.lua;./?/init.lua;" .. package.path

local hashed = {}
function hash(value)
    value = tostring(value)
    if hashed[value] then return hashed[value] end
    local result = 7
    for index = 1, #value do result = (result * 131 + string.byte(value, index)) % 4294967291 end
    hashed[value] = result
    return result
end
function hash_to_hex(value) return tostring(value) end

local dynamic_textures = {}
local texture_creations = 0
local texture_deletions = 0
gui = {
    RESULT_TEXTURE_ALREADY_EXISTS = 1,
    new_texture = function(name, width, height, texture_type, pixels)
        if dynamic_textures[name] then return false, 1 end
        dynamic_textures[name] = { width = width, height = height, texture_type = texture_type, pixels = pixels }
        texture_creations = texture_creations + 1
        return true
    end,
    delete_texture = function(name)
        if dynamic_textures[name] then texture_deletions = texture_deletions + 1 end
        dynamic_textures[name] = nil
    end,
    set_texture = function(node, name) node.texture = name end,
}

image = { TYPE_RGB = "rgb", TYPE_RGBA = "rgba" }
buffer = {
    VALUE_TYPE_UINT8 = 1,
    create = function(count) return { count = count } end,
    get_bytes = function(value) return string.rep("x", value.count * 4) end,
}

bezierpath = {
    init_buffer = function() end,
    fill_color = function() end,
}

package.preload["helpers.color_helper"] = function()
    return { black = function(alpha) return { r = 0, g = 0, b = 0, a = alpha or 1 } end }
end
package.preload["helpers.gui_helper"] = function()
    return {
        create_box_node = function(root, width, height, position, _, visible)
            return { root = root, width = width, height = height, position = position, visible = visible }
        end,
    }
end

local api = require("bezierpath.bezierpath")
api.configure_texture_cache({ enabled = false })

local draws = 0
local first = api.create_texture("button.primary", 4, 3, function(_, factor, params)
    draws = draws + 1
    assert(factor == 2 and params.value == 42)
end, { value = 42 })
local second = api.create_texture("button.primary", 4, 3, function() draws = draws + 1 end, nil)
assert(first == second and draws == 1, "same-scene calls must reuse the runtime texture")

local scaled = api.create_texture("button.primary", 4, 3, function() draws = draws + 1 end, nil, 1)
assert(scaled ~= first and draws == 2, "factor must be part of texture identity")

local box, box_texture = api.add_gui_box({}, 8, 6, {}, true, "panel", function(_, params)
    draws = draws + 1
    assert(params.kind == "panel")
end, { kind = "panel" })
assert(box.texture == box_texture)

local regions = 0
local region_name, region = api.create_texture_region("hover", { x = 1.2, y = 2.3, width = 4.1, height = 5.1 }, function()
    regions = regions + 1
end)
local repeated_name = api.create_texture_region("hover", { x = 1.2, y = 2.3, width = 4.1, height = 5.1 }, function()
    regions = regions + 1
end)
assert(region_name == repeated_name and regions == 1)
assert(region.x == 1 and region.y == 2 and region.width == 5 and region.height == 6)
assert(texture_creations > 0)

local layout = {}
api.begin_texture_generation(layout)
local old_layout_texture = api.create_texture("layout.panel", 10, 6, function() end)
api.end_texture_generation(layout)
assert(dynamic_textures[old_layout_texture], "the current layout texture must remain available")

api.begin_texture_generation(layout)
assert(not dynamic_textures[old_layout_texture], "the previous layout texture must be reclaimed")
local new_layout_texture = api.create_texture("layout.panel", 12, 6, function() end)
api.end_texture_generation(layout)
assert(dynamic_textures[new_layout_texture], "the replacement layout texture must be available")
assert(texture_deletions > 0, "starting a replacement generation must delete old textures")

print("bezier-path texture API tests passed")
