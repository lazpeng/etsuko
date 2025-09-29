#include "events.h"

#include <print>
#include <SDL2/SDL.h>

bool etsuko::EventManager::has_quit() const {
    return m_quit;
}

void etsuko::EventManager::loop() {
    m_mouse_clicks.clear();
    m_scrolled = 0;

    SDL_Event event;
    while ( SDL_PollEvent(&event) ) {
        switch ( event.type ) {
        case SDL_QUIT:
            m_quit = true;
            break;
        case SDL_MOUSEBUTTONDOWN:
            m_mouse_clicks.emplace_back(event.button.x, event.button.y);
            break;
        case SDL_MOUSEWHEEL:
            m_scrolled += event.wheel.y;
            break;
        default:
            break;
        }
    }
}

bool etsuko::EventManager::area_was_clicked(const BoundingBox &area, int32_t *dest_x, int32_t *dest_y) const {
    for ( const auto &[x, y] : m_mouse_clicks ) {
        if ( x >= area.x && x <= area.x + area.w
            && y >= area.y && y <= area.y + area.h ) {
            if ( dest_x != nullptr )
                *dest_x = x;
            if ( dest_y != nullptr )
                *dest_y = y;

            return true;
        }
    }

    return false;
}

double etsuko::EventManager::amount_scrolled() const {
    return m_scrolled;
}
