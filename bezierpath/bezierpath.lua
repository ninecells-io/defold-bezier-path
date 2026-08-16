local color_helper = require("helpers.color_helper")
local gui_helper = require("helpers.gui_helper")
local texture_cache = require("bezierpath.texture_cache")

local MINIMUM_TEXTURE_FACTOR = 2
local default_texture_factor = MINIMUM_TEXTURE_FACTOR

local M = {}

local function is_finite(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

function M.set_default_texture_factor(factor)
    default_texture_factor = is_finite(factor) and math.max(MINIMUM_TEXTURE_FACTOR, factor)
        or MINIMUM_TEXTURE_FACTOR
    return default_texture_factor
end

function M.get_default_texture_factor()
    return default_texture_factor
end

-- GUI textures belong to a GUI scene and survive until they are explicitly
-- deleted or the scene is destroyed. Controls commonly create size-specific
-- textures, so a live window resize can otherwise exhaust the scene's dynamic
-- texture slots. A generation lets a scene reclaim the textures produced by
-- its previous full layout immediately before producing the replacement set.
local texture_generations = setmetatable({}, { __mode = "k" })
local active_texture_generation

function M.begin_texture_generation(owner)
    assert(owner ~= nil, "texture generation owner is required")
    assert(active_texture_generation == nil, "a texture generation is already active")

    local previous = texture_generations[owner]
    if previous ~= nil then
        for texture_name in pairs(previous) do
            pcall(gui.delete_texture, texture_name)
        end
    end

    local textures = {}
    texture_generations[owner] = textures
    active_texture_generation = { owner = owner, textures = textures }
end

function M.end_texture_generation(owner)
    assert(active_texture_generation ~= nil, "no texture generation is active")
    assert(active_texture_generation.owner == owner, "texture generation owner mismatch")
    active_texture_generation = nil
end

function M.configure_texture_cache(options)
    texture_cache.configure(options)
end

function M.clear_texture_cache()
    return texture_cache.clear()
end

--HIGH LEVEL Methods

function M.point(x, y)
    return {
        x = x or 0,
        y = y or 0
    }
end

function M.quad_curve_point(point, control_point)
    return {
        x = point.x,
        y = point.y,
        cx = control_point.x,
        cy = control_point.y
    }
end

function M.arc_point(center, radius, start_angle, end_angle, clockwise)
    return {
        center_x = center.x,
        center_y = center.y,
        r = radius,
        a_start = start_angle,
        a_end = end_angle,
        clockwise = clockwise
    }
end

function M.zero()
    return {
        x = 0.0,
        y = 0.0
    }
end

--Adds points based on points table
function M.polygon(handle, points, closed)
    bezierpath.reset_path(handle)
    for _, point in pairs(points) do
        if (point.r ~= nil) then
            bezierpath.add_arc_point(handle, point.center_x, point.center_y, point.r, point.a_start, point.a_end, point.clockwise)
        elseif (point.cx ~= nil) then
            bezierpath.add_quad_curve_point(handle, point.x, point.y, point.cx, point.cy)
        else
            bezierpath.add_point(handle, point.x, point.y)
        end
    end
    bezierpath.set_path_closed(handle, closed or false)
end

--Draws a rounded rect into the point buffer, can be filled with fill_path, stroke_path, ...
function M.rounded_rect(handle, left, top, width, height, edge_radius)
    M.polygon(handle, {
        M.point(left + width - edge_radius, top),
        M.quad_curve_point(M.point(left + width, top + edge_radius), M.point(left + width, top)),
        M.point(left + width, top + height - edge_radius),
        M.quad_curve_point(M.point(left + width - edge_radius, top + height), M.point(left + width, top + height)),
        M.point(left + edge_radius, top + height),
        M.quad_curve_point(M.point(left, top + height - edge_radius), M.point(left, top + height)),
        M.point(left, top + edge_radius),
        M.quad_curve_point(M.point(left + edge_radius, top), M.point(left, top))
    }, true)
end

function M.circle(handle, left, top, radius_x, radius_y)
    -- If it's a true circle, use the arc segment
    if math.abs((radius_x or 0) - (radius_y or 0)) < 1e-6 then
        local r  = radius_x
        local cx = left + r
        local cy = top  + r

        M.polygon(handle, {
            M.point(cx + r, cy),
            M.arc_point(M.point(cx, cy), r, 0.0, math.pi * 2.0, false),
        }, true)
        return
    end

    -- Otherwise keep ellipse approximation with 4 quadratics
    M.polygon(handle, {
        M.quad_curve_point(M.point(left + radius_x, top), M.point(left, top)),
        M.quad_curve_point(M.point(left + radius_x * 2, top + radius_y), M.point(left + radius_x * 2,  top)),
        M.quad_curve_point(M.point(left + radius_x, top + radius_y * 2), M.point(left + radius_x * 2,  top + radius_y * 2)),
        M.quad_curve_point(M.point(left, top + radius_y), M.point(left, top + radius_y * 2)),
    }, true)
end

function M.pill(handle, left, top, width, height)
    -- Guard
    if width <= 0 or height <= 0 then
        bezierpath.reset_path(handle)
        bezierpath.set_path_closed(handle, false)
        return
    end

    if width >= height then
        -- Horizontal pill: semicircles on left/right, radius = height/2
        local r    = height * 0.5
        local cy   = top + r
        local cxL  = left + r
        local cxR  = left + width - r

        M.polygon(handle, {
            M.point(cxL, top),
            M.point(cxR, top),
            M.arc_point(M.point(cxR, cy), r, -math.pi/2, math.pi/2, false),
            M.point(cxL, top + height),
            M.arc_point(M.point(cxL, cy), r,  math.pi/2, -math.pi/2, false)
        }, true)
    else
        -- Vertical pill: semicircles on top/bottom, radius = width/2
        local r    = width * 0.5
        local cx   = left + r
        local cyT  = top + r
        local cyB  = top + height - r


        M.polygon(handle, {
            M.point(left + width, cyT),
            M.point(left + width, cyB),
            M.arc_point(M.point(cx, cyB), r, 0.0, math.pi, false),
            M.point(left, cyT),
            M.arc_point(M.point(cx, cyT), r, math.pi, math.pi * 2.0, false)
        }, true)
    end
end

function M.line(handle, point1, point2)
    M.polygon(handle, {
        M.point(point1.x, point1.y),
        M.point(point2.x, point2.y)
    }, false)
end

function M.init_buffer(buffer_info, color)
    bezierpath.init_buffer(buffer_info)
    M.fill_color(buffer_info, color)
end

--LOW LEVEL ACCESS

function M.fill_color(buffer_info, color)
    bezierpath.fill_color(buffer_info, color.r, color.g, color.b, color.a)
end

function M.create_path(buffer_info)
    local path = bezierpath.create_path()
    if buffer_info ~= nil then
        buffer_info.path_handle = path
    end
    return path
end

function M.delete_path(handle)
    bezierpath.delete_path(handle)
end

function M.get_path_bounds(handle, padding)
    return bezierpath.get_path_bounds(handle, padding or 0)
end

local function pack(...)
    return { n = select("#", ...), ... }
end

function M.with_path(buffer_info, fn)
    local handle = M.create_path(buffer_info)
    local results
    local ok, err = xpcall(function()
        results = pack(fn(handle))
    end, debug.traceback)
    M.delete_path(handle)
    if not ok then
        error(err, 0)
    end
    return unpack(results, 1, results.n)
end

function M.clear_all_paths()
    bezierpath.clear_all_paths()
end

function M.reset_path(handle)
    bezierpath.reset_path(handle)
end

--Add a point to the path
function M.add_point(handle, point)
    bezierpath.add_point(handle, point.x, point.y)
end

--Add a quad curve point to the path
function M.add_quad_curve_point(handle, point, controlPoint)
    bezierpath.add_quad_curve_point(handle, point.x, point.y, controlPoint.x, controlPoint.y)
end

-- Add a circular arc segment (UIKit-like). Angles in radians.
-- center = { x=..., y=... }, radius = number, clockwise = boolean
function M.add_arc(path_handle, center, radius, start_angle, end_angle, clockwise)
    bezierpath.add_arc_point(path_handle, center.x, center.y, radius, start_angle, end_angle, clockwise)
end

--Set if the path is closed
function M.set_path_closed(handle, closed)
    bezierpath.set_path_closed(handle, closed)
end

--Fill the current path with given color
function M.fill_path(buffer_info, color)
    bezierpath.fill_path(buffer_info, color.r, color.g, color.b, color.a)
end

function M.mask_path(buffer_info, inverted)
    bezierpath.mask_path(buffer_info, inverted)
end

--Fill the current path with a gradient
function M.fill_path_gradient(buffer_info, color1, color2, angle)
    bezierpath.fill_path_gradient(
        buffer_info,
        color1.r, color1.g, color1.b, color1.a,
        color2.r, color2.g, color2.b, color2.a,
        angle
    )
end

--Fill the current path with a gradient along the form of the shape
function M.fill_path_gradient_shape(buffer_info, color1, color2)
    bezierpath.fill_path_gradient_shape(
        buffer_info,
        color1.r, color1.g, color1.b, color1.a,
        color2.r, color2.g, color2.b, color2.a
    )
end

--Fill the current path with a shadow
function M.fill_shadow(buffer_info, offset, radius, inner, color)
    bezierpath.fill_shadow(buffer_info, offset.x, offset.y, radius, inner, color.r, color.g, color.b, color.a)
end

--Draw a stroke along the path
function M.stroke_path(buffer_info, width, color, position, cap_style, join_style)
    bezierpath.stroke_path(buffer_info, width, color.r, color.g, color.b, color.a, position, cap_style, join_style)
end

--Draw a glow effect along the path
function M.stroke_path_glow(buffer_info, width, radius, position, color)
    bezierpath.stroke_path_glow(buffer_info, width, radius, position, color.r, color.g, color.b, color.a)
end

function M.draw_texture(width, height, draw_fn, factor, params)
    local channels = 4

    local buffer_info = {
        buffer = buffer.create(
            width * height,
            {{
                name = hash("rgba"),
                type = buffer.VALUE_TYPE_UINT8,
                count = channels
            }}
        ),
        width = width,
        height = height,
        channels = channels,
        premultiply_alpha = true,
        path_handle = 0,
    }

    M.init_buffer(buffer_info, color_helper.black(0))
    draw_fn(buffer_info, factor, params)

    return buffer.get_bytes(buffer_info.buffer, hash("rgba"))
end

local function normalize_bounds(bounds)
    assert(type(bounds) == "table", "bounds must be a table")
    local x = math.floor(tonumber(bounds.x) or 0)
    local y = math.floor(tonumber(bounds.y) or 0)
    local right = math.ceil((tonumber(bounds.x) or 0) + (tonumber(bounds.width) or 0))
    local top = math.ceil((tonumber(bounds.y) or 0) + (tonumber(bounds.height) or 0))
    return {
        x = x,
        y = y,
        width = math.max(1, right - x),
        height = math.max(1, top - y),
    }
end

function M.draw_texture_region(bounds, draw_fn, params)
    local region = normalize_bounds(bounds)
    local channels = 4
    local buffer_info = {
        buffer = buffer.create(
            region.width * region.height,
            {{
                name = hash("rgba"),
                type = buffer.VALUE_TYPE_UINT8,
                count = channels
            }}
        ),
        width = region.width,
        height = region.height,
        channels = channels,
        premultiply_alpha = true,
        path_handle = 0,
        origin_x = region.x,
        origin_y = region.y,
    }

    M.init_buffer(buffer_info, color_helper.black(0))
    draw_fn(buffer_info, params)
    return buffer.get_bytes(buffer_info.buffer, hash("rgba")), region
end

function M.add_gui_box(root, width, height, pos, visible, texture_key, draw_fn, params)
    local texture_name = M.create_texture(texture_key, width, height,
        function(buffer_info, _, draw_params)
            draw_fn(buffer_info, draw_params)
        end,
        params,
        1
    )
    local box = gui_helper.create_box_node(root, width, height, pos, nil, visible)
    gui.set_texture(box, texture_name)

    return box, texture_name
end

local function create_gui_texture(texture_name, width, height, pixels_fn)
    if active_texture_generation ~= nil then
        active_texture_generation.textures[texture_name] = true
    end
    local created, reason = gui.new_texture(texture_name, 1, 1, image.TYPE_RGB, string.rep('\0', 3))
    if not created then
        if reason == nil or reason == gui.RESULT_TEXTURE_ALREADY_EXISTS then
            return texture_name, false
        end
        error("Unable to reserve texture: " .. tostring(reason))
    end
    gui.delete_texture(texture_name)

    local pixels = pixels_fn()
    local ok, create_reason = gui.new_texture(texture_name, width, height, image.TYPE_RGBA, pixels)
    if not ok then
        error("Unable to create texture: " .. tostring(create_reason))
    end
    return texture_name, true
end

function M.new_texture(width, height, key, texture, texture_size)
    if texture == nil then return nil end
    local w = texture_size and texture_size.width or width
    local h = texture_size and texture_size.height or height
    local record = texture_cache.describe("bytes", key, w, h, 1)
    local texture_name = create_gui_texture(record.runtime_name, w, h, function() return texture end)
    return texture_name
end

function M.set_node_texture(node, width, height, key, texture, texture_size)
    local texture_name = M.new_texture(width, height, key, texture, texture_size)
    if texture_name ~= nil then gui.set_texture(node, texture_name) end
    return texture_name
end

function M.create_texture(key, width, height, draw_fn, params, factor)

    factor = factor or default_texture_factor
    if not is_finite(factor) or factor <= 0 then factor = default_texture_factor end

    width = math.floor(width * factor)
    height = math.floor(height * factor)

    local record = texture_cache.describe("texture", key, width, height, factor)
    local texture_name = create_gui_texture(record.runtime_name, width, height, function()
        local pixels = texture_cache.load(record)
        if pixels ~= nil then return pixels end
        pixels = M.draw_texture(width, height, draw_fn, factor, params)
        texture_cache.store(record, pixels)
        return pixels
    end)
    return texture_name
end


function M.create_texture_region(key, bounds, draw_fn, params)
    local region = normalize_bounds(bounds)
    local record = texture_cache.describe("region", key, region.width, region.height, 1)
    local texture_name = create_gui_texture(record.runtime_name, region.width, region.height, function()
        local pixels = texture_cache.load(record)
        if pixels ~= nil then return pixels end
        pixels, region = M.draw_texture_region(region, draw_fn, params)
        texture_cache.store(record, pixels)
        return pixels
    end)
    return texture_name, region
end

return M
