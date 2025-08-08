--AUTOCOMPLETE FOR IDEA DO NOT REMOVE OR REQUIRE
---@class BEZIER_PATH
bezierpath = {}

---Fill the buffer with given color
function bezierpath.fill_color(buffer_info, red, green, blue, alpha) end

--Resets the current path array
function bezierpath.reset_path() end

--Add a point to the path
function bezierpath.add_point(x, y) end

--Add a bezier point to the path
function bezierpath.add_bezier_point(x, y, cx, cy) end

--Set if the path is closed
function bezierpath.set_path_closed(closed) end

--Fill the current path with given color
function bezierpath.fill_path(buffer_info, red, green, blue, alpha) end

--Fill the current path with a gradient
function bezierpath.fill_path_gradient(buffer_info, red1, green1, blue1, alpha1, red2, green2, blue2, alpha2, angle) end

--Fill the current path with a shadow
function bezierpath.fill_shadow(buffer_info, offset_x, offset_y, radius, red, green, blue, alpha) end

--Draw a stroke along the path
function bezierpath.stroke_path(buffer_info, width, red, green, blue, alpha, position, cap_style, join_style) end

--Draw a glow effect along the path
function bezierpath.stroke_path_glow(buffer_info, width, radius, position, red, green, blue, alpha) end


return
