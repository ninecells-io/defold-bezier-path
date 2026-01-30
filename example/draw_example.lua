local bp = require("bezierpath/bezierpath")
local color_helper = require("bezierpath.color_helper")

local M = {}

function M.draw(sprite)
	local width = 512
	local height = 512
	local channels = 4

	-- we have to create table with next fields: buffer, width, height, channels
	local buffer_info = {
		buffer = buffer.create(width * height, {{name = hash("rgba"), type = buffer.VALUE_TYPE_UINT8, count = channels}}),
		width = width,
		height = height,
		channels = channels, -- 3 for rgb, 4 for rgba
	}

	local handle = bp.create_path(buffer_info)

	local resource_path = go.get(sprite, "texture0")
	local header = {
		width = width,
		height = height,
		type = resource.TEXTURE_TYPE_2D,
		format = resource.TEXTURE_FORMAT_RGBA,
		num_mip_maps = 1
	}
	bp.init_buffer(buffer_info, color_helper.rgba_to_color(100, 100, 100, 128)) -- Semi-transparent grey

	bp.rounded_rect(handle, 20, 20, 300, 250, 32)
	bp.fill_path_gradient(
		buffer_info,
		color_helper.rgba_to_color(150, 150, 150, 255),
		color_helper.rgba_to_color(150, 150, 150, 255),
		45
	)

	bp.rounded_rect(handle, 100, 60, 300, 250, 32)
	bp.fill_shadow(buffer_info, bp.zero(), 20, false, bp.black(0.8))
	bp.fill_path_gradient(
		buffer_info,
		color_helper.rgba_to_color(255, 0, 0, 200),
		color_helper.rgba_to_color(0, 0, 255, 200),
		45
	)

	bp.line(handle, bp.point(110, 120), bp.point(390, 120))
	bp.stroke_path(buffer_info, 4, color_helper.white(0.8), "center", "round")

	bp.rounded_rect(handle, 240, 240, 200, 55, 16)
	bp.fill_shadow(buffer_info, bp.zero(), 10, false, color_helper.rgba_to_color(0, 0, 0, 222))
	bp.fill_path(
		buffer_info,
		color_helper.rgba_to_color(0, 255, 0, 150)
	)
	bp.stroke_path(
		buffer_info,
		2,
		bp.black(0.2),
		"center", "round", "round"
	)

	bp.polygon(handle, {
		bp.point(100, 500),
		bp.point(200, 400),
		bp.point(300, 500),
		bp.point(400, 400)
	})

	bp.stroke_path(buffer_info, 20, color_helper.rgba_to_color(255, 0, 0, 255), "center", "round", "round")

	resource.set_texture(resource_path, header, buffer_info.buffer)

	bp.delete_path(handle)
end

function M.drawGui()

	local parent = gui.new_box_node(
		vmath.vector3(256, 256, 0),
		vmath.vector3(0)
	)

	gui.set_inherit_alpha(parent, false)
	gui.set_alpha(parent, 0)
	gui.set_blend_mode(parent, gui.BLEND_ALPHA)
	gui.set_size_mode(parent, gui.SIZE_MODE_MANUAL)
	gui.set_visible(parent, true)
	
	--local color = color_helper.rgba_to_color(255, 0, 0, 255)
	--M.addBox(512, 512, color, color_helper.rgba_to_color(0, 0, 255, 255), "box1", parent)

	--M.circle(512, 512, color_helper.rgba_to_color(0, 0, 255, 255), color_helper.rgba_to_color(0, 0, 200, 255), "circle", parent)
	M.pill(130, 250, color_helper.rgba_to_color(255, 0, 0, 1), color_helper.rgba_to_color(0, 0, 200, 1), "pill", parent)
end

function M.addBox(width, height, color, color2, name, parent)
	local buffer_info = {
		buffer = buffer.create(width * height, {{name = hash("rgba"), type = buffer.VALUE_TYPE_UINT8, count = 4}}),
		width = width,
		height = height,
		channels = 4,
		premultiply_alpha = true
	}
	local handle = bp.create_path(buffer_info)
	bp.init_buffer(buffer_info, bp.black(0))
	bp.rounded_rect(handle, 60, 200, 300, 300, 8)
	bp.fill_path_gradient(buffer_info, color, color2, 45)
	bp.fill_shadow(buffer_info, bp.point(), 28, true, bp.black(0.6))

	--bp.rounded_rect(handle, 20, 20, 100, 100, 16)
	--bp.mask_path(buffer_info, false)

	local node = gui.new_box_node(
		vmath.vector3(0, 0, 0),
		vmath.vector3(width, height, 0)
	)

	gui.set_inherit_alpha(node, false)
	gui.set_alpha(node, 1)
	gui.set_blend_mode(node, gui.BLEND_ALPHA)
	gui.set_parent(node, parent, false)
	gui.set_size_mode(node, gui.SIZE_MODE_MANUAL)
	gui.set_visible(node, true)

	local texture = buffer.get_bytes(buffer_info.buffer, hash("rgba"))
	if gui.new_texture(name, width, height, image.TYPE_RGBA, texture) then
		gui.set_texture(node, name)
	else
		error("Unable to create texture")
	end

	return node
end

function M.pill(width, height, color, color2, name, parent)
	local buffer_info = {
		buffer = buffer.create(width * height, {{name = hash("rgba"), type = buffer.VALUE_TYPE_UINT8, count = 4}}),
		width = width,
		height = height,
		channels = 4,
		premultiply_alpha = true
	}
	local handle = bp.create_path(buffer_info)
	bp.init_buffer(buffer_info, bp.black(0))
	bp.pill(handle, 0, 0, width, height)
	--bp.fill_path_gradient(buffer_info, color, color2, 45)
	bp.fill_path_gradient_shape(buffer_info, color, color2)
	
	--bp.fill_path(buffer_info, color)
	--bp.fill_shadow(buffer_info, bp.point(0, 0), 28, true, bp.black(0.6))
	
	--bp.rounded_rect(handle, 20, 20, 100, 100, 16)
	--bp.mask_path(buffer_info, false)

	local node = gui.new_box_node(
		vmath.vector3(0, 0, 0),
		vmath.vector3(width, height, 0)
	)

	gui.set_inherit_alpha(node, false)
	gui.set_alpha(node, 1)
	gui.set_blend_mode(node, gui.BLEND_ALPHA)
	gui.set_parent(node, parent, false)
	gui.set_size_mode(node, gui.SIZE_MODE_MANUAL)
	gui.set_visible(node, true)

	local texture = buffer.get_bytes(buffer_info.buffer, hash("rgba"))
	if gui.new_texture(name, width, height, image.TYPE_RGBA, texture) then
		gui.set_texture(node, name)
	else
		error("Unable to create texture")
	end

	return node
end

function M.circle(width, height, color, color2, name, parent)
	local buffer_info = {
		buffer = buffer.create(width * height, {{name = hash("rgba"), type = buffer.VALUE_TYPE_UINT8, count = 4}}),
		width = width,
		height = height,
		channels = 4,
		premultiply_alpha = true
	}
	local handle = bp.create_path(buffer_info)
	bp.init_buffer(buffer_info, bp.black(0))
	bp.circle(handle, 120, 300, 80, 80)
	bp.fill_shadow(buffer_info, bp.point(20, 20), 28, false, bp.black(0.5))
	bp.fill_path_gradient(buffer_info, color, color2, 45)
	bp.fill_shadow(buffer_info, bp.point(4, -4), 28, true, bp.black(0.6))

	--bp.rounded_rect(handle, 20, 20, 100, 100, 16)
	--bp.mask_path(buffer_info, false)

	local node = gui.new_box_node(
		vmath.vector3(0, 0, 0),
		vmath.vector3(width, height, 0)
	)

	gui.set_inherit_alpha(node, false)
	gui.set_alpha(node, 1)
	gui.set_blend_mode(node, gui.BLEND_ALPHA)
	gui.set_parent(node, parent, false)
	gui.set_size_mode(node, gui.SIZE_MODE_MANUAL)
	gui.set_visible(node, true)

	local texture = buffer.get_bytes(buffer_info.buffer, hash("rgba"))
	if gui.new_texture(name, width, height, image.TYPE_RGBA, texture) then
		gui.set_texture(node, name)
	else
		error("Unable to create texture")
	end

	return node
end

return M