local M = {}

-- Clamp helper
local function clamp(x, lo, hi)
	if x < lo then return lo end
	if x > hi then return hi end
	return x
end

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

function M.color_to_hex(color)
	-- color.r/g/b: 0..255, color.a: 0..1
	local r = math.floor(color.r or 0)
	local g = math.floor(color.g or 0)
	local b = math.floor(color.b or 0)
	local a = color.a

	-- clamp values to valid ranges
	r = clamp(r, 0, 255)
	g = clamp(g, 0, 255)
	b = clamp(b, 0, 255)
	if a == nil then a = 1 end
	if type(a) ~= "number" then a = 1 end
	a = clamp(a, 0, 1)

	if a >= 1 then
		return string.format("#%02x%02x%02x", r, g, b)
	else
		local ai = math.floor(a * 255 + 0.5)
		return string.format("#%02x%02x%02x%02x", r, g, b, ai)
	end
end

function M.to_vector(color, alpha)
	return vmath.vector4(
		color.r / 255,
		color.g / 255,
		color.b / 255,
		alpha or (color.a)
	)
end

-- RGB (0–1) → HSV (0–1)
function M.rgb_to_hsv(r, g, b)
	r = r / 255
	g = g / 255
	b = b / 255

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
	if s == 0 then
		v = clamp(math.floor(v * 255 + 0.5), 0, 255)
		return v, v, v
	end
	h = (h % 1) * 6
	local i = math.floor(h)
	local f = h - i
	local p = v * (1 - s)
	local q = v * (1 - s * f)
	local t = v * (1 - s * (1 - f))

	v = clamp(math.floor(v * 255 + 0.5), 0, 255)
	t = clamp(math.floor(t * 255 + 0.5), 0, 255)
	p = clamp(math.floor(p * 255 + 0.5), 0, 255)
	q = clamp(math.floor(q * 255 + 0.5), 0, 255)

	if i == 0 then return v, t, p
	elseif i == 1 then return q, v, p
	elseif i == 2 then return p, v, t
	elseif i == 3 then return p, q, v
	elseif i == 4 then return t, p, v
	else return v, p, q end
end

function M.modify_color(color, mod_h, mod_s, mod_v, mode)
	local h, s, v = M.rgb_to_hsv(color.r, color.g, color.b)
	local r, g, b
	if mode == "multiply" then
		r, g, b = M.hsv_to_rgb(
			mod_h and h * mod_h or h,
			mod_s and s * mod_s or s,
			mod_v and v * mod_v or v
		)
	elseif mode == "addition" then
		r, g, b = M.hsv_to_rgb(
			mod_h and h + mod_h or h,
			mod_s and s + mod_s or s,
			mod_v and v + mod_v or v
		)
	else
		r, g, b = M.hsv_to_rgb(mod_h or h, mod_s or s, mod_v or v)
	end
	return M.rgba_to_color(r, g, b, color.a)
end

-- Swift's spreadColor in Lua
-- color: table {r, g, b, a} where r/g/b are either 0–1 or 0–255
-- spreadFactor: amount to subtract/add to brightness (0–1)
function M.spread_color(color, factor)
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

	-- Use 0..255 inputs consistently: `rgb_to_hsv` expects 0..255 values
	local r = clamp(color.r, 0, 255)
	local g = clamp(color.g, 0, 255)
	local b = clamp(color.b, 0, 255)

	local h, s, v = M.rgb_to_hsv(r, g, b)
	local vd = clamp(v - factor, 0, 1)
	local vl = clamp(v + factor, 0, 1)

	local dr, dg, db = M.hsv_to_rgb(h, s, vd)
	local lr, lg, lb = M.hsv_to_rgb(h, s, vl)

	local a = color.a or 1

	-- `hsv_to_rgb` returns 0..255 values here; round and return those
	return {
		darker = {
			r = math.floor(dr + 0.5),
			g = math.floor(dg + 0.5),
			b = math.floor(db + 0.5),
			a = a
		},
		lighter = {
			r = math.floor(lr + 0.5),
			g = math.floor(lg + 0.5),
			b = math.floor(lb + 0.5),
			a = a
		},
	}
end

function M.is_dark(color, g, b)

	-- Accept either a color table (`{r,g,b}` or `{r=..,g=..,b=..}`) or three numeric args.
	local r
	if type(color) == "table" then
		r = color.r or color[1] or 0
		g = color.g or color[2] or 0
		b = color.b or color[3] or 0
	else
		r = color or 0
		g = g or 0
		b = b or 0
	end

	-- Ensure values are in 0..255 range; alpha is deliberately ignored
	r = clamp(r, 0, 255)
	g = clamp(g, 0, 255)
	b = clamp(b, 0, 255)

	-- Perceived luminance (BT.601): 0.299 R + 0.587 G + 0.114 B
	local luminance = 0.299 * r + 0.587 * g + 0.114 * b

	-- Consider dark if luminance is below midpoint (128)
	return luminance < 128
end

return M