-- Native global API declarations for IDEs. Do not require this file.
---@class BEZIER_PATH
bezierpath = {}

---Replace every pixel with the given color.
---@param buffer_info table
---@param red number
---@param green number
---@param blue number
---@param alpha number
function bezierpath.fill_color(buffer_info, red, green, blue, alpha) end

---Return a new native path handle.
---@return number
function bezierpath.create_path() end

---Release a path handle.
---@param handle number
function bezierpath.delete_path(handle) end

---Return {x,y,width,height}, or nil for an empty path; padding defaults to zero.
---@param handle number
---@param padding number?
---@return table|nil
function bezierpath.get_path_bounds(handle, padding) end

---Release all native paths; only call when no owner retains handles.
function bezierpath.clear_all_paths() end

---Clear points and mark the path open.
---@param handle number
function bezierpath.reset_path(handle) end

---Append a straight segment endpoint.
---@param handle number
---@param x number
---@param y number
function bezierpath.add_point(handle, x, y) end

---Append a quadratic endpoint and control point.
---@param handle number
---@param x number
---@param y number
---@param cx number
---@param cy number
function bezierpath.add_quad_curve_point(handle, x, y, cx, cy) end

---Append a circular arc; angles are radians and clockwise decreases angles.
---@param handle number
---@param center_x number
---@param center_y number
---@param radius number
---@param start_angle number
---@param end_angle number
---@param clockwise boolean
function bezierpath.add_arc_point(handle, center_x, center_y, radius, start_angle, end_angle, clockwise) end

---Set whether the contour is closed.
---@param handle number
---@param closed boolean
function bezierpath.set_path_closed(handle, closed) end

---Zero the complete buffer.
---@param buffer_info table
function bezierpath.init_buffer(buffer_info) end

---Fill the selected path.
---@param buffer_info table
---@param red number
---@param green number
---@param blue number
---@param alpha number
function bezierpath.fill_path(buffer_info, red, green, blue, alpha) end

---Fill a linear gradient; angle is in degrees.
---@param buffer_info table
---@param red1 number
---@param green1 number
---@param blue1 number
---@param alpha1 number
---@param red2 number
---@param green2 number
---@param blue2 number
---@param alpha2 number
---@param angle number
function bezierpath.fill_path_gradient(buffer_info, red1, green1, blue1, alpha1, red2, green2, blue2, alpha2, angle) end

---Fill a gradient from the contour toward the interior.
---@param buffer_info table
---@param red1 number
---@param green1 number
---@param blue1 number
---@param alpha1 number
---@param red2 number
---@param green2 number
---@param blue2 number
---@param alpha2 number
function bezierpath.fill_path_gradient_shape(buffer_info, red1, green1, blue1, alpha1, red2, green2, blue2, alpha2) end

---Composite an inner or outer blurred shadow.
---@param buffer_info table
---@param offset_x number
---@param offset_y number
---@param radius number
---@param inner boolean
---@param red number
---@param green number
---@param blue number
---@param alpha number
function bezierpath.fill_shadow(buffer_info, offset_x, offset_y, radius, inner, red, green, blue, alpha) end

---Composite an outer shadow and clear the interior of the mask path.
---@param buffer_info table
---@param offset_x number
---@param offset_y number
---@param radius number
---@param red number
---@param green number
---@param blue number
---@param alpha number
---@param mask_handle? number The path to mask, or the shadow path if omitted.
function bezierpath.fill_shadow_with_mask(buffer_info, offset_x, offset_y, radius, red, green, blue, alpha, mask_handle) end

---Clear alpha inside the path; inverted=true clears the exterior.
---@param buffer_info table
---@param inverted boolean
function bezierpath.mask_path(buffer_info, inverted) end

---Stroke a path; defaults are center, butt, miter.
---@param buffer_info table
---@param width number
---@param red number
---@param green number
---@param blue number
---@param alpha number
---@param position string?
---@param cap_style string?
---@param join_style string?
function bezierpath.stroke_path(buffer_info, width, red, green, blue, alpha, position, cap_style, join_style) end

---Draw a blurred stroke; position defaults to center.
---@param buffer_info table
---@param width number
---@param radius number
---@param position string?
---@param red number
---@param green number
---@param blue number
---@param alpha number
function bezierpath.stroke_path_glow(buffer_info, width, radius, position, red, green, blue, alpha) end

---Queue a solid fill when supported. Returns a job ID, or nil with an optional busy status.
---@param buffer_info table
---@param red number
---@param green number
---@param blue number
---@param alpha number
---@return number? id
---@return string? status
function bezierpath.begin_fill_path(buffer_info, red, green, blue, alpha) end

---Queue a linear gradient fill when supported. Returns a job ID, or nil with an optional busy status.
---@param buffer_info table
---@param red1 number
---@param green1 number
---@param blue1 number
---@param alpha1 number
---@param red2 number
---@param green2 number
---@param blue2 number
---@param alpha2 number
---@param angle number
---@return number? id
---@return string? status
function bezierpath.begin_fill_path_gradient(buffer_info, red1, green1, blue1, alpha1, red2, green2, blue2, alpha2, angle) end

---Queue a shadow fill when supported. Returns a job ID, or nil with an optional busy status.
---@param buffer_info table
---@param offset_x number
---@param offset_y number
---@param radius number
---@param inner boolean
---@param red number
---@param green number
---@param blue number
---@param alpha number
---@return number? id
---@return string? status
function bezierpath.begin_fill_shadow(buffer_info, offset_x, offset_y, radius, inner, red, green, blue, alpha) end

---Queue an outer shadow and its mask together.
---@param buffer_info table
---@param offset_x number
---@param offset_y number
---@param radius number
---@param red number
---@param green number
---@param blue number
---@param alpha number
---@param mask_handle? number The path to mask, or the shadow path if omitted.
---@return number? id
---@return string? status
function bezierpath.begin_fill_shadow_with_mask(buffer_info, offset_x, offset_y, radius, red, green, blue, alpha, mask_handle) end

---Queue a path mask when supported. Returns a job ID, or nil with an optional busy status.
---@param buffer_info table
---@param inverted boolean
---@return number? id
---@return string? status
function bezierpath.begin_mask_path(buffer_info, inverted) end

---Queue a large path stroke. Uses the same arguments as stroke_path.
---@param buffer_info table
---@param width number
---@param red number
---@param green number
---@param blue number
---@param alpha number
---@param position? string
---@param cap_style? string
---@param join_style? string
---@return number? id
---@return string? status
function bezierpath.begin_stroke_path(buffer_info, width, red, green, blue, alpha, position, cap_style, join_style) end

---Copy the completed raster job into its original buffer. Returns true when ready.
---@param id number
---@param buffer_info table
---@return boolean? done
---@return number? queue_seconds
---@return number? worker_seconds
function bezierpath.poll_raster(id, buffer_info) end

---Cancel or discard the specified raster job.
---@param id number
function bezierpath.cancel_raster(id) end

---Enable optional queue and worker durations on completed raster polls.
---@param enabled boolean
function bezierpath.set_raster_job_timing(enabled) end
