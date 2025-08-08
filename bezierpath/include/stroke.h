#pragma once

enum StrokePosition { CENTER, INSIDE, OUTSIDE };
enum CapStyle { CAP_BUTT = 0, CAP_ROUND, CAP_SQUARE };
enum JoinStyle { JOIN_MITER = 0, JOIN_ROUND, JOIN_BEVEL };

void stroke_path(float width, int position_mode, int r, int g, int b, int a, int cap_style, int join_style);