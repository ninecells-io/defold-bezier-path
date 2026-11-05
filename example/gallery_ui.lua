-- Shared presentation primitives; all coordinates use the 1280 x 800 canvas.
local M = {}
M.ink = vmath.vector4(0.94, 0.93, 0.88, 1)
M.muted = vmath.vector4(0.52, 0.59, 0.62, 1)
M.accent = vmath.vector4(0.56, 0.91, 0.65, 1)
M.panel = vmath.vector4(0.085, 0.106, 0.12, 1)
function M.box(parent, x, y, w, h, color)
    local n = gui.new_box_node(vmath.vector3(x, y, 0), vmath.vector3(w, h, 0))
    gui.set_parent(n, parent, false)
    gui.set_color(n, color or M.panel)
    gui.set_size_mode(n, gui.SIZE_MODE_MANUAL)
    return n
end
function M.text(parent, x, y, text, size, color)
    local n = gui.new_text_node(vmath.vector3(x, y, 0), text)
    gui.set_parent(n, parent, false)
    gui.set_font(n, "default")
    gui.set_pivot(n, gui.PIVOT_W)
    gui.set_scale(n, vmath.vector3(size / 16, size / 16, 1))
    gui.set_color(n, color or M.ink)
    return n
end
function M.caption(parent, x, y, text)
    local n = M.text(parent, x, y, text, 10, M.muted)
    gui.set_line_break(n, true)
    gui.set_size(n, vmath.vector3(416, 32, 0))
    return n
end
function M.button(parent, x, y, w, caption, selected)
    local n = M.box(parent, x, y, w, 46, selected and vmath.vector4(0.15, 0.24, 0.20, 1) or M.panel)
    M.text(n, -w / 2 + 18, 0, caption, 14, selected and M.accent or M.ink)
    return n
end
return M
