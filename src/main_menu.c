#include <stdlib.h>

#include "config.h"
#include "error.h"
#include "events.h"
#include "main_menu.h"
#include "repository.h"
#include "secret.h"
#include "song.h"
#include "str_utils.h"
#include "ui.h"
#include "user_settings.h"

#include <stdio.h>
#include <string.h>

#define ALBUM_SCALE_DURATION (0.2)
#define ALBUM_SCALE_DOWN_DURATION (0.1)
#define ALBUM_SCALE_FACTOR (1.2)
#define MAX_SONGS_PER_ROW (4)
#define SONG_TITLE_REGULAR_OFFSET_Y (0.01)
#define SONG_TITLE_SCALED_OFFSET_Y (0.04)
#define SONG_ALBUM_REGULAR_OFFSET_Y (0.005)
#define SONG_ALBUM_SCALED_OFFSET_Y (0.01)
#define PILL_REGULAR_OFFSET_Y (0.06)
#define PILL_SCALED_OFFSET_Y (0.1)

// Prev comment: "This shit shouldn't be here"
// but unfortunately the "ui" is too barebones to offer a generic solution for this problem, so
// it ultimately does need to be in here
typedef struct GridLayoutInfo_t {
    WEAK Drawable_t *first_in_row, *last_in_row;
    int row_count;
    double max_y;
} GridLayoutInfo_t;

struct MainMenu_t {
    OWNING Ui_t *ui;
    OWNING HashMap_t *menu_artists; // of song.h MenuArtist_t
    OWNING Resource_t *load_ui_font, *load_song_list, *load_no_album_art;
    OWNING HashMap_t *album_arts;          // of AlbumArtData_t
    OWNING HashMap_t *album_art_drawables; // of SongEntryDrawables_t
    OWNING Vector_t *album_art_loads;      // of Resource_t
    WEAK const char *selected_song;
    WEAK Container_t *container;
    bool album_data_dirty;
    GridLayoutInfo_t grid_layout_info;
};

typedef struct SongEntryDrawables_t {
    WEAK Drawable_t *image, *title, *album;
    WEAK Container_t *pills_container;
    WEAK MainMenu_t *menu;
    WEAK const char *key;
} SongEntryDrawables_t;

typedef enum AlbumArtState_t { ART_NOT_LOADED = 0, ART_PENDING_DRAW = 1, ART_OK } AlbumArtState_t;

#define NO_ART_KEY "_no_art"

typedef struct AlbumArtData_t {
    WEAK MainMenu_t *menu;
    WEAK const char *id;
    Color_t sampled_colors[5];
    OWNING unsigned char *image_data;
    uint64_t image_data_size;
    AlbumArtState_t state;
} AlbumArtData_t;

static void album_art_loaded(const Resource_t *res) {
    AlbumArtData_t *data = res->custom_data;

    data->image_data = res->buffer->data;
    data->image_data_size = res->buffer->downloaded_bytes;

    render_sample_bg_colors_from_image(res->buffer->data, (int)res->buffer->downloaded_bytes, data->sampled_colors);

    data->state = ART_PENDING_DRAW;
    if ( str_is_empty(data->menu->selected_song) )
        data->menu->selected_song = data->id;

    data->menu->album_data_dirty = true;
}

static void iterate_albums(const char *key, void *value, void *userdata) {
    MenuAlbum_t *album = value;
    if ( album == NULL ) {
        printf("Warning: album for key %s is null.\n", key);
        return;
    }

    MainMenu_t *menu = userdata;

    for ( size_t i = 0; i < album->songs->size; i++ ) {
        MenuSong_t *song = album->songs->data[i];
        if ( str_is_empty(song->album_art_path) )
            continue;
        AlbumArtData_t *data = calloc(1, sizeof(*data));
        data->id = song->id;
        data->menu = menu;
        map_put(menu->album_arts, song->id, data);

        const LoadRequest_t request = {
            .custom_data = data, .on_resource_loaded = album_art_loaded, .absolute_path = song->album_art_path};

        // Enqueue
        vec_add(menu->album_art_loads, repo_load_resource(&request));
    }
}

static void iterate_artists(const char *key, void *value, void *userdata) {
    const MenuArtist_t *artist = value;
    if ( artist == NULL ) {
        printf("Warning: artist for key %s is null.\n", key);
        return;
    }

    map_iterate(artist->albums, iterate_albums, userdata);
}

static void song_list_loaded(const Resource_t *res) {
    StrBuffer_t *buffer = str_buf_init();
    str_buf_append_len(buffer, (const char *)res->buffer->data, (int)res->buffer->downloaded_bytes);

    MainMenu_t *menu = res->custom_data;
    menu->menu_artists = menu_songs_parse(buffer->data, (int)buffer->len);
    str_buf_destroy(buffer);

    if ( menu->menu_artists == NULL )
        error_abort("Failed to load song list");

    // Set up load requests for all the album arts
    map_iterate(menu->menu_artists, iterate_artists, menu);
}

MainMenu_t *menu_init() {
    MainMenu_t *menu = calloc(1, sizeof(*menu));
    if ( menu == NULL )
        error_abort("Failed to allocate data for the main menu");
    menu->ui = ui_init();
    menu->album_art_loads = vec_init();
    menu->album_arts = map_init();
    menu->menu_artists = map_init();
    menu->album_art_drawables = map_init();

    render_set_window_title("etsuko");

    return menu;
}

static void ui_font_loaded(const Resource_t *res) {
    ui_load_font(res->buffer->data, (int)res->buffer->downloaded_bytes, FONT_UI);
}

static void no_album_art_loaded(const Resource_t *res) {
    // Create a temp album art for it
    AlbumArtData_t *data = calloc(1, sizeof(*data));
    data->id = NO_ART_KEY;
    data->menu = res->custom_data;
    data->state = ART_OK;

    data->image_data = malloc(res->buffer->downloaded_bytes);
    memcpy(data->image_data, res->buffer->data, res->buffer->downloaded_bytes);
    data->image_data_size = res->buffer->downloaded_bytes;

    map_put(data->menu->album_arts, NO_ART_KEY, data);
}

AppStatus_t menu_load_loop(MainMenu_t *menu) {
    if ( menu->load_ui_font == NULL ) {
        const LoadRequest_t request = {.custom_data = menu,
                                       .on_resource_loaded = ui_font_loaded,
                                       .relative_path = config_get()->ui_font,
                                       .sub_dir = "files/"};
        menu->load_ui_font = repo_load_resource(&request);
    }
    if ( menu->load_song_list == NULL ) {
        const LoadRequest_t request = {
            .custom_data = menu, .on_resource_loaded = song_list_loaded, .absolute_path = API_SONG_LIST_ENDPOINT};
        menu->load_song_list = repo_load_resource(&request);
    }
    if ( menu->load_no_album_art == NULL ) {
        const LoadRequest_t request = {
            .custom_data = menu, .on_resource_loaded = no_album_art_loaded, .relative_path = "no_album.jpg", .sub_dir = "img/"};
        menu->load_no_album_art = repo_load_resource(&request);
    }

    const bool loaded = menu->load_ui_font->status == LOAD_DONE && menu->load_song_list->status == LOAD_DONE &&
                        menu->load_no_album_art->status == LOAD_DONE && !str_is_empty(menu->selected_song);
    if ( loaded )
        return APP_STATUS_OK;
    return APP_STATUS_LOADING;
}

static void on_album_art_event(const UiEventOpts_t *opts, Drawable_t *, void *custom_data) {
    const SongEntryDrawables_t *entry = custom_data;
    MainMenu_t *menu = entry->menu;

    if ( opts->event == UI_EVENT_MOUSE_HOVER_ENTERED ) {
        menu->selected_song = entry->key;
        ui_drawable_set_scale_factor(entry->image, ALBUM_SCALE_FACTOR);
        ui_drawable_set_scale_factor(entry->title, ALBUM_SCALE_FACTOR);
        ui_drawable_set_scale_factor(entry->album, ALBUM_SCALE_FACTOR);

        // Change the layout slightly so the text gets out of the way of the enlarged image
        // this is unfortunately necessary because the ui design is shit
        // change the title only because it's the one right below. the rest should reposition accordingly
        entry->title->layout.offset_y = SONG_TITLE_SCALED_OFFSET_Y;
        ui_reposition_drawable(entry->title);
        // also change the album to keep a uniform spacing when scaled
        entry->album->layout.offset_y = SONG_ALBUM_SCALED_OFFSET_Y;
        ui_reposition_drawable(entry->album);
        entry->pills_container->layout.offset_y = PILL_SCALED_OFFSET_Y;
        ui_reposition_container(entry->pills_container);

        // Update background
        menu->album_data_dirty = true;
    } else if ( opts->event == UI_EVENT_MOUSE_HOVER_EXITED ) {
        ui_drawable_set_scale_factor_dur(entry->image, 1.f, ALBUM_SCALE_DOWN_DURATION);
        ui_drawable_set_scale_factor_dur(entry->title, 1.f, ALBUM_SCALE_DOWN_DURATION);
        ui_drawable_set_scale_factor_dur(entry->album, 1.f, ALBUM_SCALE_DOWN_DURATION);

        entry->title->layout.offset_y = SONG_TITLE_REGULAR_OFFSET_Y;
        ui_reposition_drawable(entry->title);
        entry->album->layout.offset_y = SONG_ALBUM_REGULAR_OFFSET_Y;
        ui_reposition_drawable(entry->album);
        entry->pills_container->layout.offset_y = PILL_REGULAR_OFFSET_Y;
        ui_reposition_container(entry->pills_container);
    } else if ( opts->event == UI_EVENT_MOUSE_CLICK ) {
        config_set_karaoke_song_file(config_get(), entry->key);
        etsuko_navigate("/?song=", entry->key);

        global_mode_switch(APP_MODE_KARAOKE);
    }
}

static Drawable_t *setup_song_title(const MainMenu_t *menu, const MenuSong_t *song, const Drawable_t *image) {
    const Layout_t layout = {
        .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_X_POS_TO_RELATIVE |
                 LAYOUT_CENTER_X | LAYOUT_PROPORTIONAL_Y,
        .relative_to = image,
        .offset_y = SONG_TITLE_REGULAR_OFFSET_Y,
    };
    const Drawable_TextData_t data = {.text = song->name, .color = {.r = 255, .g = 255, .b = 255, .a = 255}, .em = 0.7};
    Drawable_t *text = ui_make_text(menu->ui, &data, menu->container, &layout);
    text->center_on_scale = true;
    ui_drawable_set_alpha_immediate(text, 200);

    const Animation_ScaleData_t scale_data = {
        .duration = ALBUM_SCALE_DURATION,
    };
    ui_animate_scale(text, &scale_data);
    const Animation_EaseTranslationData_t translation_data = {.duration = ALBUM_SCALE_DURATION};
    ui_animate_translation(text, &translation_data);

    return text;
}

static Drawable_t *setup_single_tag_pill(const MainMenu_t *menu, char *tag, const Drawable_t *prev_pill, Container_t *container) {
    const Drawable_TextData_t text_data = {
        .em = 0.5,
        .color = {.r = 255, .g = 255, .b = 255, .a = 255},
        .text = tag,
    };
    Drawable_t *text = ui_make_text(menu->ui, &text_data, container, &(Layout_t){});

    Layout_t rect_layout = {
        .flags = LAYOUT_PROPORTIONAL_POS | LAYOUT_RELATIVE_TO_SIZE | LAYOUT_PROPORTIONAL_SIZE,
        .width = 1.0,
        .height = 1.5,
        .padding_w = 0.1,
        .relative_to_size = text,
    };
    if ( prev_pill != NULL ) {
        rect_layout.relative_to = prev_pill;
        rect_layout.offset_x = 0.025;
        rect_layout.flags |= LAYOUT_RELATIVE_TO_X | LAYOUT_RELATION_X_INCLUDE_WIDTH;
    }
    const Drawable_RectangleData_t rect_data = {.border_radius_em = BORDER_RADIUS_AUTO,
                                                .color = {.r = 100, .g = 100, .b = 100, .a = 100}};
    Drawable_t *rect = ui_make_rectangle(menu->ui, &rect_data, container, &rect_layout);

    // Reposition text inside the rect
    text->layout.relative_to = rect;
    text->layout.flags |= LAYOUT_PROPORTIONAL_POS | LAYOUT_PROPORTIONAL_POS_TO_RELATIVE | LAYOUT_RELATIVE_TO_POS |
                          LAYOUT_ANCHOR_CENTER_X | LAYOUT_ANCHOR_CENTER_Y;
    text->layout.offset_x = text->layout.offset_y = 0.5;
    ui_reposition_drawable(text);

    return rect;
}

static Container_t *setup_tag_pills(const MainMenu_t *menu, const MenuSong_t *song, const SongEntryDrawables_t *entry) {
    const Layout_t layout = {
        .flags = LAYOUT_PROPORTIONAL_POS | LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_RELATIVE_TO_SIZE,
        .offset_y = PILL_REGULAR_OFFSET_Y,
        .width = 1.0,
        .height = 1.0,
        .relative_to = entry->image,
        .relative_to_size = entry->image,
    };
    Container_t *pills_container = ui_make_container(menu->ui, menu->container, &layout, CONTAINER_HORIZONTAL_ALIGN_CONTENT);
    const Animation_EaseTranslationData_t translation_data = {
        .duration = ALBUM_SCALE_DURATION,
    };
    ui_container_animate_translation(pills_container, &translation_data);

    const Drawable_t *prev_pill = NULL;
    StrBuffer_t *buf = str_buf_init();
    if ( !str_is_empty(song->tags) ) {
        char *tags = song->tags;
        char *tags_end = tags + strlen(tags);
        while ( tags < tags_end ) {
            char *next = strchr(tags, ',');
            str_buf_clear(buf);
            str_buf_append(buf, tags, next);
            if ( next == NULL )
                tags = tags_end;
            else
                tags = next + 1;

            prev_pill = setup_single_tag_pill(menu, buf->data, prev_pill, pills_container);
        }
    }

    str_buf_destroy(buf);
    return pills_container;
}

static Drawable_t *setup_song_album_text(const MainMenu_t *menu, const MenuSong_t *song, const Drawable_t *title) {
    const Layout_t layout = {
        .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_X_POS_TO_RELATIVE |
                 LAYOUT_CENTER_X | LAYOUT_PROPORTIONAL_Y,
        .relative_to = title,
        .offset_y = 0.005,
    };
    char *str;
    if ( song->year != 0 ) {
        asprintf(&str, "%s | %s | %d", song->album, song->artist, song->year);
    } else {
        asprintf(&str, "%s | %s", song->album, song->artist);
    }

    const Drawable_TextData_t data = {.text = str, .color = {.r = 255, .g = 255, .b = 255, .a = 255}, .em = 0.5};
    Drawable_t *text = ui_make_text(menu->ui, &data, menu->container, &layout);
    text->center_on_scale = true;
    ui_drawable_set_alpha_immediate(text, 100);

    const Animation_ScaleData_t scale_data = {
        .duration = ALBUM_SCALE_DURATION,
    };
    ui_animate_scale(text, &scale_data);
    const Animation_EaseTranslationData_t translation_data = {.duration = ALBUM_SCALE_DURATION};
    ui_animate_translation(text, &translation_data);

    free(str);
    return text;
}

static void setup_album(const char *_, const void *value, void *userdata) {
    const MenuAlbum_t *album = value;
    MainMenu_t *menu = userdata;
    GridLayoutInfo_t *grid = &menu->grid_layout_info;

    for ( size_t i = 0; i < album->songs->size; i++ ) {
        const MenuSong_t *song = album->songs->data[i];
        Layout_t layout = {.flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_PROPORTIONAL_POS | LAYOUT_RELATIVE_TO_POS |
                                    LAYOUT_SPECIAL_KEEP_ASPECT_RATIO,
                           .width = 0.15};
        bool set_first = false;
        if ( grid->row_count == MAX_SONGS_PER_ROW || grid->first_in_row == NULL ) {
            grid->row_count = 0;
            layout.offset_x = 0.0;
            layout.offset_y = 0.15;
            layout.flags |= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
            layout.relative_to = grid->first_in_row;
            set_first = true;
        } else {
            layout.offset_x = 0.05;
            layout.offset_y = 0;
            layout.flags |= LAYOUT_RELATION_X_INCLUDE_WIDTH;
            layout.relative_to = grid->last_in_row;
        }

        const Drawable_ImageData_t data = {.border_radius_em = 1};
        const AlbumArtData_t *no_data = map_get(menu->album_arts, NO_ART_KEY);
        if ( no_data == NULL ) {
            error_abort("Fatal error: Couldn't load the default album image");
        }

        Drawable_t *image =
            ui_make_image(no_data->image_data, (int)no_data->image_data_size, &data, menu->container, &layout);
        grid->last_in_row = image;
        if ( set_first ) {
            grid->first_in_row = image;
        }
        grid->row_count += 1;

        const Animation_ScaleData_t scale_data = {
            .duration = ALBUM_SCALE_DURATION,
        };
        image->center_on_scale = true;
        ui_animate_scale(image, &scale_data);

        SongEntryDrawables_t *entry = calloc(1, sizeof(*entry));
        Drawable_t *title_text = setup_song_title(menu, song, image);
        Drawable_t *album_text = setup_song_album_text(menu, song, title_text);

        entry->menu = menu;
        entry->key = song->id;
        entry->image = image;
        entry->title = title_text;
        entry->album = album_text;
        entry->pills_container = setup_tag_pills(menu, song, entry);

        // Setup events
        ui_add_event_callback(menu->ui, UI_EVENT_MOUSE_HOVER_ENTERED, image, on_album_art_event, entry);
        ui_add_event_callback(menu->ui, UI_EVENT_MOUSE_HOVER_EXITED, image, on_album_art_event, entry);
        ui_add_event_callback(menu->ui, UI_EVENT_MOUSE_CLICK, image, on_album_art_event, entry);

        grid->max_y = MAX(grid->max_y, album_text->bounds.y + album_text->bounds.h);

        map_put(menu->album_art_drawables, song->id, entry);
    }
}

static void setup_artist(const char *_, const void *value, void *userdata) {
    const MenuArtist_t *artist = value;

    map_iterate_const(artist->albums, setup_album, userdata);
}

static void update_background(const MainMenu_t *menu) {
    if ( str_is_empty(menu->selected_song) )
        return;

    const AlbumArtData_t *art = map_get(menu->album_arts, menu->selected_song);
    if ( art == NULL || art->image_data == NULL )
        return;

    const Container_t *root = ui_root_container(menu->ui);
    Color_t sampled_colors[5];
    for ( int i = 0; i < 5; i++ ) {
        sampled_colors[i] = art->sampled_colors[i];
    }
    if ( root->background->type == BACKGROUND_NONE )
        ui_container_update_background_colors_immediate(root, sampled_colors, 5);
    else
        ui_container_update_background_colors(root, sampled_colors, 5);

    root->background->type = BACKGROUND_AM_LIKE_GRADIENT;
}

static void destroy_resource_load(Resource_t **resource) {
    if ( resource != NULL && *resource != NULL ) {
        repo_resource_destroy(*resource);
        *resource = NULL;
    }
}

static void free_setup_resource_loads(MainMenu_t *menu) {
    destroy_resource_load(&menu->load_ui_font);
    destroy_resource_load(&menu->load_song_list);
    destroy_resource_load(&menu->load_no_album_art);
}

static void on_settings_click(Ui_t *ui, const ButtonWidget_t *) {
    settings_show(ui);
}

void menu_setup(MainMenu_t *menu) {
    ui_container_animate_color_lerp(ui_root_container(menu->ui), 0.5, ANIM_EASE_NONE);
    // Set the initial background to the first album art loaded
    update_background(menu);
    etsuko_setup_version(menu->ui);

    if ( config_get()->settings.show_settings ) {
        const Layout_t layout = {
            .flags = LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_WRAP_AROUND_Y | LAYOUT_PROPORTIONAL_POS,
            .offset_x = 0.02,
            .offset_y = -0.02,
        };
        const ButtonWidgetOpts_t opts = {
            .text = "Settings",
            .bg_show_type = BUTTON_BG_SHOW_ALWAYS,
            .bg_color = {.r = 100, .g = 100, .b = 100, .a = 100},
            .text_color = {.r = 255, .g = 255, .b = 255, .a = 255},
            .text_em = 0.8
        };
        ButtonWidget_t *button = ui_build_button_widget(menu->ui, ui_root_container(menu->ui), &layout, &opts);
        button->on_click_callback = on_settings_click;
    }

    const Layout_t layout = {
        .flags = LAYOUT_PROPORTIONAL_Y | LAYOUT_PROPORTIONAL_SIZE, .offset_y = 0.0, .width = 1.0, .height = 1.0};
    menu->container = ui_make_container(menu->ui, ui_root_container(menu->ui), &layout, CONTAINER_HORIZONTAL_ALIGN_CONTENT);
    menu->container->overflow_y = (ContainerOverflow_t){
        .kind = OVERFLOW_SCROLL,
        .relative_end_padding = 0.15
    };
    ui_container_add_vertical_scrollbar(menu->ui, menu->container, SCROLL_BAR_ALWAYS);
    ui_container_animate_scroll_y(menu->container, 0.1, ANIM_EASE_OUT_CUBIC);

    map_iterate_const(menu->menu_artists, setup_artist, menu);

    free_setup_resource_loads(menu);
}

static void iterate_pending_art(const char *key, void *data, void *userdata) {
    const MainMenu_t *menu = userdata;
    AlbumArtData_t *art = data;

    if ( art->state == ART_PENDING_DRAW ) {
        const SongEntryDrawables_t *entry = map_get(menu->album_art_drawables, key);
        if ( entry == NULL )
            error_abort("Failed to find drawable for album art");
        Drawable_t *image = entry->image;
        ui_drawable_set_image(image, art->image_data, (int)art->image_data_size);
        art->state = ART_OK;
    }
}

AppStatus_t menu_loop(MainMenu_t *menu) {
    events_loop();
    if ( events_has_quit() )
        return APP_STATUS_FAILURE;

    map_iterate(menu->album_arts, iterate_pending_art, menu);
    if ( menu->album_data_dirty ) {
        update_background(menu);
        menu->album_data_dirty = false;
    }

    ui_begin_loop(menu->ui);
    events_frame_end();
    ui_draw(menu->ui);
    ui_end_loop();
    settings_on_frame_end(menu->ui);

    global_update();
    return APP_STATUS_OK;
}

static void cleanup_album_art_data(const char *key, void *value, void *_) {
    AlbumArtData_t *data = value;
    if ( str_equals(key, NO_ART_KEY) ) {
        free(data->image_data);
    }
    // The other ones are owned by the Resource_t which is freed separately
    free(data);
}

static void cleanup_drawables_map(const char *key, void *value, void *_) {
    SongEntryDrawables_t *entry = value;
    // Drawables are owned by the UI, so we just free the container struct
    free(entry);
}

void menu_finish(const MainMenu_t *menu) {
    ui_finish(menu->ui);

    if ( menu->menu_artists )
        menu_songs_destroy(menu->menu_artists);

    if ( menu->album_art_loads ) {
        for ( size_t i = 0; i < menu->album_art_loads->size; i++ ) {
            repo_resource_destroy(menu->album_art_loads->data[i]);
        }
        vec_destroy(menu->album_art_loads);
    }

    if ( menu->album_arts ) {
        map_iterate(menu->album_arts, cleanup_album_art_data, NULL);
        map_destroy(menu->album_arts);
    }

    if ( menu->album_art_drawables ) {
        map_iterate(menu->album_art_drawables, cleanup_drawables_map, NULL);
        map_destroy(menu->album_art_drawables);
    }
}
