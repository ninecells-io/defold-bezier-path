local M = {}

function M.rgba_to_color(red, green, blue, alpha)
	return {
		r = red,
		g = green,
		b = blue,
		a = alpha
	}
end

function M.hex_to_color(hex, alpha)
	-- accept string or number (e.g., 0xffaabb)
	local s = type(hex) == "number" and string.format("%x", hex) or tostring(hex)

	-- strip prefixes like "#", "0x", whitespace
	s = s:gsub("^%s+", ""):gsub("%s+$", "")
	s = s:gsub("^#", ""):gsub("^0[xX]", "")

	local r, g, b, a

	if #s == 3 then
		-- #RGB
		r = tonumber(s:sub(1,1)..s:sub(1,1), 16)
		g = tonumber(s:sub(2,2)..s:sub(2,2), 16)
		b = tonumber(s:sub(3,3)..s:sub(3,3), 16)
	elseif #s == 4 then
		-- #RGBA
		r = tonumber(s:sub(1,1)..s:sub(1,1), 16)
		g = tonumber(s:sub(2,2)..s:sub(2,2), 16)
		b = tonumber(s:sub(3,3)..s:sub(3,3), 16)
		a = tonumber(s:sub(4,4)..s:sub(4,4), 16)
	elseif #s == 6 or #s == 8 then
		-- #RRGGBB or #RRGGBBAA
		r = tonumber(s:sub(1,2), 16)
		g = tonumber(s:sub(3,4), 16)
		b = tonumber(s:sub(5,6), 16)
		if #s == 8 then
			a = tonumber(s:sub(7,8), 16)
		end
	else
		error("Invalid hex color: " .. tostring(hex))
	end

	-- override/ensure alpha (default 1)
	if alpha ~= nil then a = alpha * 255 end
	if a == nil then
		a = 1
	else
		a = a / 255
	end

	return M.rgba_to_color(r, g, b, a)
end

function M:to_vector(color, alpha)
	return vmath.vector4(
		color.r / 255,
		color.g / 255,
		color.b / 255,
		alpha or (color.a)
	)
end

-- Clamp helper
local function clamp(x, lo, hi)
	if x < lo then return lo end
	if x > hi then return hi end
	return x
end

-- RGB (0–1) → HSV (0–1)
function M.rgb_to_hsv(r, g, b)
	local maxc = math.max(r, g, b)
	local minc = math.min(r, g, b)
	local v = maxc
	local delta = maxc - minc

	local s = (maxc == 0) and 0 or (delta / maxc)

	local h
	if delta == 0 then
		h = 0
	elseif maxc == r then
		h = ((g - b) / delta) % 6
	elseif maxc == g then
		h = ((b - r) / delta) + 2
	else
		h = ((r - g) / delta) + 4
	end
	h = (h / 6) % 1
	if h < 0 then h = h + 1 end

	return h, s, v
end

-- HSV (0–1) → RGB (0–1)
function M.hsv_to_rgb(h, s, v)
	if s == 0 then return v, v, v end
	h = (h % 1) * 6
	local i = math.floor(h)
	local f = h - i
	local p = v * (1 - s)
	local q = v * (1 - s * f)
	local t = v * (1 - s * (1 - f))

	if i == 0 then return v, t, p
	elseif i == 1 then return q, v, p
	elseif i == 2 then return p, v, t
	elseif i == 3 then return p, q, v
	elseif i == 4 then return t, p, v
	else return v, p, q end
end

-- Swift's spreadColor in Lua
-- color: table {r, g, b, a} where r/g/b are either 0–1 or 0–255
-- spreadFactor: amount to subtract/add to brightness (0–1)
function M.spread_color(color, spreadFactor)
-- Validate input
	if
		type(color) ~= "table" or
		type(color.r) ~= "number" or
		type(color.g) ~= "number" or
		type(color.b) ~= "number"
	then
		-- Couldn’t decode; mimic Swift's fallback (return the input twice)
		return { darker = color, lighter = color }
	end

	-- Normalize to 0–1 if needed
	local scale = 255
	local r = clamp(color.r / scale, 0, 1)
	local g = clamp(color.g / scale, 0, 1)
	local b = clamp(color.b / scale, 0, 1)

	local h, s, v = M.rgb_to_hsv(r, g, b)
	local vd = clamp(v - spreadFactor, 0, 1)
	local vl = clamp(v + spreadFactor, 0, 1)

	local dr, dg, db = M.hsv_to_rgb(h, s, vd)
	local lr, lg, lb = M.hsv_to_rgb(h, s, vl)

	local a = 1

	-- Return in the same scale as input
	return {
		darker  = {
			r = math.floor(dr * scale),
			g = math.floor(dg * scale),
			b = math.floor(db * scale),
			a = a
		},
		lighter = {
			r = math.floor(lr * scale),
			g = math.floor(lg * scale),
			b = math.floor(lb * scale),
			a = a
		},
	}
end

return M