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
assert(api.get_default_texture_factor() == 2)
api.set_default_texture_factor(3)
local configured = api.create_texture("configured.factor", 4, 3, function(buffer_info, factor)
    assert(factor == 3)
    assert(buffer_info.width == 12 and buffer_info.height == 9)
end)
assert(dynamic_textures[configured].width == 12 and dynamic_textures[configured].height == 9)
api.set_default_texture_factor(2)
assert(api.set_default_texture_factor(1) == 2)
assert(api.set_default_texture_factor(0 / 0) == 2)
assert(api.get_default_texture_factor() == 2)
local fallback = api.create_texture("invalid.explicit.factor", 4, 3, function(buffer_info, factor)
    assert(factor == 2)
    assert(buffer_info.width == 8 and buffer_info.height == 6)
end, nil, 0)
assert(dynamic_textures[fallback].width == 8 and dynamic_textures[fallback].height == 6)

local draws = 0
local first = api.create_texture("button.primary", 4, 3, function(_, factor, params)
    draws = draws + 1
    assert(factor == 2 and params.value == 42)
end, { value = 42 })
local second = api.create_texture("button.primary", 4, 3, function() draws = draws + 1 end, nil)
assert(first == second and draws == 1, "same-scene calls must reuse the runtime texture")

local scaled = api.create_texture("button.primary", 4, 3, function() draws = draws + 1 end, nil, 1)
assert(scaled ~= first and draws == 2, "factor must be part of texture identity")

for _, cap in ipairs({ 1.25, 1.5, 2 }) do
    api.set_texture_factor_cap(cap)
    assert(api.set_default_texture_factor(3) == 3 and api.get_default_texture_factor() == cap)
    local capped = api.create_texture("button.primary", 4, 3, function(info, factor)
        assert(factor == cap)
        assert(info.width == math.floor(4 * cap) and info.height == math.floor(3 * cap))
    end, nil, 3)
    local same = api.create_texture("button.primary", 4, 3, function() error("expected a cache hit") end)
    assert(same == capped and (capped == first) == (cap == 2))
    assert(api.create_texture("button.primary", 4, 3, function() error("expected 1x reuse") end, nil, 1) == scaled)
    local thin = api.create_texture("thin", 5, 0.3, function(info) assert(info.height == 1) end)
    assert(dynamic_textures[thin].height == 1)
end
assert(not pcall(api.set_texture_factor_cap, -1))
api.set_texture_factor_cap(nil)
assert(api.get_default_texture_factor() == 3)
api.set_default_texture_factor(2)

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

local observed = {}
api.set_performance_observer(function(phase, elapsed, bytes)
    assert(type(phase) == "string" and elapsed >= 0)
    if phase == "bezier.upload" and bytes then observed[#observed + 1] = bytes end
end)
local winner
local result = api.create_texture("racing", 4, 4, function()
    -- A second task can upload a matching texture while the outer task yields.
    winner = api.create_texture("racing", 4, 4, function() end, nil, 1)
end, nil, 1)
assert(result == winner and dynamic_textures[result], "the first completed upload must be reusable")
assert(#observed == 1 and observed[1] == 64, "report bytes only for successful uploads")
api.set_performance_observer(nil)
api.create_texture("without-observer", 4, 4, function() end, nil, 1)
assert(#observed == 1)

print("bezier-path texture API tests passed")
