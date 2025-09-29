/**
 * common.h - Defines some common structures and data types that don't belong in a specific module
 */

#pragma once
#include <cstdint>

namespace etsuko {
    struct BoundingBox {
        int32_t x, y, w, h;
    };

    constexpr auto VERSION_STRING = "0.1.0";
}
