#include "renderer.h"
#include "parser.h"

#include <iostream>

using namespace etsuko::renderer;

constexpr auto TILING_PARTS = 20;

template void BakedDrawableScrollingContainer<etsuko::parser::TimedLyric>::draw(const Renderer &renderer);

template <typename T>
void BakedDrawableScrollingContainer<T>::draw(const Renderer &renderer) {
    CoordinateType y = m_opts.margin_top - m_viewport.y;
    for ( auto &drawable : m_drawables ) {
        if ( !drawable->is_enabled() )
            continue;

        if ( y + drawable->bounds().h >= m_bounds.y + m_bounds.h )
            break;

        CoordinateType x;
        if ( m_opts.alignment == ScrollingContainerOpts::ALIGN_LEFT ) {
            x = 0;
        } else if ( m_opts.alignment == ScrollingContainerOpts::ALIGN_CENTER ) {
            x = m_bounds.w / 2 - drawable->bounds().w / 2;
        } else if ( m_opts.alignment == ScrollingContainerOpts::ALIGN_RIGHT ) {
            x = m_bounds.w - drawable->bounds().w;
        } else
            throw std::runtime_error("Invalid alignment option");

        drawable->set_bounds({.x = x, .y = y, .w = drawable->bounds().w, .h = drawable->bounds().h});

        if ( y + drawable->bounds().h >= 0 ) {
            renderer.render_baked(*drawable, *this);
        }

        y += drawable->bounds().h + m_opts.vertical_padding;
    }
}

void etsuko::Renderer::measure_line_size(const std::string &text, const int pt, int32_t *w, int32_t *h) const {
    if ( TTF_SetFontSizeDPI(m_font, pt, m_h_dpi, m_v_dpi) != 0 ) {
        throw std::runtime_error("Failed to set font size/DPI");
    }

    if ( TTF_SizeUTF8(m_font, text.c_str(), w, h) != 0 ) {
        throw std::runtime_error("Failed to measure line size");
    }
}

int etsuko::Renderer::initialize() {
    if ( SDL_Init(SDL_INIT_VIDEO) != 0 ) {
        return -1;
    }

    if ( TTF_Init() != 0 ) {
        return -1;
    }

    constexpr auto pos = SDL_WINDOWPOS_CENTERED;
    constexpr auto flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    m_window = SDL_CreateWindow(DEFAULT_TITLE, pos, pos, DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if ( m_window == nullptr ) {
        return -2;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    constexpr auto renderer_flags = SDL_RENDERER_ACCELERATED;
    m_renderer = SDL_CreateRenderer(m_window, -1, renderer_flags);
    if ( m_renderer == nullptr ) {
        return -3;
    }
    int outW, outH;
    SDL_GetRendererOutputSize(m_renderer, &outW, &outH);
    SDL_RenderSetLogicalSize(m_renderer, outW, outH);

    m_viewport = {.x = 0, .y = 0, .w = outW, .h = outH};

    /* This value should be recalculated when the screen changes size(?) */
    float hdpi_temp, v_dpi_temp;
    if ( SDL_GetDisplayDPI(0, nullptr, &hdpi_temp, &v_dpi_temp) != 0 ) {
        std::puts("Could not get DPI");
        std::puts(SDL_GetError());
        return -4;
    }

    m_h_dpi = static_cast<int32_t>(hdpi_temp), m_v_dpi = static_cast<int32_t>(v_dpi_temp);

    return 0;
}

void etsuko::Renderer::begin_loop() const {
    Color color = {.r = 17, .g = 24, .b = 39, .a = 255};
    auto [r,g,b,a] = color;
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_RenderClear(m_renderer);
}

void etsuko::Renderer::end_loop() const {
    SDL_RenderPresent(m_renderer);
}

void etsuko::Renderer::finalize() {
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);

    m_renderer = nullptr;
    m_window = nullptr;
}

size_t etsuko::Renderer::measure_text_wrap_stop(const TextOpts &text_opts, ContainerLike const &container, const size_t start) const {
    const auto &text = text_opts.text;

    const auto m_current_width = container.get_bounds().w;
    const auto calculated_max_width = static_cast<int32_t>(m_current_width * text_opts.layout_opts.wrap_opts.wrap_width_threshold);

    size_t end_idx = start + 1;
    while ( true ) {
        auto tmp_end_idx = text.find(' ', end_idx + 1);
        if ( tmp_end_idx == std::string::npos ) {
            tmp_end_idx = text.size();
        }

        if ( start == tmp_end_idx )
            break;

        auto measure_pt_size = text_opts.layout_opts.wrap_opts.measure_at_pts;
        if ( measure_pt_size <= 0 ) {
            measure_pt_size = text_opts.pt_size;
        }
        int32_t w, h;
        measure_line_size(text.substr(start, tmp_end_idx - start), measure_pt_size, &w, &h);

        if ( w > calculated_max_width ) {
            // go with whatever was on previously
            if ( end_idx != start ) {
                // do nothing, draw using last good value
                break;
            }
            end_idx = tmp_end_idx; // Go with what we have now, even if it surpasses the threshold,
            // hopefully it doesn't blow it by much
        } else {
            const auto backup_end_idx = end_idx;
            end_idx = tmp_end_idx;
            if ( backup_end_idx == end_idx )
                break;
        }
    }

    return end_idx;
}

void etsuko::Renderer::measure_layout(const BoundingBox &box, const Point &position, ContainerLike const &container, BoundingBox &out_box) {
    const auto w = box.w, h = box.h;
    const auto current_width = container.get_bounds().w, current_height = container.get_bounds().h;

    int32_t calc_w = 0, calc_h = 0;

    if ( position.flags & Point::ANCHOR_RIGHT_X ) {
        calc_w = w;
    } else if ( position.flags & Point::ANCHOR_CENTER_X ) {
        calc_w = w / 2;
    }

    if ( position.flags & Point::ANCHOR_BOTTOM_Y ) {
        calc_h = h;
    } else if ( position.flags & Point::ANCHOR_CENTER_Y ) {
        calc_h = h / 2;
    }

    CoordinateType x = position.x, y = position.y;

    // Calculate x
    if ( position.flags & Point::TILED_X ) {
        x *= current_width / TILING_PARTS;
    }

    if ( position.flags & Point::CENTERED_X ) {
        x = static_cast<CoordinateType>(current_width / 2.0 - w / 2.0 - calc_w) + x;
    } else if ( x < 0 ) {
        x = current_width + x - calc_w;
    }

    // Calculate y
    if ( position.flags & Point::TILED_Y ) {
        y *= current_height / TILING_PARTS;
    }

    if ( position.flags & Point::CENTERED_Y ) {
        y = static_cast<CoordinateType>(current_height / 2.0 - h / 2.0 - calc_h) + y;
    } else if ( y < 0 ) {
        y = current_height + y - calc_h;
    }

    out_box.x = x;
    out_box.y = y;
    out_box.w = w;
    out_box.h = h;
}

void etsuko::Renderer::measure_layout_texture(SDL_Texture *texture, const Point &position, ContainerLike const &container, BoundingBox &out_box) {
    int32_t w, h;
    SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);

    const BoundingBox box = {.w = w, .h = h};
    measure_layout(box, position, container, out_box);
}

SDL_Texture *etsuko::Renderer::draw_text(const std::string_view &text, int32_t pt_size, bool bold, Color color) const {
    if ( m_font == nullptr ) {
        throw std::runtime_error("Font not loaded");
    }
    if ( text.empty() ) {
        throw std::runtime_error("Text is empty");
    }

    if ( TTF_SetFontSizeDPI(m_font, pt_size, m_h_dpi, m_v_dpi) != 0 ) {
        std::puts(TTF_GetError());
        throw std::runtime_error("Failed to set font size/DPI");
    }
    const auto style = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
    TTF_SetFontStyle(m_font, style);

    const auto sdl_color = SDL_Color{color.r, color.g, color.b, color.a};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(m_font, text.data(), sdl_color);
    if ( surface == nullptr ) {
        std::puts(TTF_GetError());
        throw std::runtime_error("Failed to render to surface");
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_FreeSurface(surface);
    if ( texture == nullptr ) {
        std::puts(SDL_GetError());
        throw std::runtime_error("Failed to create texture");
    }

    return texture;
}

std::shared_ptr<BakedDrawable> etsuko::Renderer::draw_text_baked(const TextOpts &opts, const ContainerLike &container) {
    SDL_Texture *final_texture = nullptr;

    if ( opts.layout_opts.wrap && measure_text_wrap_stop(opts, container) < opts.text.size() - 1 ) {
        size_t start = 0;
        const auto full_text_size = opts.text.size();

        std::vector<SDL_Texture *> textures;
        int32_t max_w = 0, total_h = 0;

        do {
            const size_t end = measure_text_wrap_stop(opts, container, start);
            const auto line = opts.text.substr(start, end - start);
            const auto texture = draw_text(line, opts.pt_size, opts.bold, opts.color);
            textures.push_back(texture);

            int32_t w, h;
            SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
            max_w = std::max(max_w, w);
            if ( total_h != 0 ) {
                total_h += opts.layout_opts.wrap_opts.line_padding;
            }
            total_h += h;

            start = end + 1;
        } while ( start < full_text_size - 1 );

        final_texture = make_new_texture_target(max_w, total_h);

        CoordinateType x, y = 0;
        for ( const auto texture : textures ) {
            int32_t w, h;
            SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
            if ( opts.layout_opts.wrap_opts.alignment_mode == TextLayoutOpts::WrapOpts::WRAP_ALIGN_LEFT ) {
                x = 0;
            } else if ( opts.layout_opts.wrap_opts.alignment_mode == TextLayoutOpts::WrapOpts::WRAP_ALIGN_RIGHT ) {
                x = max_w - w;
            } else if ( opts.layout_opts.wrap_opts.alignment_mode == TextLayoutOpts::WrapOpts::WRAP_ALIGN_CENTER ) {
                x = static_cast<CoordinateType>(max_w / 2.0 - w / 2.0);
            } else {
                throw std::runtime_error("Invalid alignment mode");
            }

            SDL_Rect destination = {.x = x, .y = y, .w = w, .h = h};
            SDL_RenderCopy(m_renderer, texture, nullptr, &destination);
            SDL_DestroyTexture(texture);

            y += h + opts.layout_opts.wrap_opts.line_padding;
        }

        restore_texture_target();
    } else {
        final_texture = draw_text(opts.text, opts.pt_size, opts.bold, opts.color);
    }

    // Initialize baked drawable
    auto baked = std::make_shared<BakedDrawable>();
    baked->set_texture(final_texture);
    measure_layout_texture(final_texture, opts.position, container, baked->m_bounds);
    baked->set_valid(true);
    return baked;
}

void etsuko::Renderer::render_baked(const BakedDrawable &baked, const ContainerLike &container) const {
    if ( !baked.is_valid() ) {
        throw std::runtime_error("Attempted to draw uninitialized baked drawable");
    }

    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
    const BoundingBox &pivot = container.get_bounds();
    const SDL_Rect rect = {.x = pivot.x + baked.m_bounds.x, .y = pivot.y + baked.m_bounds.y, .w = baked.m_bounds.w, .h = baked.m_bounds.h};
    SDL_RenderCopy(m_renderer, baked.m_texture, nullptr, &rect);
}

void etsuko::Renderer::render_baked(const BakedDrawable &baked) const {
    render_baked(baked, root_container());
}

void etsuko::Renderer::draw_horiz_progress_simple(const ProgressBarOpts &options, BoundingBox *out_box) const {
    BoundingBox temp = {.w = 1, .h = options.thickness};
    measure_layout(temp, {.x = options.end_x, .y = 0, .flags = options.position.flags}, root_container(), temp);
    BoundingBox box = {.w = 1, .h = options.thickness};
    measure_layout(box, options.position, root_container(), box);
    box.w = temp.x - box.x;

    const auto [r, g, b, a] = options.bg_color.has_value() ? options.bg_color.value() : options.color.darken();
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_Rect rect = {.x = box.x, .y = box.y, .w = box.w, .h = box.h};
    SDL_RenderFillRect(m_renderer, &rect);
    SDL_SetRenderDrawColor(m_renderer, options.color.r, options.color.g, options.color.b, options.color.a);
    rect.w = static_cast<int32_t>(box.w * options.progress);
    SDL_RenderFillRect(m_renderer, &rect);

    if ( out_box != nullptr )
        *out_box = box;
}

SDL_Texture *etsuko::Renderer::make_new_texture_target(int32_t w, int32_t h) {
    if ( m_root_texture != nullptr ) {
        throw std::runtime_error("Drawing to two textures simultaneously is not supported yet");
    }

    constexpr auto format = SDL_PIXELFORMAT_RGBA8888;
    constexpr auto access = SDL_TEXTUREACCESS_TARGET;
    SDL_Texture *texture = SDL_CreateTexture(m_renderer, format, access, w, h);
    if ( texture == nullptr ) {
        throw std::runtime_error("Failed to create texture for drawing text");
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    // Clear texture
    m_root_texture = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, texture);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    return texture;
}

void etsuko::Renderer::restore_texture_target() {
    SDL_SetRenderTarget(m_renderer, m_root_texture);
    m_root_texture = nullptr;
}

namespace {
    void internal_draw_play_icon(SDL_Renderer *renderer, int32_t x, int32_t y, int32_t width, int32_t height, const Color &color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 0xFF);
        const SDL_Point playTriangle[4] = {{x, y}, {x, y + height}, {x + width, y + height / 2}, {x, y}};
        SDL_RenderDrawLines(renderer, playTriangle, 4);
    }

    void internal_draw_pause_icon(SDL_Renderer *renderer, int32_t x, int32_t y, int32_t width, int32_t height, const Color &color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 0xFF);
        const SDL_Rect pauseRect1 = {x, y, width / 3, height};
        const SDL_Rect pauseRect2 = {x + 2 * width / 3, y, width / 3, height};
        SDL_RenderFillRect(renderer, &pauseRect1);
        SDL_RenderFillRect(renderer, &pauseRect2);
    }
}

std::shared_ptr<BakedDrawable> etsuko::Renderer::draw_button_baked(const ButtonOpts &opts, ContainerLike const &container) {
    if ( opts.label_icon == ButtonOpts::LABEL_ICON_NONE && !opts.label.has_value() ) {
        throw std::runtime_error("Can't have an empty button");
    }

    std::shared_ptr<BakedDrawable> baked_text = nullptr;
    int32_t w = 0, h = 0;
    if ( opts.label.has_value() ) {
        baked_text = draw_text_baked(opts.label.value(), container);
        w = baked_text->m_bounds.w;
        h = baked_text->m_bounds.h;
    }

    constexpr auto default_icon_width = 16, default_icon_height = 16;
    if ( opts.label_icon != ButtonOpts::LABEL_ICON_NONE ) {
        w = std::max(w, default_icon_width);
        h = std::max(h, default_icon_height);
    }

    w += opts.horizontal_padding * 2;
    h += opts.vertical_padding * 2;

    SDL_Texture *texture = make_new_texture_target(w, h);

    /* Actually draw the button now */
    SDL_Rect rect = {
        .x = 0,
        .y = 0,
        .w = w,
        .h = h
    };
    // Border and background
    if ( opts.draw_background && opts.bg_color.has_value() ) {
        const auto [r, g, b, a] = opts.bg_color.value();
        SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
        SDL_RenderFillRect(m_renderer, &rect);
    }

    if ( opts.draw_border && opts.border_color.has_value() ) {
        const auto [r, g, b, a] = opts.border_color.value();
        SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
        SDL_RenderDrawRect(m_renderer, &rect);
    }
    // Label inside
    rect.x = opts.horizontal_padding;
    rect.y = opts.vertical_padding;
    if ( baked_text != nullptr ) {
        SDL_RenderCopy(m_renderer, baked_text->m_texture, nullptr, &rect);
        rect.w += baked_text->bounds().w + opts.horizontal_padding;
    }

    if ( opts.label_icon != ButtonOpts::LABEL_ICON_NONE ) {
        switch ( opts.label_icon ) {
        case ButtonOpts::LABEL_ICON_PLAY:
            internal_draw_play_icon(m_renderer, rect.x, rect.y, default_icon_width, default_icon_height, Color::white());
            break;
        case ButtonOpts::LABEL_ICON_PAUSE:
            internal_draw_pause_icon(m_renderer, rect.x, rect.y, default_icon_width, default_icon_height, Color::white());
            break;
        default:
            throw std::runtime_error("Invalid label icon value");
        }
    }

    BoundingBox bounds = {};
    measure_layout_texture(texture, opts.position, container, bounds);

    restore_texture_target();

    auto baked = std::make_shared<BakedDrawable>();
    baked->set_texture(texture);
    baked->set_bounds(bounds);
    baked->set_valid(true);
    return baked;
}

void etsuko::Renderer::load_font(const std::string &path, const size_t index) {
    const auto last_dot = path.find_last_of('.');
    if ( last_dot == std::string::npos ) {
        throw std::runtime_error("Invalid font path");
    }

    if ( path.substr(last_dot + 1) == "ttc" ) {
        m_font = TTF_OpenFontIndexDPI(path.c_str(), DEFAULT_PT, static_cast<int64_t>(index), m_h_dpi, m_v_dpi);
    } else {
        m_font = TTF_OpenFontDPI(path.c_str(), DEFAULT_PT, m_h_dpi, m_v_dpi);
    }

    if ( m_font == nullptr ) {
        std::puts("Could not load font");
        std::puts(TTF_GetError());
        return;
    }

    TTF_SetFontHinting(m_font, TTF_HINTING_NORMAL);
    TTF_SetFontKerning(m_font, 1);
}
