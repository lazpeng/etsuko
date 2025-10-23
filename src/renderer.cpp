#include "renderer.h"
#include "parser.h"

#include <iostream>

#include <SDL2/SDL_image.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace etsuko::renderer;

constexpr auto TILING_PARTS = 20;

int32_t ScrollingLyricsContainer::total_size_before_active() const {
    if ( m_active_index == 0 )
        return 0;

    int32_t total_size = 0;
    for ( int32_t i = 0; i < m_active_index; ++i ) {
        total_size += m_drawables.at(i)->bounds().h;
        total_size += static_cast<int32_t>(m_opts.vertical_padding_percent * m_bounds.h);
    }

    return total_size;
}

[[nodiscard]] TextOpts ScrollingLyricsContainer::build_text_opts(const parser::TimedLyric &elem, const bool active) {
    constexpr Color active_color = {.r = 255, .g = 255, .b = 255, .a = 255};
    constexpr Color inactive_color = {.r = 100, .g = 100, .b = 100, .a = 255};
    std::string line_text = elem.full_line;
    if ( line_text.empty() ) {
        if ( elem.base_duration > 1.5 ) {
            line_text = "...";
        } else {
            line_text = " ";
        }
    }
    constexpr auto active_em = 2.0;
    const auto em = active ? active_em : 1.5;
    return {
        .text = line_text,
        .position = {},
        .em_size = em,
        .bold = false,
        .color = active ? active_color : inactive_color,
        .layout_opts = {
            .wrap = true,
            .wrap_opts = {
                .measure_at_em = active_em,
                .wrap_width_threshold = 0.85,
                .line_padding = 10,
            }
        },
        .font_kind = TextOpts::FONT_LYRIC,
    };
}

ScrollingLyricsContainer::ScrollingLyricsContainer(const ScrollingLyricsContainerOpts &opts) :
    VerticalSplitContainer(opts.is_left, opts.parent) {
    m_opts = opts;
    m_song = opts.song;
    m_renderer = opts.renderer;
    if ( m_renderer == nullptr ) {
        throw std::runtime_error("Renderer is null");
    }
    m_viewport = m_bounds;
    m_drawables.reserve(m_song->lyrics.size());
    rebuild_drawables();
}

void ScrollingLyricsContainer::rebuild_drawables() {
    m_drawables.clear();
    for ( size_t i = 0; i < m_song->lyrics.size(); ++i ) {
        const auto &elem = m_song->lyrics.at(i);
        const auto text_opts = build_text_opts(elem, i == m_active_index);
        m_drawables.push_back(m_renderer->draw_text_baked(text_opts, *this));
    }
}

[[nodiscard]] int32_t ScrollingLyricsContainer::find_active_index() const {
    if ( m_song == nullptr ) {
        throw std::runtime_error("Song is null");
    }

    for ( int32_t i = 0; i < m_song->lyrics.size(); ++i ) {
        const auto &elem = m_song->lyrics.at(i);
        if ( m_elapsed_time >= elem.base_start_time && m_elapsed_time < elem.base_start_time + elem.base_duration ) {
            return i;
        }
    }

    return 0;
}

int32_t ScrollingLyricsContainer::distance_between_lines(int32_t a, int32_t b) const {
    if ( a == b )
        return 0;
    auto invert_flag = false;
    if ( a > b ) {
        invert_flag = true;
        std::swap(a, b);
    }

    int32_t distance = 0;
    for ( int32_t i = a; i < b; i++ ) {
        distance += m_drawables.at(i)->bounds().h;
        distance += static_cast<int32_t>(m_bounds.h * m_opts.vertical_padding_percent);
    }

    if ( invert_flag )
        distance *= -1;

    return distance;
}

void ScrollingLyricsContainer::draw(const Renderer &renderer, const bool animate) {
    if ( m_song->lyrics.size() != m_drawables.size() ) {
        throw std::runtime_error("Invalid state: Not all lyric elements have generated drawables");
    }

    const auto before_active_offset = total_size_before_active();
    const auto virtual_target_y = static_cast<int32_t>(m_bounds.h * m_opts.margin_top_percent) - m_viewport.y;
    CoordinateType y = virtual_target_y - before_active_offset;
    for ( int32_t i = 0; i < m_drawables.size(); ++i ) {
        const auto &drawable = m_drawables.at(i);
        if ( (i < m_active_index && m_viewport.y > 0) ) {
            y += static_cast<int32_t>(drawable->bounds().h + m_bounds.h * m_opts.vertical_padding_percent);
            continue;
        }

        if ( m_active_index != m_draw_active_index && i <= m_active_index ) {
            // Invalidate the drawable
            m_drawables[i]->invalidate();

            if ( i != m_draw_active_index ) {
                m_drawables[m_draw_active_index]->invalidate();
            }
        }

        if ( !m_drawables.at(i)->is_valid() ) {
            const auto old_bounds = m_drawables.at(i)->bounds();
            const bool is_active = i == m_active_index;
            m_drawables[i] = m_renderer->draw_text_baked(build_text_opts(m_song->lyrics.at(i), is_active), *this);

            const auto bounds = m_drawables.at(i)->bounds();
            m_drawables.at(i)->set_bounds({.x = old_bounds.x, .y = old_bounds.y, .w = bounds.w, .h = bounds.h});
        }

        if ( !drawable->is_enabled() )
            continue;

        constexpr auto translate_duration = 2.5;
        if ( i == m_active_index && m_active_index != m_draw_active_index ) {
            const auto duration = m_song->translate_duration_override == 0 ? translate_duration : m_song->translate_duration_override;
            const auto distance = std::min(0, virtual_target_y - drawable->bounds().y);
            m_active_y_offset_delta = distance / duration;
            m_anim_duration = 0;
            std::print("active offset delta: {} distance: {} \n", m_active_y_offset_delta, distance);
        }

        auto animated_y = y;

        const auto frame_delta = m_active_y_offset_delta * m_delta_time;
        const auto frame_target = drawable->bounds().y + frame_delta - m_viewport.y;
        if ( !m_first_draw && animate && m_anim_duration < translate_duration ) {
            std::print("frame delta: {}\n", frame_delta);
            animated_y = drawable->bounds().y + frame_delta;// std::min(y, static_cast<CoordinateType>(y + frame_delta));
            std::print("animated y: {}\n", animated_y);
            m_anim_duration += m_delta_time;
        }

        if ( animated_y >= m_bounds.y + m_bounds.h )
            break;

        CoordinateType x;
        if ( m_opts.alignment == ScrollingLyricsContainerOpts::ALIGN_LEFT ) {
            x = 0;
        } else if ( m_opts.alignment == ScrollingLyricsContainerOpts::ALIGN_CENTER ) {
            x = m_bounds.w / 2 - drawable->bounds().w / 2;
        } else if ( m_opts.alignment == ScrollingLyricsContainerOpts::ALIGN_RIGHT ) {
            x = m_bounds.w - drawable->bounds().w;
        } else
            throw std::runtime_error("Invalid alignment option");

        drawable->set_bounds({.x = x, .y = animated_y, .w = drawable->bounds().w, .h = drawable->bounds().h});

        constexpr auto max_fade_steps = 4;
        const auto max_distance_step = (get_bounds().y + get_bounds().h - y) / max_fade_steps;
        const auto distance_to_target_y = y - virtual_target_y + drawable->bounds().h;

        if ( distance_to_target_y + drawable->bounds().h >= 0 || true ) {
            auto alpha = 255;
            if ( m_song->lyrics.at(m_active_index).full_line.empty() && m_active_index != i ) {
                alpha = 50;
            } else {
                const auto steps = static_cast<int32_t>(std::floor(distance_to_target_y / max_distance_step));
                for ( int distance = 1; distance < steps; distance++ ) {
                    //alpha = static_cast<uint8_t>(alpha / 1.5);
                }
            }
            renderer.render_baked(*drawable, *this, alpha);
        }

        auto vertical_padding = m_bounds.h * m_opts.vertical_padding_percent;
        if ( m_active_index == i && m_song->lyrics.at(i).full_line.empty() ) {
            vertical_padding += m_bounds.h * m_opts.active_padding_percent;
        }

        y += static_cast<int32_t>(drawable->bounds().h + vertical_padding);

        if ( m_first_draw ) {
            m_first_draw = false;
        }
    }

    m_draw_active_index = m_active_index;
}

void ScrollingLyricsContainer::loop(const EventManager &events, const double delta_time, const double audio_elapsed_time) {
    const auto scrolled = events.amount_scrolled();
    m_elapsed_time = audio_elapsed_time;
    m_delta_time = delta_time;
    m_active_index = find_active_index();

    if ( scrolled != 0.0 ) {
#ifdef __EMSCRIPTEN__
        constexpr auto scroll_speed = 50.0;
#else
        constexpr auto scroll_speed = 10.0;
#endif

        m_viewport.y = static_cast<CoordinateType>(m_viewport.y - scrolled * scroll_speed);

        //m_viewport.y = std::max(0, m_viewport.y);
        m_viewport.y = std::min(m_viewport.y, total_height());
    }
}

void etsuko::Renderer::measure_line_size(const std::string &text, const int pt, int32_t *w, int32_t *h, const TextOpts::FontKind kind) const {
    const auto font = kind == TextOpts::FONT_UI ? m_ui_font : m_lyric_font;
    if ( TTF_SetFontSizeDPI(font, pt, m_h_dpi, m_v_dpi) != 0 ) {
        throw std::runtime_error("Failed to set font size/DPI");
    }

    if ( TTF_SizeUTF8(font, text.c_str(), w, h) != 0 ) {
        throw std::runtime_error("Failed to measure line size");
    }
}

int32_t etsuko::Renderer::em_to_pt_size(const double em) const {
    constexpr auto base_pt_size = DEFAULT_PT;
    constexpr auto base_width = DEFAULT_WIDTH;
    const auto scale = m_viewport.w / static_cast<double>(base_width);
    const auto rem = std::max(12.0, std::round(base_pt_size * scale));
    const auto pixels = em * rem;
    const auto pt_size = static_cast<int32_t>(std::lround(pixels * 72.0 / m_h_dpi));
    return pt_size;
}

int etsuko::Renderer::initialize() {
    if ( SDL_Init(SDL_INIT_VIDEO) != 0 ) {
        std::puts(SDL_GetError());
        return -1;
    }

    if ( TTF_Init() != 0 ) {
        std::puts(TTF_GetError());
        return -1;
    }

    constexpr auto image_formats = IMG_INIT_PNG | IMG_INIT_JPG;
    if ( IMG_Init(image_formats) != image_formats ) {
        std::puts(IMG_GetError());
        return -1;
    }

    constexpr auto pos = SDL_WINDOWPOS_CENTERED;
    constexpr auto flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    m_window = SDL_CreateWindow(DEFAULT_TITLE, pos, pos, DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if ( m_window == nullptr ) {
        return -2;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    SDL_SetHint(SDL_HINT_RENDER_OPENGL_SHADERS, "1");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");

    constexpr auto renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    m_renderer = SDL_CreateRenderer(m_window, -1, renderer_flags);
    if ( m_renderer == nullptr ) {
        return -3;
    }

#ifdef __EMSCRIPTEN__
    //double devicePixelRatio = emscripten_get_device_pixel_ratio();
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);

    const auto width = static_cast<int32_t>(cssWidth);
    const auto height = static_cast<int32_t>(cssHeight);
    SDL_SetWindowSize(m_window, width, height);

    //SDL_RenderSetLogicalSize(m_renderer, cssWidth, cssHeight);
#endif

    notify_window_changed();

    return 0;
}

void etsuko::Renderer::notify_window_changed() {
    int32_t outW, outH;
    SDL_GetRendererOutputSize(m_renderer, &outW, &outH);
    SDL_RenderSetLogicalSize(m_renderer, outW, outH);

    m_viewport = {.x = 0, .y = 0, .w = outW, .h = outH};
    std::puts(std::format("viewport: {}x{}", m_viewport.w, m_viewport.h).c_str());

    float hdpi_temp, v_dpi_temp;
    if ( SDL_GetDisplayDPI(0, nullptr, &hdpi_temp, &v_dpi_temp) != 0 ) {
        std::puts(SDL_GetError());
        throw std::runtime_error("Failed to get DPI");
    }

    m_h_dpi = static_cast<int32_t>(hdpi_temp), m_v_dpi = static_cast<int32_t>(v_dpi_temp);
}

void etsuko::Renderer::begin_loop() {
    m_start_ticks = SDL_GetTicks64();

    auto [r,g,b,a] = m_bg_color;
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_RenderClear(m_renderer);
}

void etsuko::Renderer::end_loop() const {
    SDL_RenderPresent(m_renderer);

    SDL_SetWindowTitle(m_window, std::format("etsuko | {} fps", 1000 / (SDL_GetTicks64() - m_start_ticks)).c_str());
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

        auto measure_pt_size = 0;
        if ( text_opts.layout_opts.wrap_opts.measure_at_em != 0 ) {
            measure_pt_size = em_to_pt_size(text_opts.layout_opts.wrap_opts.measure_at_em);
        } else {
            measure_pt_size = em_to_pt_size(text_opts.em_size);
        }
        int32_t w, h;
        measure_line_size(text.substr(start, tmp_end_idx - start), measure_pt_size, &w, &h, text_opts.font_kind);

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
        x = static_cast<int32_t>(current_width / 2.0 - w / 2.0 - calc_w + x);
    } else {
        if ( x < 0 ) {
            x = current_width + x;
        }
        x -= calc_w;
    }

    // Calculate y
    if ( position.flags & Point::TILED_Y ) {
        y *= current_height / TILING_PARTS;
    }

    if ( position.flags & Point::CENTERED_Y ) {
        y = static_cast<int32_t>(current_height / 2.0 - h / 2.0 - calc_h + y);
    } else {
        if ( y < 0 ) {
            y = current_height + y;
        }
        y -= calc_h;
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

SDL_Texture *etsuko::Renderer::draw_text(const std::string_view &text, const int32_t pt_size, const bool bold, const Color color, const TextOpts::FontKind kind) const {
    const auto font = kind == TextOpts::FONT_UI ? m_ui_font : m_lyric_font;
    if ( font == nullptr ) {
        throw std::runtime_error("Font not loaded");
    }
    if ( text.empty() ) {
        throw std::runtime_error("Text is empty");
    }

    if ( TTF_SetFontSizeDPI(font, pt_size, m_h_dpi, m_v_dpi) != 0 ) {
        std::puts(TTF_GetError());
        throw std::runtime_error("Failed to set font size/DPI");
    }
    const auto style = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
    TTF_SetFontStyle(font, style);

    const auto sdl_color = SDL_Color{color.r, color.g, color.b, color.a};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.data(), sdl_color);
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

std::shared_ptr<BakedDrawable> etsuko::Renderer::draw_image_baked(const ImageOpts &opts, const ContainerLike &container) {
    SDL_Surface *loaded = IMG_Load(opts.resource_path.c_str());
    if ( loaded == nullptr ) {
        throw std::runtime_error("Failed to load image");
    }
    SDL_Surface *converted = SDL_ConvertSurfaceFormat(
        loaded,
        SDL_PIXELFORMAT_ABGR8888,
        0
        );
    if ( converted == nullptr ) {
        throw std::runtime_error("Failed to convert image surface to appropriate pixel format");
    }
    SDL_FreeSurface(loaded);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(m_renderer, converted);
    if ( texture == nullptr ) {
        throw std::runtime_error("Failed to render texture from image surface");
    }

    int32_t src_w, src_h;
    SDL_QueryTexture(texture, nullptr, nullptr, &src_w, &src_h);
    const double aspect_ratio = static_cast<double>(src_w) / static_cast<double>(src_h);

    int32_t w = opts.w, h = opts.h;
    if ( w <= 0 && h <= 0 ) {
        w = src_w;
        h = src_h;
    } else if ( w <= 0 ) {
        w = static_cast<int32_t>(h * aspect_ratio);
    } else if ( h <= 0 ) {
        h = static_cast<int32_t>(opts.w / aspect_ratio);
    }

    BoundingBox box = {};

    SDL_Texture *final_texture = make_new_texture_target(w, h);

    const SDL_Rect destination_rect = {.x = 0, .y = 0, .w = w, .h = h};
    SDL_RenderCopy(m_renderer, texture, nullptr, &destination_rect);
    SDL_DestroyTexture(texture);

    measure_layout_texture(final_texture, opts.position, container, box);

    restore_texture_target();

    auto baked = std::make_shared<BakedDrawable>();
    baked->set_texture(final_texture);
    baked->set_valid(true);
    baked->set_bounds(box);

    return baked;
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
            const auto pt_size = em_to_pt_size(opts.em_size);
            const auto texture = draw_text(line, pt_size, opts.bold, opts.color, opts.font_kind);
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
        const auto pt_size = em_to_pt_size(opts.em_size);
        final_texture = draw_text(opts.text, pt_size, opts.bold, opts.color, opts.font_kind);
    }

    // Initialize baked drawable
    auto baked = std::make_shared<BakedDrawable>();
    baked->set_texture(final_texture);
    measure_layout_texture(final_texture, opts.position, container, baked->m_bounds);
    baked->set_valid(true);
    return baked;
}

void etsuko::Renderer::render_baked(const BakedDrawable &baked, const ContainerLike &container, const std::optional<uint8_t> alpha) const {
    if ( !baked.is_valid() ) {
        throw std::runtime_error("Attempted to draw uninitialized baked drawable");
    }

    const BoundingBox &pivot = container.get_bounds();
    const SDL_Rect rect = {.x = pivot.x + baked.m_bounds.x + baked.m_viewport.x, .y = pivot.y + baked.m_bounds.y + baked.m_viewport.y, .w = baked.m_bounds.w, .h = baked.m_bounds.h};
    uint8_t alpha_backup = 0;
    if ( alpha.has_value() ) {
        SDL_GetTextureAlphaMod(baked.m_texture, &alpha_backup);
        SDL_SetTextureAlphaMod(baked.m_texture, *alpha);
    }
    SDL_RenderCopy(m_renderer, baked.m_texture, nullptr, &rect);
    if ( alpha.has_value() ) {
        SDL_SetTextureAlphaMod(baked.m_texture, alpha_backup);
    }
}

void etsuko::Renderer::render_baked(const BakedDrawable &baked, const std::optional<uint8_t> alpha) const {
    render_baked(baked, *root_container(), alpha);
}

void etsuko::Renderer::draw_horiz_progress_simple(const ProgressBarOpts &options, BoundingBox *out_box) const {
    BoundingBox temp = {.w = 1, .h = options.thickness};
    measure_layout(temp, {.x = options.end_x, .y = 0, .flags = options.position.flags}, *root_container(), temp);
    BoundingBox box = {.w = 1, .h = options.thickness};
    measure_layout(box, options.position, *root_container(), box);
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

SDL_Texture *etsuko::Renderer::make_new_texture_target(const int32_t w, const int32_t h) {
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
    void internal_draw_play_icon(SDL_Renderer *renderer, const int32_t x, const int32_t y, const int32_t width, const int32_t height, const Color &color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 0xFF);
        const SDL_Point playTriangle[4] = {{x, y}, {x, y + height}, {x + width, y + height / 2}, {x, y}};
        SDL_RenderDrawLines(renderer, playTriangle, 4);
    }

    void internal_draw_pause_icon(SDL_Renderer *renderer, const int32_t x, const int32_t y, const int32_t width, const int32_t height, const Color &color) {
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

    int32_t w = opts.icon_size, h = opts.icon_size;
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

    if ( opts.label_icon != ButtonOpts::LABEL_ICON_NONE ) {
        switch ( opts.label_icon ) {
        case ButtonOpts::LABEL_ICON_PLAY:
            internal_draw_play_icon(m_renderer, rect.x, rect.y, opts.icon_size, opts.icon_size, Color::white());
            break;
        case ButtonOpts::LABEL_ICON_PAUSE:
            internal_draw_pause_icon(m_renderer, rect.x, rect.y, opts.icon_size, opts.icon_size, Color::white());
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

void etsuko::Renderer::load_font(const std::string &path, TextOpts::FontKind kind) {
    const auto last_dot = path.find_last_of('.');
    if ( last_dot == std::string::npos ) {
        throw std::runtime_error("Invalid font path");
    }

    const auto font = TTF_OpenFontDPI(path.c_str(), DEFAULT_PT, m_h_dpi, m_v_dpi);

    if ( font == nullptr ) {
        std::puts("Could not load font");
        std::puts(TTF_GetError());
        return;
    }

    TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
    TTF_SetFontKerning(font, 1);

    if ( kind == TextOpts::FONT_UI ) {
        m_ui_font = font;
    } else if ( kind == TextOpts::FONT_LYRIC ) {
        m_lyric_font = font;
    } else {
        throw std::runtime_error("Invalid font kind");
    }
}
