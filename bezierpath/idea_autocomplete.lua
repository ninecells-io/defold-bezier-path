--AUTOCOMPLETE FOR IDEA DO NOT REMOVE OR REQUIRE
---@class BEZIER_PATH
bezierpath = {}

---Fill the buffer with given color
function bezierpath.fill_color(buffer_info, red, green, blue, alpha) end

--Create a new entry in path record and return handle
function bezierpath.create_path(buffer_info) end

--Delete path record
function bezierpath.delete_path(handle) end

--Resets the current path array
function bezierpath.reset_path(handle) end

--Add a point to the path
function bezierpath.add_point(handle, x, y) end

--Add a arc point to the path
function bezierpath.add_arc_point(handle, center_x, center_y, radius, start_angle, end_angle, clockwise) end

--Add a quad curve point to the path
function bezierpath.add_quad_curve_point(handle, x, y, cx, cy) end

--Set if the path is closed
function bezierpath.set_path_closed(handle, closed) end

--Fill buffer with zero
function bezierpath.init_buffer(buffer_info, color) end

--Fill the current path with given color
function bezierpath.fill_path(buffer_info, red, green, blue, alpha) end

--Fill the current path with a gradient
function bezierpath.fill_path_gradient(buffer_info, red1, green1, blue1, alpha1, red2, green2, blue2, alpha2, angle) end

--Mask the inside or outside of the current path with alpha = 0
function bezierpath.mask_path(buffer_info, inverted) end

--Fill the current path with a inner or outer drop shadow
function bezierpath.fill_shadow(buffer_info, offset_x, offset_y, radius, inner, red, green, blue, alpha) end

--Draw a stroke along the path
function bezierpath.stroke_path(buffer_info, width, red, green, blue, alpha, position, cap_style, join_style) end

--Draw a glow effect along the path
function bezierpath.stroke_path_glow(buffer_info, width, radius, position, red, green, blue, alpha) end


return
