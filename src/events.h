/**
 * events.h - Stores and provides an easy-to-use interface for the rest of the application
 *  to know when certain user inputs occurred
 */

#pragma once

#include <vector>

#include "common.h"

namespace etsuko {
    namespace events {
        struct Point {
            int32_t raw_x, raw_y;
        };
    }
    class EventManager {
        bool m_quit = false;
        std::vector<events::Point> m_mouse_clicks;
        double m_scrolled = 0.0;

    public:
        [[nodiscard]] bool has_quit() const;

        void loop();

        [[nodiscard]] bool area_was_clicked(const BoundingBox &area, int32_t *x, int32_t *y) const;

        [[nodiscard]] double amount_scrolled() const;
    };
}
