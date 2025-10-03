/**
 * events.h - Stores and provides an easy-to-use interface for the rest of the application
 *  to know when certain user inputs occurred
 */

#pragma once

#include <vector>

#include <SDL2/SDL_events.h>

#include "common.h"

namespace etsuko {
    namespace events {
        struct Point {
            int32_t raw_x, raw_y;
        };

        struct Key {
            enum Code {
                NONE = 0,
                SPACE,
                LEFT_ARROW,
                RIGHT_ARROW
            };
        };
    }
    class EventManager {
        bool m_quit = false;
        std::vector<events::Point> m_mouse_clicks;
        std::vector<events::Key::Code> m_keys_down;
        double m_scrolled = 0.0;

        void handle_key(const SDL_Event &event);

    public:
        [[nodiscard]] bool has_quit() const;

        void loop();

        [[nodiscard]] bool area_was_clicked(const BoundingBox &area, int32_t *destination_x, int32_t *destination_y) const;

        [[nodiscard]] double amount_scrolled() const;

        [[nodiscard]] bool is_key_down(events::Key::Code key) const;
    };
}
