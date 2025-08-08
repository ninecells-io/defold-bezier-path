#include <dmsdk/sdk.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>
#include <stack>
#include "buffer.h"
#include "utils.h"

void flatten_path(
    std::vector<std::pair<float, float>>& points,
    int curve_segments,
    std::vector<uint8_t>* corner_flags_out
) {
    points.clear();
    if (corner_flags_out) corner_flags_out->clear();
    if (g_path_points.empty()) return;

    for (size_t i = 0; i < g_path_points.size(); ++i)
    {
        const PathPoint& p0 = g_path_points[i];
        const PathPoint& p1 = g_path_points[(i + 1) % g_path_points.size()];

        if (p1.type == POINT_TYPE_LINEAR)
        {
            points.emplace_back(p0.x, p0.y);
            if (corner_flags_out) corner_flags_out->push_back(1); // original vertex
        }
        else if (p1.type == POINT_TYPE_BEZIER)
        {
            for (int j = 0; j <= curve_segments; ++j)
            {
                float t = (float)j / curve_segments;
                float mt = 1.0f - t;

                float x = mt * mt * p0.x + 2 * mt * t * p1.cx + t * t * p1.x;
                float y = mt * mt * p0.y + 2 * mt * t * p1.cy + t * t * p1.y;

                points.emplace_back(x, y);

                if (corner_flags_out)
                {
                    // mark only the ends of the Bezier as corners
                    corner_flags_out->push_back((j == 0 || j == curve_segments) ? 1 : 0);
                }
            }
        }
    }
}

bool point_in_polygon(float x, float y, const std::vector<std::pair<float, float>>& poly)
{
    bool inside = false;
    size_t n = poly.size();

    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        float xi = poly[i].first, yi = poly[i].second;
        float xj = poly[j].first, yj = poly[j].second;

        bool intersect = ((yi > y) != (yj > y)) &&
                         (x < (xj - xi) * (y - yi) / (yj - yi + 1e-6f) + xi);
        if (intersect)
            inside = !inside;
    }

    return inside;
}