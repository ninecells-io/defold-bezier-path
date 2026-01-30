local M = {}

-- =========================================================
-- Helpers
-- =========================================================

local function clamp(x, lo, hi)
	if x < lo then return lo end
	if x > hi then return hi end
	return x
end

local function round(x)
	return math.floor(x + 0.5)
end

local function normalize_rgb(r, g, b)
	-- Accept either 0..1 or 0..255 (mixed inputs are treated as 0..255).
	-- Heuristic: if all channels are <= 1, assume 0..1.
	if r <= 1 and g <= 1 and b <= 1 then
		return clamp(r * 255, 0, 255),
		       clamp(g * 255, 0, 255),
		       clamp(b * 255, 0, 255)
	end
	return clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255)
end

local function normalize_alpha(a)
	if a == nil then return 1 end
	if type(a) ~= "number" then return 1 end
	-- If someone passes 0..255 by mistake, treat it as that.
	if a > 1 then a = a / 255 end
	return clamp(a, 0, 1)
end

local function get_rgb(c)
	-- Accept {r=,g=,b=} or {r,g,b}
	local r = (c and (c.r or c[1])) or 0
	local g = (c and (c.g or c[2])) or 0
	local b = (c and (c.b or c[3])) or 0
	return normalize_rgb(r, g, b)
end

local function get_rgba(c)
	local r, g, b = get_rgb(c)
	local a = normalize_alpha(c and c.a)
	return r, g, b, a
end

-- =========================================================
-- Color constructors / conversions
-- =========================================================

function M.rgba_to_color(red, green, blue, alpha)
	local r, g, b = normalize_rgb(red or 0, green or 0, blue or 0)
	local a = normalize_alpha(alpha)
	return { r = r, g = g, b = b, a = a }
end

-- Shorthands
function M.rgb(r, g, b)
	return M.rgba_to_color(r, g, b, 1)
end

function M.rgba(r, g, b, a)
	return M.rgba_to_color(r, g, b, a)
end

function M.white(alpha)
    return M.rgba(255, 255, 255, alpha or 1)
end

function M.black(alpha)
    return M.rgba(0, 0, 0, alpha or 1)
end

function M.hex_to_color(hex, alpha)
	-- accept string or number (e.g., 0xffaabb)
	local s = type(hex) == "number" and string.format("%x", hex) or tostring(hex)

	-- strip prefixes like "#", "0x", whitespace
	s = s:gsub("^%s+", ""):gsub("%s+$", "")
	s = s:gsub("^#", ""):gsub("^0[xX]", "")

	local r, g, b, a255

	local function dup(n) return n .. n end

	if #s == 3 then
		-- RGB
		r = tonumber(dup(s:sub(1,1)), 16)
		g = tonumber(dup(s:sub(2,2)), 16)
		b = tonumber(dup(s:sub(3,3)), 16)
	elseif #s == 4 then
		-- RGBA
		r = tonumber(dup(s:sub(1,1)), 16)
		g = tonumber(dup(s:sub(2,2)), 16)
		b = tonumber(dup(s:sub(3,3)), 16)
		a255 = tonumber(dup(s:sub(4,4)), 16)
	elseif #s == 6 or #s == 8 then
		-- RRGGBB or RRGGBBAA
		r = tonumber(s:sub(1,2), 16)
		g = tonumber(s:sub(3,4), 16)
		b = tonumber(s:sub(5,6), 16)
		if #s == 8 then
			a255 = tonumber(s:sub(7,8), 16)
		end
	else
		error("Invalid hex color: " .. tostring(hex))
	end

	local a
	if alpha ~= nil then
		a = normalize_alpha(alpha) -- explicit override 0..1
	elseif a255 ~= nil then
		a = clamp(a255 / 255, 0, 1)
	else
		a = 1
	end

	return M.rgba_to_color(r, g, b, a)
end

function M.color_to_hex(color)
	local r, g, b, a = get_rgba(color or {})
	r, g, b = round(r), round(g), round(b)

	if a >= 1 then
		return string.format("#%02x%02x%02x", r, g, b)
	end

	local ai = round(a * 255)
	return string.format("#%02x%02x%02x%02x", r, g, b, ai)
end

function M.to_vector(color, alpha)
	local r, g, b, a = get_rgba(color or {})
	if alpha ~= nil then a = normalize_alpha(alpha) end
	return vmath.vector4(r / 255, g / 255, b / 255, a)
end

-- Linear interpolation between two colors
function M.lerp_color(c1, c2, t)
	t = clamp(tonumber(t) or 0, 0, 1)

	local r1, g1, b1, a1 = get_rgba(c1 or {})
	local r2, g2, b2, a2 = get_rgba(c2 or {})

	local r = r1 + (r2 - r1) * t
	local g = g1 + (g2 - g1) * t
	local b = b1 + (b2 - b1) * t
	local a = a1 + (a2 - a1) * t

	return M.rgba_to_color(round(r), round(g), round(b), a)
end

-- =========================================================
-- HSL conversion
-- =========================================================

function M.rgb_to_hsl(r, g, b)
	r, g, b = normalize_rgb(r, g, b)
	r, g, b = r / 255, g / 255, b / 255

	local maxc = math.max(r, g, b)
	local minc = math.min(r, g, b)
	local delta = maxc - minc

	local l = (maxc + minc) / 2

	local s
	if delta == 0 then
		s = 0
	else
		s = delta / (1 - math.abs(2 * l - 1))
	end

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

	return h, s, l
end

function M.hsl_to_rgb(h, s, l)
	local r, g, b

	h = (h or 0) % 1
	s = clamp(s or 0, 0, 1)
	l = clamp(l or 0, 0, 1)

	if s == 0 then
		r, g, b = l, l, l
	else
		local function hue_to_rgb(p, q, t)
			if t < 0 then t = t + 1 end
			if t > 1 then t = t - 1 end
			if t < 1/6 then return p + (q - p) * 6 * t end
			if t < 1/2 then return q end
			if t < 2/3 then return p + (q - p) * (2/3 - t) * 6 end
			return p
		end

		local q = (l < 0.5) and (l * (1 + s)) or (l + s - l * s)
		local p = 2 * l - q

		r = hue_to_rgb(p, q, h + 1/3)
		g = hue_to_rgb(p, q, h)
		b = hue_to_rgb(p, q, h - 1/3)
	end

	return round(r * 255), round(g * 255), round(b * 255)
end

function M.modify_color(color, mod_h, mod_s, mod_l, mode)
	local h, s, l = M.rgb_to_hsl(color.r, color.g, color.b)

	local nh, ns, nl
	if mode == "multiply" then
		nh = mod_h and (h * mod_h) or h
		ns = mod_s and (s * mod_s) or s
		nl = mod_l and (l * mod_l) or l
	elseif mode == "addition" then
		nh = mod_h and (h + mod_h) or h
		ns = mod_s and (s + mod_s) or s
		nl = mod_l and (l + mod_l) or l
	else
		nh = mod_h or h
		ns = mod_s or s
		nl = mod_l or l
	end

	local r, g, b = M.hsl_to_rgb(nh, ns, nl)
	return M.rgba_to_color(r, g, b, color.a)
end

-- =========================================================
-- Spread color (lighter/darker by lightness)
-- =========================================================

function M.spread_color(color, factor)
	if type(color) ~= "table" then
		return { darker = color, lighter = color }
	end
	if type(color.r) ~= "number" or type(color.g) ~= "number" or type(color.b) ~= "number" then
		return { darker = color, lighter = color }
	end

	factor = clamp(tonumber(factor) or 0, 0, 1)

	local r, g, b = normalize_rgb(color.r, color.g, color.b)
	local h, s, l = M.rgb_to_hsl(r, g, b)

	local ld = clamp(l - factor, 0, 1)
	local ll = clamp(l + factor, 0, 1)

	local dr, dg, db = M.hsl_to_rgb(h, s, ld)
	local lr, lg, lb = M.hsl_to_rgb(h, s, ll)

	local a = normalize_alpha(color.a)

	return {
		darker  = M.rgba_to_color(dr, dg, db, a),
		lighter = M.rgba_to_color(lr, lg, lb, a),
	}
end

-- =========================================================
-- Contrast helpers (WCAG 2.x)
-- =========================================================

local function srgb_to_linear(u)
	if u <= 0.04045 then return u / 12.92 end
	return ((u + 0.055) / 1.055) ^ 2.4
end

local function relative_luminance(r, g, b)
	local rs, gs, bs = r / 255, g / 255, b / 255
	local rl = srgb_to_linear(rs)
	local gl = srgb_to_linear(gs)
	local bl = srgb_to_linear(bs)
	return 0.2126 * rl + 0.7152 * gl + 0.0722 * bl
end

function M.contrast_ratio(c1, c2)
	local r1, g1, b1 = get_rgb(c1 or {})
	local r2, g2, b2 = get_rgb(c2 or {})

	local L1 = relative_luminance(r1, g1, b1)
	local L2 = relative_luminance(r2, g2, b2)
	if L1 < L2 then L1, L2 = L2, L1 end

	return (L1 + 0.05) / (L2 + 0.05)
end

function M.contrast_passes(ratio, level, large_text)
	level = (level or "AA"):upper()
	large_text = not not large_text

	if level == "AAA" then
		return ratio >= (large_text and 4.5 or 7.0)
	end
	return ratio >= (large_text and 3.0 or 4.5)
end

function M.is_dark(color)
	local white = M.white()
	local black = M.black()
	return M.contrast_ratio(color, white) > M.contrast_ratio(color, black)
end

return M
