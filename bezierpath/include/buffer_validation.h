#pragma once

#include <cmath>
#include <climits>
#include <stdint.h>

// Validate before converting Lua numbers to ints or multiplying dimensions.
// Raster byte offsets use signed ints, even though GetBytes returns uint32_t.
inline const char* validate_buffer_layout(double width, double height,
                                         double channels, uint32_t available_bytes) {
    if (!std::isfinite(width) || width < 1 || width > INT_MAX || std::floor(width) != width)
        return "'width' of the buffer must be a positive integer within INT_MAX";
    if (!std::isfinite(height) || height < 1 || height > INT_MAX || std::floor(height) != height)
        return "'height' of the buffer must be a positive integer within INT_MAX";
    if (channels != 3 && channels != 4)
        return "'channels' of the buffer must be 3 or 4";

    const uint64_t row_bytes = uint64_t(width) * uint64_t(channels);
    if (row_bytes > uint64_t(INT_MAX) / uint64_t(height))
        return "Buffer dimensions exceed the renderer's signed byte-offset limit";
    if (row_bytes * uint64_t(height) > available_bytes)
        return "Buffer is smaller than width * height * channels";
    return nullptr;
}
