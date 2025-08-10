#pragma once

#include "buffer.h"

enum StrokePosition { CENTER = 0, INSIDE, OUTSIDE };
enum CapStyle { CAP_BUTT = 0, CAP_ROUND, CAP_SQUARE };
enum JoinStyle { JOIN_MITER = 0, JOIN_ROUND, JOIN_BEVEL };

void stroke_path(
    const BufferInfo& buffer_info,
    float width, int position_mode,
    int r, int g, int b, int a,
    int cap_style, int join_style
);
void stroke_path_glow(
    const BufferInfo& buffer_info,
    float width, float radius, int position_mode,
    int r, int g, int b, int a
);