/**
 * renderer_ex.h - Things not explicitly part of the regular renderer part
 */

#pragma once

#include "renderer.h"

namespace etsuko::renderer {
    struct ScrollingLyricsContainerOpts {
        enum AlignMode {
            ALIGN_LEFT = 0,
            ALIGN_CENTER,
            ALIGN_RIGHT
        };

        double margin_top_percent = 0;
        double vertical_padding_percent = 0.025;
        double active_padding_percent = 0.03;
        AlignMode alignment = ALIGN_LEFT;
    };

    class BakedDrawableScrollingLyricsContainer final : public VerticalSplitContainer {
        std::shared_ptr<parser::Song> m_song;
        std::vector<std::shared_ptr<BakedDrawable>> m_drawables;
        BoundingBox m_viewport = {};
        ScrollingLyricsContainerOpts m_opts;
        double m_elapsed_time = 0.0;
        double m_delta_time = 0.0;
        size_t m_active_index = 0, m_draw_active_index = 0;
        size_t m_total_before_size = 0;
        Renderer *m_renderer = nullptr;
        bool m_first_draw = true;
        // Animations
        double m_active_y_offset_delta = 0.0, m_previous_active_y_offset = 0.0;

        [[nodiscard]] size_t find_last_visible_index() const {
            if ( m_song == nullptr ) {
                throw std::runtime_error("Song is null");
            }

            for ( size_t i = 0; i < m_song->lyrics.size() - 1; ++i ) {
                const auto &elem = m_song->lyrics.at(i);
                const auto &next = m_song->lyrics.at(i + 1);
                if ( m_elapsed_time >= elem.base_start_time && m_elapsed_time < next.base_start_time ) {
                    return i;
                }
            }
            return m_active_index;
        }

        [[nodiscard]] size_t find_active_index() const {
            if ( m_song == nullptr ) {
                throw std::runtime_error("Song is null");
            }

            for ( size_t i = 0; i < m_song->lyrics.size(); ++i ) {
                const auto &elem = m_song->lyrics.at(i);
                if ( m_elapsed_time >= elem.base_start_time && m_elapsed_time < elem.base_start_time + elem.base_duration ) {
                    return i;
                }
            }

            return 0;
        }

        [[nodiscard]] static TextOpts build_text_opts(const parser::TimedLyric &elem, const bool active) {
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

    public:
        explicit BakedDrawableScrollingLyricsContainer(Renderer *renderer, const std::shared_ptr<parser::Song> &song, const bool left, const ContainerLike *parent, const ScrollingLyricsContainerOpts &opts) :
            VerticalSplitContainer(left, parent), m_opts(opts), m_renderer(renderer), m_song(song) {
            if ( m_renderer == nullptr ) {
                throw std::runtime_error("Renderer is null");
            }

            m_viewport = m_bounds;

            m_drawables.clear();
            m_drawables.reserve(m_song->lyrics.size());
            for ( size_t i = 0; i < m_song->lyrics.size(); ++i ) {
                const auto &elem = m_song->lyrics.at(i);
                const auto text_opts = build_text_opts(elem, i == m_active_index);
                m_drawables.push_back(m_renderer->draw_text_baked(text_opts, *this));
            }
        }

        [[nodiscard]] const std::vector<std::shared_ptr<BakedDrawable>> &drawables() const {
            return m_drawables;
        }

        [[nodiscard]] const ScrollingLyricsContainerOpts &opts() const {
            return m_opts;
        }

        void set_item_enabled(const size_t index, const bool enabled) const {
            if ( index >= m_drawables.size() ) {
                throw std::runtime_error("Invalid index passed to set_item_enabled");
            }

            m_drawables[index]->set_enabled(enabled);
        }

        void notify_window_resized() override {
            VerticalSplitContainer::notify_window_resized();

            for ( const auto &drawable : m_drawables ) {
                drawable->invalidate();
            }
        }

        void draw(const Renderer &renderer, const bool animate) {
            if ( m_song->lyrics.size() != m_drawables.size() ) {
                throw std::runtime_error("Invalid state: Not all lyric elements have generated drawables");
            }

            const auto last_visible_index = find_last_visible_index();

            const auto virtual_target_y = static_cast<int32_t>(m_bounds.h * m_opts.margin_top_percent);
            CoordinateType y = virtual_target_y - m_viewport.y - static_cast<int32_t>(m_opts.vertical_padding_percent * m_bounds.h);
            // Draw lines before the current one
            if ( m_viewport.y < 0 ) {
                for ( ssize_t i = static_cast<ssize_t>(last_visible_index) - 1; i >= 0; i-- ) {
                    const auto opts = build_text_opts(m_song->lyrics.at(i), false);

                    const auto drawable = m_renderer->draw_text_baked(opts, *this);

                    CoordinateType x;
                    if ( m_opts.alignment == ScrollingLyricsContainerOpts::ALIGN_LEFT ) {
                        x = 0;
                    } else if ( m_opts.alignment == ScrollingLyricsContainerOpts::ALIGN_CENTER ) {
                        x = m_bounds.w / 2 - drawable->bounds().w / 2;
                    } else if ( m_opts.alignment == ScrollingLyricsContainerOpts::ALIGN_RIGHT ) {
                        x = m_bounds.w - drawable->bounds().w;
                    } else
                        throw std::runtime_error("Invalid alignment option");

                    const BoundingBox line_bounds = {.x = x, .y = y - drawable->bounds().h, .w = drawable->bounds().w, .h = drawable->bounds().h};
                    drawable->set_bounds(line_bounds);

                    m_renderer->render_baked(*drawable, *this, 200);
                    y = line_bounds.y - static_cast<int32_t>(m_opts.vertical_padding_percent * m_bounds.h);
                }
            }

            // The current line and the previous ones
            y = virtual_target_y - m_viewport.y;
            for ( size_t i = last_visible_index; i < m_song->lyrics.size(); i++ ) {
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

                const auto &drawable = m_drawables.at(i);
                if ( !drawable->is_enabled() )
                    continue;

                if ( i == m_active_index && m_active_index != m_draw_active_index ) {
                    constexpr auto translate_duration = 0.25;
                    const auto duration = m_song->translate_duration_override == 0 ? translate_duration : m_song->translate_duration_override;
                    m_active_y_offset_delta = drawable->bounds().y > 0 ? (y - drawable->bounds().y) / duration : 1.0;
                }

                auto animated_y = y;

                const auto frame_delta = m_active_y_offset_delta * m_delta_time;
                const auto frame_target = drawable->bounds().y + frame_delta - m_viewport.y;
                if ( !m_first_draw && animate && y < frame_target ) {
                    animated_y = std::max(y, static_cast<CoordinateType>(frame_target));
                }

                /*if ( animated_y >= m_bounds.y + m_bounds.h )
                    break;*/

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

                if ( distance_to_target_y + drawable->bounds().h >= 0 || 1 ) {
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

        void snap_back_to_active() {
            m_viewport.y = 0;
        }

        [[nodiscard]] CoordinateType total_height() const {
            CoordinateType h = 0;
            for ( const auto &drawable : m_drawables ) {
                if ( drawable != nullptr && drawable->is_enabled() ) {
                    h += drawable->bounds().h + static_cast<int32_t>(m_bounds.h * m_opts.vertical_padding_percent);
                }
            }

            return h;
        }

        void loop(const EventManager &events, const double delta_time, const double audio_elapsed_time) {
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

        [[nodiscard]] const BoundingBox &get_bounds() const override {
            return m_bounds;
        }
    };
}
