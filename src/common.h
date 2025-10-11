/**
 * common.h - Defines some common structures and data types that don't belong in a specific module
 */

#pragma once
#include <format>

namespace etsuko {
    using CoordinateType = int32_t;

    struct BoundingBox {
        CoordinateType x, y, w, h;

        [[nodiscard]] bool is_inside_of(const CoordinateType trg_x, const CoordinateType trg_y) const {
            return trg_x >= x && trg_x <= x + w && trg_y >= y && trg_y <= y + h;
        }
    };

    constexpr auto VERSION_STRING = "0.3.5";
}
