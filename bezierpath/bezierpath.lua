local M = {}

--HIGH LEVEL Methods

function M.rgbaToColor(red, green, blue, alpha)
    return {
        r = red,
        g = green,
        b = blue,
        a = alpha
    }
end

function M.point(x, y)
    return {
        x = x,
        y = y
    }
end

function M.bezierPoint(point, controlPoint)
    return {
        x = point.x,
        y = point.y,
        cx = controlPoint.x,
        cy = controlPoint.y
    }
end

function M.zero()
    return {
        x = 0.0,
        y = 0.0
    }
end

function M.white(alpha)
    return M.rgbaToColor(255, 255, 255, alpha or 255)
end

function M.black(alpha)
    return M.rgbaToColor(0, 0, 0, alpha or 255)
end

--Adds points based on points table
function M.polygon(handle, points, closed)
    bezierpath.reset_path(handle)
    for _, point in pairs(points) do
        if (point.cx ~= nil) then
            bezierpath.add_bezier_point(handle, point.x, point.y, point.cx, point.cy)
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
        M.bezierPoint(M.point(left + width, top + edge_radius), M.point(left + width, top)),
        M.point(left + width, top + height - edge_radius),
        M.bezierPoint(M.point(left + width - edge_radius, top + height), M.point(left + width, top + height)),
        M.point(left + edge_radius, top + height),
        M.bezierPoint(M.point(left, top + height - edge_radius), M.point(left, top + height)),
        M.point(left, top + edge_radius),
        M.bezierPoint(M.point(left + edge_radius, top), M.point(left, top))
    }, true)
end

function M.line(handle, point1, point2)
    bezierpath.reset_path(handle)
    bezierpath.add_point(handle, point1.x, point1.y)
    bezierpath.add_point(handle, point2.x, point2.y)
    bezierpath.set_path_closed(handle, false)
end

--LOW LEVEL ACCESS

function M.fill_color(buffer_info, color)
    bezierpath.fill_color(buffer_info, color.r, color.g, color.b, color.a)
end

function M.create_path()
    return bezierpath.create_path()
end

function M.delete_path(handle)
    bezierpath.delete_path()
end

function M.reset_path(handle)
    bezierpath.reset_path(handle)
end

--Add a point to the path
function M.add_point(handle, point)
    bezierpath.add_point(handle, point.x, point.y)
end

--Add a bezier point to the path
function M.add_bezier_point(handle, point, controlPoint)
    bezierpath.add_bezier_point(handle, point.x, point.y, controlPoint.x, controlPoint.y)
end

--Set if the path is closed
function M.set_path_closed(handle, closed)
    bezierpath.set_path_closed(handle, closed)
end

--Fill the current path with given color
function M.fill_path(buffer_info, color)
    bezierpath.fill_path(buffer_info, color.r, color.g, color.b, color.a)
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
function M.fill_shadow(buffer_info, offset, radius, color)
    bezierpath.fill_shadow(buffer_info, offset.x, offset.y, radius, color.r, color.g, color.b, color.a)
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