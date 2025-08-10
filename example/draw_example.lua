local M = {}

local bp = require "bezierpath/bezierpath"

function M.draw(sprite)
	local width = 512
	local height = 512
	local channels = 4

	local handle = bp.create_path()

	-- we have to create table with next fields: buffer, width, height, channels
	local buffer_info = {
		buffer = buffer.create(width * height, {{name = hash("rgba"), type = buffer.VALUE_TYPE_UINT8, count = channels}}),
		width = width,
		height = height,
		channels = channels, -- 3 for rgb, 4 for rgba
		path_handle = handle
	}
	local resource_path = go.get(sprite, "texture0")
	local header = {
		width = width,
		height = height,
		type = resource.TEXTURE_TYPE_2D,
		format = resource.TEXTURE_FORMAT_RGBA,
		num_mip_maps = 1
	}
	bp.init_buffer(buffer_info, bp.rgbaToColor(100, 100, 100, 128)) -- Semi-transparent grey

	bp.rounded_rect(handle, 20, 20, 300, 250, 32)
	bp.fill_path_gradient(
		buffer_info,
		bp.rgbaToColor(150, 150, 150, 255),
		bp.rgbaToColor(150, 150, 150, 255),
		45
	)

	bp.rounded_rect(handle, 100, 60, 300, 250, 32)
	bp.fill_shadow(buffer_info, bp.zero(), 20, bp.black(200))
	bp.fill_path_gradient(
		buffer_info,
		bp.rgbaToColor(255, 0, 0, 200),
		bp.rgbaToColor(0, 0, 255, 200),
		45
	)

	bp.line(handle, bp.point(110, 120), bp.point(390, 120))
	bp.stroke_path(buffer_info, 4, bp.white(200), "center", "round")

	bp.rounded_rect(handle, 240, 240, 200, 55, 16)
	bp.fill_shadow(buffer_info, bp.zero(), 10, bp.rgbaToColor(0, 0, 0, 222))
	bp.fill_path(
		buffer_info,
		bp.rgbaToColor(0, 255, 0, 150)
	)
	bp.stroke_path(
		buffer_info,
		2,
		bp.black(80),
		"center", "round", "round"
	)

	bp.polygon(handle, {
		bp.point(100, 500),
		bp.point(200, 400),
		bp.point(300, 500),
		bp.point(400, 400)
	})

	bp.stroke_path(buffer_info, 20, bp.rgbaToColor(255, 0, 0, 255), "center", "round", "round")

	resource.set_texture(resource_path, header, buffer_info.buffer)

	bp.delete_path(handle)
end

return M