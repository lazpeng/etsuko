#include "events.h"

#include <algorithm>
#include <print>
#include <SDL2/SDL.h>

bool etsuko::EventManager::has_quit() const {
    return m_quit;
}

void etsuko::EventManager::handle_key(const SDL_Event &event) {
    events::Key::Code code = events::Key::NONE;
    switch ( event.key.keysym.sym ) {
    case SDLK_SPACE:
        code = events::Key::SPACE;
        break;
    case SDLK_LEFT:
        code = events::Key::LEFT_ARROW;
        break;
    case SDLK_RIGHT:
        code = events::Key::RIGHT_ARROW;
        break;
    default:
        break;
    }

    m_keys_down.push_back(code);
}

bool etsuko::EventManager::is_key_down(const events::Key::Code key) const {
    return std::ranges::any_of(m_keys_down, [key](const auto k) {
        return k == key;
    });
}

void etsuko::EventManager::loop() {
    m_mouse_clicks.clear();
    m_keys_down.clear();
    m_scrolled = 0;
    m_window_resized = false;

    SDL_Event event;
    while ( SDL_PollEvent(&event) ) {
        switch ( event.type ) {
        case SDL_QUIT:
            m_quit = true;
            break;
        case SDL_MOUSEBUTTONDOWN:
            m_mouse_clicks.emplace_back(event.button.x, event.button.y);
            break;
        case SDL_MOUSEMOTION:
            m_mouse_x = event.motion.x;
            m_mouse_y = event.motion.y;
            break;
        case SDL_MOUSEWHEEL:
            m_scrolled += event.wheel.y;
            break;
        case SDL_KEYDOWN:
            handle_key(event);
            break;
        case SDL_WINDOWEVENT:
            if ( event.window.event == SDL_WINDOWEVENT_RESIZED ) {
                m_window_resized = true;
            }
            break;
        default:
            break;
        }
    }
}

bool etsuko::EventManager::window_was_resized() const {
    return m_window_resized;
}

bool etsuko::EventManager::area_was_clicked(const BoundingBox &area, int32_t *destination_x, int32_t *destination_y) const {
    for ( const auto &[x, y] : m_mouse_clicks ) {
        if ( x >= area.x && x <= area.x + area.w
            && y >= area.y && y <= area.y + area.h ) {
            if ( destination_x != nullptr )
                *destination_x = x;
            if ( destination_y != nullptr )
                *destination_y = y;

            return true;
        }
    }

    return false;
}

void etsuko::EventManager::get_mouse_position(int32_t *x, int32_t *y) const {
    if ( x != nullptr ) {
        *x = m_mouse_x;
    }

    if ( y != nullptr ) {
        *y = m_mouse_y;
    }
}

double etsuko::EventManager::amount_scrolled() const {
    return m_scrolled;
}
