local color_helper = require("bezierpath.color_helper")

local M = {}

--HIGH LEVEL Methods

function M.point(x, y)
    return {
        x = x or 0,
        y = y or 0
    }
end

function M.quadCurvePoint(point, controlPoint)
    return {
        x = point.x,
        y = point.y,
        cx = controlPoint.x,
        cy = controlPoint.y
    }
end

function M.arcPoint(center, radius, start_angle, end_angle, clockwise)
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

function M.white(alpha)
    return color_helper.rgbaToColor(255, 255, 255, alpha or 1)
end

function M.black(alpha)
    return color_helper.rgbaToColor(0, 0, 0, alpha or 1)
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
        M.quadCurvePoint(M.point(left + width, top + edge_radius), M.point(left + width, top)),
        M.point(left + width, top + height - edge_radius),
        M.quadCurvePoint(M.point(left + width - edge_radius, top + height), M.point(left + width, top + height)),
        M.point(left + edge_radius, top + height),
        M.quadCurvePoint(M.point(left, top + height - edge_radius), M.point(left, top + height)),
        M.point(left, top + edge_radius),
        M.quadCurvePoint(M.point(left + edge_radius, top), M.point(left, top))
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
            M.arcPoint(M.point(cx, cy), r, 0.0, math.pi * 2.0, false),
        }, true)
        return
    end

    -- Otherwise keep ellipse approximation with 4 quadratics
    M.polygon(handle, {
        M.quadCurvePoint(M.point(left + radius_x, top), M.point(left, top)),
        M.quadCurvePoint(M.point(left + radius_x * 2, top + radius_y), M.point(left + radius_x * 2,  top)),
        M.quadCurvePoint(M.point(left + radius_x, top + radius_y * 2), M.point(left + radius_x * 2,  top + radius_y * 2)),
        M.quadCurvePoint(M.point(left, top + radius_y), M.point(left, top + radius_y * 2)),
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
            M.arcPoint(M.point(cxR, cy), r, -math.pi/2, math.pi/2, false),
            M.point(cxL, top + height),
            M.arcPoint(M.point(cxL, cy), r,  math.pi/2, -math.pi/2, false)
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
            M.arcPoint(M.point(cx, cyB), r, 0.0, math.pi, false),
            M.point(left, cyT),
            M.arcPoint(M.point(cx, cyT), r, math.pi, math.pi * 2.0, false)
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
    buffer_info.path_handle = path
    return path
end

function M.delete_path(handle)
    bezierpath.delete_path(handle)
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

return M