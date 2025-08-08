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

--Draws a rounded rect into the point buffer, can be filled with fill_path, stroke_path, ...
function M.rounded_rect(x, y, width, height, edge_radius)
    bezierpath.reset_path()
    bezierpath.add_point(x + width - edge_radius, y)
    bezierpath.add_bezier_point(x + width, y + edge_radius, x + width, y)
    bezierpath.add_point(x + width, y + height - edge_radius)
    bezierpath.add_bezier_point(x + width - edge_radius, y + height, x + width, y + height)
    bezierpath.add_point(x + edge_radius, y + height)
    bezierpath.add_bezier_point(x, y + height - edge_radius, x, y + height)
    bezierpath.add_point(x, y + edge_radius)
    bezierpath.add_bezier_point(x + edge_radius, y, x, y)
    bezierpath.set_path_closed(true)
end

--LOW LEVEL ACCESS

function M.fill_color(buffer_info, color)
    bezierpath.fill_color(buffer_info, color.r, color.g, color.b, color.a)
end

function M.reset_path()
    bezierpath.reset_path()
end

--Add a point to the path
function M.add_point(x, y)
    bezierpath.add_point(x, y)
end

--Add a bezier point to the path
function M.add_bezier_point(x, y, cx, cy)
    bezierpath.add_bezier_point(x, y, cx, cy)
end

--Set if the path is closed
function M.set_path_closed(closed)
    bezierpath.set_path_closed(closed)
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
function M.fill_shadow(buffer_info, offset_x, offset_y, radius, color)
    bezierpath.fill_shadow(buffer_info, offset_x, offset_y, radius, color.r, color.g, color.b, color.a)
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