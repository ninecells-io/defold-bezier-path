-- Drawing examples are independent of navigation and GUI presentation.
local bp = require "bezierpath.bezierpath"
local M = {}
local mint = { r = 143, g = 232, b = 167, a = 1 }
local orange = { r = 248, g = 168, b = 92, a = 1 }
local blue = { r = 92, g = 173, b = 240, a = 1 }
local white = { r = 237, g = 235, b = 223, a = 1 }
local dark = { r = 24, g = 31, b = 36, a = 1 }
local function alpha(c, a) return { r = c.r, g = c.g, b = c.b, a = a } end
local function point(x, y, s) return bp.point(x * s, y * s) end
local function polygon(h, coords, s, closed)
    local p = {}
    for _, v in ipairs(coords) do p[#p + 1] = point(v[1], v[2], s) end
    bp.polygon(h, p, closed)
end
local function shape(h, kind, s)
    if kind == "rectangle" then bp.rounded_rect(h, 38*s, 18*s, 184*s, 76*s, 0)
    elseif kind == "rounded" then bp.rounded_rect(h, 38*s, 18*s, 184*s, 76*s, 20*s)
    elseif kind == "circle" then bp.circle(h, 92*s, 18*s, 38*s, 38*s)
    elseif kind == "ellipse" then bp.circle(h, 38*s, 20*s, 92*s, 36*s)
    elseif kind == "pill" then bp.pill(h, 30*s, 24*s, 200*s, 64*s)
    elseif kind == "polygon" then polygon(h, {{60,20},{130,8},{212,52},{170,102},{70,88}}, s, true)
    elseif kind == "curve" then
        bp.reset_path(h)
        bp.add_point(h, point(30,28,s))
        bp.add_quad_curve_point(h, point(230,28,s), point(130,160,s))
        bp.set_path_closed(h, false)
    elseif kind == "arc" then
        bp.reset_path(h)
        bp.add_arc(h, point(130,56,s), 42*s, math.pi*0.15, math.pi*1.85, false)
        bp.set_path_closed(h, false)
    else polygon(h, {{30,25},{83,88},{136,25},{189,88},{230,25}}, s, false) end
end
local function sample(title, description, draw, options)
    local entry = options or {}
    entry.title, entry.description = title, description
    entry.draw = function(b, _, params)
        local s = b.width / 260
        bp.with_path(b, function(h) draw(b, h, s, params) end)
    end
    return entry
end
local function filled(kind, color)
    return function(b,h,s)
        shape(h,kind,s)
        if kind == "curve" or kind == "arc" or kind == "open" then
            bp.stroke_path(b,5*s,color,"center","round","round")
        else bp.fill_path(b,color) end
    end
end
local function stroke(position, cap, join)
    return function(b,h,s)
        shape(h, join and "open" or "rounded", s)
        bp.stroke_path(b, 13*s, mint, position or "center", cap or "butt", join or "miter")
        -- Only position samples need a guide marking the original contour.
        if not join then
            bp.stroke_path(b, s, alpha(white,0.6), "center", "butt", "miter")
        end
    end
end
local function cap(style)
    return function(b,h,s)
        bp.line(h,point(45,56,s),point(215,56,s))
        bp.stroke_path(b,32*s,blue,"center",style,"miter")
        for _, x in ipairs({45,215}) do
            bp.line(h,point(x,26,s),point(x,86,s))
            bp.stroke_path(b,s,orange,"center")
        end
    end
end
local function gradient(shape_gradient, angle)
    return function(b,h,s)
        shape(h,"pill",s)
        if shape_gradient then bp.fill_path_gradient_shape(b,mint,blue)
        else bp.fill_path_gradient(b,mint,blue,angle or 0) end
    end
end
local function shadow(inner, offset, radius)
    return function(b,h,s)
        shape(h,"rounded",s)
        if not inner then bp.fill_shadow(b,point(offset,offset,s),radius*s,false,alpha(blue,0.8)) end
        bp.fill_path(b,mint)
        if inner then bp.fill_shadow(b,point(offset,offset,s),radius*s,true,alpha(dark,0.95)) end
    end
end
local function masked(inverted)
    return function(b,h,s)
        bp.rounded_rect(h,12*s,6*s,236*s,100*s,0)
        bp.fill_path_gradient(b,orange,blue,25)
        for x=0,260,24 do
            bp.line(h,point(x,0,s),point(x+50,112,s))
            bp.stroke_path(b,8*s,alpha(white,0.35),"center")
        end
        shape(h,"pill",s)
        bp.mask_path(b,not inverted)
    end
end
M.pages = {
    { id="shapes", title="Shapes & paths", subtitle="A small vocabulary. Endless combinations.", entries={
        sample("01 / Rectangle","Four edges, one closed path",filled("rectangle",mint)),
        sample("02 / Rounded rectangle","Quadratic corners",filled("rounded",blue)),
        sample("03 / Circle","A full circular arc",filled("circle",orange)),
        sample("04 / Ellipse","Independent horizontal / vertical radii",filled("ellipse",blue)),
        sample("05 / Pill","Straight edges meet semicircles",filled("pill",orange)),
        sample("06 / Polygon","Ordered points, closed contour",filled("polygon",mint)),
        sample("07 / Quadratic curve","One control point bends the segment",filled("curve",orange)),
        sample("08 / Arc","Center, radius, start and end angles",filled("arc",mint)),
        sample("09 / Open path","Connected segments without closure",filled("open",blue)),
    }},
    { id="fills", title="Color & gradients", subtitle="Solid surfaces, soft transitions, layered transparency.", entries={
        sample("Solid color","RGB 0-255 / alpha 0-1",filled("rounded",mint)),
        sample("Transparency","Two translucent paths overlap",function(b,h,s)
            bp.circle(h,45*s,17*s,40*s,40*s); bp.fill_path(b,alpha(blue,0.7))
            bp.circle(h,105*s,17*s,40*s,40*s); bp.fill_path(b,alpha(orange,0.7))
        end),
        sample("Shape gradient","Color follows the distance to the edge",gradient(true)),
        sample("Linear / 0 degrees","Horizontal transition",gradient(false,0)),
        sample("Linear / 45 degrees","Diagonal transition",gradient(false,45)),
        sample("Linear / 90 degrees","Vertical transition",gradient(false,90)),
    }},
    { id="strokes", title="Strokes & glow", subtitle="The same path, drawn with a different character.", entries={
        sample("Position / inside","Hairline marks the original contour",stroke("inside")),
        sample("Position / center","Half of the width on either side",stroke("center")),
        sample("Position / outside","The fill area stays unobstructed",stroke("outside")),
        sample("Cap / butt","Orange marks the segment endpoints",cap("butt")),
        sample("Cap / round","Semicircular ends",cap("round")),
        sample("Cap / square","Ends extend by half the stroke width",cap("square")),
        sample("Join / miter + bevel","Sharp and clipped corners",function(b,h,s)
            polygon(h,{{30,26},{75,86},{120,26}},s,false);bp.stroke_path(b,12*s,mint,"center","butt","miter")
            polygon(h,{{145,26},{190,86},{235,26}},s,false);bp.stroke_path(b,12*s,orange,"center","butt","bevel")
        end),
        sample("Join / round","Rounded transitions between segments",stroke("center","round","round")),
        sample("Glow","Blurred stroke underneath a crisp contour",function(b,h,s)
            shape(h,"rounded",s);bp.stroke_path_glow(b,4*s,12*s,"center",blue)
            bp.stroke_path(b,2*s,mint,"center","round","round")
        end),
    }},
    { id="shadows", title="Light & shadow", subtitle="Depth from offset, blur radius, and an inner or outer mask.", entries={
        sample("Outer / soft","6 px offset, 12 px blur",shadow(false,6,12)),
        sample("Outer / close","2 px offset, 5 px blur",shadow(false,2,5)),
        sample("Outer / ambient","Zero offset, 16 px blur",shadow(false,0,16)),
        sample("Inner / recessed","6 px offset, 12 px blur",shadow(true,6,12)),
        sample("Inner / opposite light","Negative offset reverses direction",shadow(true,-6,9)),
        sample("Inner / ambient","Zero offset, 14 px blur",shadow(true,0,14)),
    }},
    { id="masks", title="Alpha masks", subtitle="Draw the artwork first. Then keep or remove the path interior.", entries={
        sample("Source artwork","Gradient with translucent stripes",function(b,h,s)
            bp.rounded_rect(h,12*s,6*s,236*s,100*s,0);bp.fill_path_gradient(b,orange,blue,25)
            for x=0,260,24 do bp.line(h,point(x,0,s),point(x+50,112,s));bp.stroke_path(b,8*s,alpha(white,0.35),"center") end
        end),
        sample("Normal mask","Keep pixels inside the pill",masked(false)),
        sample("Inverted mask","Keep pixels outside the pill",masked(true)),
        sample("Layered composition","A masked layer over a solid surface",function(b,h,s)
            shape(h,"rounded",s);bp.fill_path_gradient(b,mint,blue,45)
            bp.circle(h,94*s,20*s,36*s,36*s);bp.mask_path(b,true)
        end),
    }},
    { id="textures", title="From paths to textures", subtitle="Use the pixels in a GUI, on a sprite, or in a tightly cropped region.", entries={
        sample("Sprite texture","resource.set_texture / actual game object",gradient(true),{sprite=true}),
        sample("GUI box","add_gui_box returns node + texture id",gradient(true),{box=true}),
        sample("Raw RGBA bytes","draw_texture + set_node_texture",gradient(false,45),{bytes=true}),
        sample("Sampling / 1x","Native antialiasing",filled("curve",mint),{factor=1}),
        sample("Sampling / 2x","Scaled geometry, downsampled by the GPU",filled("curve",mint),{factor=2}),
        sample("Cropped region","Board coordinates mapped to a small buffer",filled("rounded",orange),{region=true}),
    }},
    { id="lifecycle", title="Resources & caching", subtitle="Own your paths. Reuse textures. Keep persistence an explicit choice.", entries={
        sample("Scoped path","with_path releases handles after drawing",filled("rounded",mint)),
        sample("Bounds + padding","The outline shows get_path_bounds(h, 6)",function(b,h,s)
            shape(h,"pill",s);bp.fill_path(b,blue)
            local bounds=bp.get_path_bounds(h,6*s)
            bp.rounded_rect(h,bounds.x,bounds.y,bounds.width,bounds.height,0)
            bp.stroke_path(b,s,orange,"center")
        end),
        sample("Explicit ownership","create_path / reset_path / delete_path",function(b,_,s)
            local h=bp.create_path(b)
            shape(h,"rounded",s);bp.fill_path(b,orange)
            bp.reset_path(h);shape(h,"circle",s);bp.stroke_path(b,3*s,white,"center")
            bp.delete_path(h)
        end),
        sample("Same-scene reuse","Two nodes share one opaque texture id",gradient(true),{reuse=true}),
        sample("Persistent cache","Enable below; rebuild to test disk hits",gradient(false,45)),
        sample("Texture generations","Switching pages releases the previous set",filled("open",mint)),
    }},
}
return M
