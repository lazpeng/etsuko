/**
 * renderer.h - Draws stuff to the screen, like the lyric lines, effects and images
 */

#pragma once

#include "common.h"

#include <optional>
#include <string>
#include <vector>
#include <functional>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "events.h"

namespace etsuko {

    /* Forward declaration */
    class Renderer;

    namespace renderer {
        constexpr auto DEFAULT_WIDTH = 1280;
        constexpr auto DEFAULT_HEIGHT = 720;
        constexpr auto DEFAULT_TITLE = "etsuko";
        constexpr auto DEFAULT_PT = 16;

        struct Color {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            [[nodiscard]] Color darken(const double amount_perc = 0.2) const {
                const auto amount = 1.0 - amount_perc;
                return {
                    static_cast<uint8_t>(r * amount),
                    static_cast<uint8_t>(g * amount),
                    static_cast<uint8_t>(b * amount),
                    a
                };
            }

            static Color white() {
                return {255, 255, 255, 255};
            }

            static Color black() {
                return {0, 0, 0, 255};
            }
        };

        using CoordinateType = int32_t;

        /**
         * Used to position elements when baking or drawing them using the renderer. The x and y values
         * can be relative or absolute based on the supplied flags
         */
        struct Point {
            CoordinateType x, y;
            int32_t flags;

            enum Flags : int32_t {
                NONE = 0,
                CENTERED_Y = 1,
                CENTERED_X = 1 << 1,
                CENTERED = CENTERED_Y | CENTERED_X,
                TILED_Y = 1 << 2,
                TILED_X = 1 << 3,
                TILED = TILED_Y | TILED_X,
                ANCHOR_RIGHT_X = 1 << 4,
                ANCHOR_BOTTOM_Y = 1 << 5,
                ANCHOR_RIGHT_BOTTOM = ANCHOR_RIGHT_X | ANCHOR_BOTTOM_Y,
                ANCHOR_CENTER_X = 1 << 6,
                ANCHOR_CENTER_Y = 1 << 7,
                ANCHOR_CENTER = ANCHOR_CENTER_X | ANCHOR_CENTER_Y,
            };
        };

        struct ProgressBarOpts {
            Point position;
            CoordinateType end_x = 0;
            Color color = Color::white();
            std::optional<Color> bg_color = std::nullopt;
            int thickness = 1;
            /// Between 0 and 1.0
            double progress = 1.0;
        };

        struct TextLayoutOpts {
            struct WrapOpts {
                enum WrapMeasuringMode : int32_t {
                    WRAP_SCREEN_WIDTH = 0
                };

                enum WrapAlignMode : int32_t {
                    WRAP_ALIGN_CENTER = 0,
                    WRAP_ALIGN_LEFT,
                    WRAP_ALIGN_RIGHT
                };

                enum WrapMode : int32_t {
                    WRAP_AT_WHITESPACE = 0,
                    WRAP_ALWAYS
                };

                int32_t measure_at_pts = 0;
                double wrap_width_threshold = 0.8;
                CoordinateType line_padding = 0;
                WrapMeasuringMode measuring_mode = WRAP_SCREEN_WIDTH;
                WrapAlignMode alignment_mode = WRAP_ALIGN_CENTER;
                WrapMode wrap_mode = WRAP_AT_WHITESPACE;
            };

            bool wrap = false;
            WrapOpts wrap_opts = {};
        };

        struct TextOpts {
            std::string text;
            Point position;
            int32_t pt_size = DEFAULT_PT;
            bool bold = false;
            Color color = Color::white();
            TextLayoutOpts layout_opts = {};
        };

        struct TextWrapOpts {
            double max_width_perc;
            CoordinateType vertical_padding;
        };

        struct ButtonOpts {
            enum LabelIcon {
                LABEL_ICON_NONE = 0,
                LABEL_ICON_PLAY,
                LABEL_ICON_PAUSE,
            };

            std::optional<TextOpts> label;
            LabelIcon label_icon = LABEL_ICON_NONE;
            Point position;
            bool draw_border = true, draw_background = false;
            CoordinateType vertical_padding = 0, horizontal_padding = 0;
            std::optional<Color> border_color = std::nullopt, bg_color = std::nullopt;
        };

        /**
         * A simple structure that holds a pre-baked texture that can be drawn by the renderer
         */
        class BakedDrawable {
            SDL_Texture *m_texture = nullptr;
            BoundingBox m_bounds = {};
            bool m_valid = false;
            bool m_enabled = true;

            friend class etsuko::Renderer;

            void set_texture(SDL_Texture *texture) {
                if ( m_texture != nullptr ) {
                    SDL_DestroyTexture(m_texture);
                }
                m_texture = texture;
            }

            void set_valid(const bool valid) {
                m_valid = valid;
            }

        public:
            [[nodiscard]] bool is_valid() const {
                return m_valid;
            }

            void invalidate() {
                set_valid(false);
            }

            [[nodiscard]] bool is_enabled() const {
                return m_enabled;
            }

            [[nodiscard]] const BoundingBox &bounds() const {
                return m_bounds;
            }

            void set_enabled(const bool enabled) {
                m_enabled = enabled;
            }

            void set_bounds(const BoundingBox &bounds) {
                m_bounds = bounds;
            }

            BakedDrawable() = default;
            BakedDrawable(BakedDrawable &&) = default;
            BakedDrawable(BakedDrawable &) = delete;

            ~BakedDrawable() {
                if ( m_texture != nullptr ) {
                    SDL_DestroyTexture(m_texture);
                    m_texture = nullptr;
                }
            }
        };

        class ContainerLike {
        public:
            virtual ~ContainerLike() = default;
            [[nodiscard]] virtual auto get_bounds() const -> const BoundingBox & = 0;
        };

        class VirtualContainer final : public ContainerLike {
            BoundingBox m_bounds = {};

        public:
            explicit VirtualContainer(const ContainerLike &parent, BoundingBox bounds) {
                const auto parent_bounds = parent.get_bounds();
                m_bounds = {.x = parent_bounds.x + bounds.x, .y = parent_bounds.y + bounds.y, .w = bounds.w, .h = bounds.h};
            }

            [[nodiscard]] const BoundingBox &get_bounds() const override {
                return m_bounds;
            }
        };

        class VerticalSplitContainer : public ContainerLike {
        protected:
            BoundingBox m_bounds = {};

        public:
            explicit VerticalSplitContainer(const bool left, const ContainerLike &parent) {
                const auto x = left ? 0 : parent.get_bounds().w / 2;
                m_bounds = {.x = x, .y = 0, .w = parent.get_bounds().w / 2, .h = parent.get_bounds().h};
            }

            [[nodiscard]] const BoundingBox &get_bounds() const override {
                return m_bounds;
            }
        };

        struct ScrollingContainerOpts {
            enum AlignMode {
                ALIGN_LEFT = 0,
                ALIGN_CENTER,
                ALIGN_RIGHT
            };

            int32_t margin_top = 0;
            int32_t vertical_padding = 0;
            AlignMode alignment = ALIGN_LEFT;
        };

        struct ImageOpts {
            Point position;
            CoordinateType w, h;
            std::string resource_path;
        };
    } // namespace renderer

    class Renderer final : public renderer::ContainerLike {
        SDL_Window *m_window = nullptr;
        SDL_Renderer *m_renderer = nullptr;
        TTF_Font *m_font = nullptr;
        SDL_Texture *m_root_texture = nullptr;
        BoundingBox m_viewport = {};
        int32_t m_h_dpi = 0, m_v_dpi = 0;

        void measure_line_size(const std::string &text, int pt, int32_t *w, int32_t *h) const;
        static void measure_layout(const BoundingBox &box, const renderer::Point &position, const ContainerLike &container, BoundingBox &out_box);
        static void measure_layout_texture(SDL_Texture *texture, const renderer::Point &position, const ContainerLike &container, BoundingBox &out_box);
        [[nodiscard]] size_t measure_text_wrap_stop(const renderer::TextOpts &text_opts, const ContainerLike &container, size_t start = 0) const;
        [[nodiscard]] SDL_Texture *draw_text(const std::string_view &text, int32_t pt_size, bool bold, renderer::Color color) const;

        SDL_Texture *make_new_texture_target(int32_t w, int32_t h);
        void restore_texture_target();

    public:
        /* Init and lifetime functions */
        int initialize();
        void begin_loop() const;
        void end_loop() const;
        void finalize();

        /* Implements for being a (root) container for drawables */
        [[nodiscard]] const BoundingBox &get_bounds() const override {
            return m_viewport;
        }

        [[nodiscard]] const Renderer &root_container() const {
            return *this;
        }

        /* Actually doing stuff */
        void draw_horiz_progress_simple(const renderer::ProgressBarOpts &options, BoundingBox *out_box) const;

        /**
         * Bakes a texture with the given text so it doesn't have to be recomputed on every frame.
         * @param opts Parameters to build the baked text.
         * @param container container to serve as the base for calculating position and alignment
         */
        std::shared_ptr<renderer::BakedDrawable> draw_text_baked(const renderer::TextOpts &opts, const ContainerLike &container);

        /**
         * Bakes a texture of a button with the given parameters
         * @param opts Options for building the button
         * @param container Container to align and position the button relative to
         */
        std::shared_ptr<renderer::BakedDrawable> draw_button_baked(const renderer::ButtonOpts &opts, const ContainerLike &container);

        std::shared_ptr<renderer::BakedDrawable> draw_image_baked(const renderer::ImageOpts &opts, const ContainerLike &container);

        /**
         * Renders a baked drawable to the screen
         * @param baked reference to the drawable to be drawn
         * @param container container the drawable is part of to calculate offsets and positioning
         */
        void render_baked(const renderer::BakedDrawable &baked, const ContainerLike &container) const;

        /**
         * Renders a baked drawable to the screen, using the default viewport as the container
         * @param baked reference to the drawable to be drawn
         */
        void render_baked(const renderer::BakedDrawable &baked) const;

        /* Loaders and whatnot */
        void load_font(const std::string &path, size_t index = 0);
    };
} // namespace etsuko
