#pragma once

#include "buffer.h"

void fill_color(
    const BufferInfo& buffer_info,
    int r, int g, int b, int a
);
void fill_path(
    const BufferInfo& buffer_info,
    int r, int g, int b, int a
);
void fill_path_gradient(
    const BufferInfo& buffer_info,
    int r1, int g1, int b1, int a1,
    int r2, int g2, int b2, int a2,
    float angle_deg
);