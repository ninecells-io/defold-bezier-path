#pragma once

#include "buffer.h"

void fill_shadow(
    const BufferInfo& buffer_info,
    int offset_x, int offset_y,
    int blur_radius,
    int r, int g, int b, int a,
    bool inner // false = outer drop shadow, true = inner shadow
);