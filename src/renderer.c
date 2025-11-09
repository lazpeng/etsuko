/**
 * renderer.c - OpenGL-based rendering backend
 */

#include "renderer.h"

#include "constants.h"
#include "contrib/incbin.h"
#include "error.h"
#include "events.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#define GLSL_VERSION "#version 300 es\n"
#define GLSL_PRECISION "precision mediump float;\n"
#else
#include <GL/glew.h>
#define GLSL_VERSION "#version 330 core\n"
#define GLSL_PRECISION ""
#endif

// 1MB
constexpr int MAX_SHADER_SIZE = 1 * 1024 * 1024;
constexpr int QUAD_VERTICES_SIZE = 4 /*points*/ * 3 /*vertices per triangle*/ * 2 /*triangles*/;
constexpr int PROJECTION_MATRIX_SIZE = 16;

typedef struct Renderer_t {
    SDL_Window *window;
    SDL_GLContext gl_context;
    Bounds_t viewport;
    TTF_Font *ui_font, *lyrics_font;
    double h_dpi, v_dpi;
    Color_t bg_color, bg_color_secondary;
    Color_t draw_color;
    RenderTarget_t *render_target;
    BlendMode_t blend_mode;
    bool rendering_to_fbo;
    double window_pixel_scale;
    Texture_t *bg_texture;
    BackgroundType_t bg_type;

    // OpenGL objects
    GLuint VAO, VBO;
    GLuint texture_shader;
    GLuint rect_shader;
    GLuint gradient_shader;
    GLuint dyn_gradient_shader;
    GLuint rand_gradient_shader;
    GLuint blur_shader;
    GLuint copy_shader;
    float projection_matrix[PROJECTION_MATRIX_SIZE];

    // Shader uniform locations
    GLint tex_projection_loc;
    GLint tex_alpha_loc;
    GLint tex_bounds_loc;
    GLint tex_border_radius_loc;
    GLint tex_rect_size_loc;
    GLint rect_projection_loc;
    GLint rect_color_loc;
    GLint rect_pos_loc;
    GLint rect_size_loc;
    GLint rect_radius_loc;
    GLint gradient_top_color_loc;
    GLint gradient_bottom_color_loc;
    GLint gradient_projection_loc;
    GLint frost_time_loc;
    GLint blur_texture_loc;
    GLint blur_direction_loc;
    GLint blur_size_loc;
    GLint blur_projection_loc;
    GLint rand_grad_time_loc;
    GLint rand_grad_noise_scale_loc;
    GLint rand_grad_resolution_loc;
} Renderer_t;

static Renderer_t *g_renderer = nullptr;

// ============================================================================
// SHADERS
// ============================================================================

#if defined __EMSCRIPTEN__
static const char incbin_texture_vert_shader[] = {
#embed "shaders/texture.vert.glsl"
};
static const char incbin_texture_frag_shader[] = {
#embed "shaders/texture.frag.glsl"
};

static const char incbin_rect_vert_shader[] = {
#embed "shaders/rect.vert.glsl"
};
static const char incbin_rect_frag_shader[] = {
#embed "shaders/rect.frag.glsl"
};

static const char incbin_copy_vert_shader[] = {
#embed "shaders/copy.vert.glsl"
};
static const char incbin_copy_frag_shader[] = {
#embed "shaders/copy.frag.glsl"
};

static const char incbin_gradient_vert_shader[] = {
#embed "shaders/gradient.vert.glsl"
};
static const char incbin_gradient_frag_shader[] = {
#embed "shaders/gradient.frag.glsl"
};

static const char incbin_dyn_gradient_vert_shader[] = {
#embed "shaders/dynamic gradient.vert.glsl"
};
static const char incbin_dyn_gradient_frag_shader[] = {
#embed "shaders/dynamic gradient.frag.glsl"
};

static const char incbin_blur_vert_shader[] = {
#embed "shaders/blur.vert.glsl"
};
static const char incbin_blur_frag_shader[] = {
#embed "shaders/blur.frag.glsl"
};

static const char incbin_rand_gradient_vert_shader[] = {
#embed "shaders/random gradient.vert.glsl"
};
static const char incbin_rand_gradient_frag_shader[] = {
#embed "shaders/random gradient.frag.glsl"
};

#else
INCBIN(texture_vert_shader, "shaders/texture.vert.glsl")
INCBIN(texture_frag_shader, "shaders/texture.frag.glsl")

INCBIN(rect_vert_shader, "shaders/rect.vert.glsl")
INCBIN(rect_frag_shader, "shaders/rect.frag.glsl")

INCBIN(gradient_vert_shader, "shaders/gradient.vert.glsl")
INCBIN(gradient_frag_shader, "shaders/gradient.frag.glsl")

INCBIN(dyn_gradient_vert_shader, "shaders/dynamic gradient.vert.glsl")
INCBIN(dyn_gradient_frag_shader, "shaders/dynamic gradient.frag.glsl")

INCBIN(rand_gradient_vert_shader, "shaders/random gradient.vert.glsl")
INCBIN(rand_gradient_frag_shader, "shaders/random gradient.frag.glsl")

INCBIN(blur_vert_shader, "shaders/blur.vert.glsl")
INCBIN(blur_frag_shader, "shaders/blur.frag.glsl")

INCBIN(copy_vert_shader, "shaders/copy.vert.glsl")
INCBIN(copy_frag_shader, "shaders/copy.frag.glsl")
#endif

// ============================================================================
// SHADER HELPERS
// ============================================================================

static GLuint compile_shader(const GLenum type, const char *source, const char *name) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if ( !success ) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        printf("Shader compilation failed for %s:\n%s\n", name, log);
        printf("source: %s\n", source);
        error_abort("Shader compilation failed");
    }

    return shader;
}

static char *begin_shader_compilation(void) {
    char *buffer = calloc(1, MAX_SHADER_SIZE);
    if ( buffer == nullptr ) {
        error_abort("Failed to allocate shader compilation buffer");
    }
    return buffer;
}

static const char *process_shader_file(char *buffer, const char *contents) {
    snprintf(buffer, MAX_SHADER_SIZE, "%s\n%s\n%s", GLSL_VERSION, GLSL_PRECISION, contents);
    return buffer;
}

static void end_shader_compilation(char *buff) { free(buff); }

static GLuint create_shader_program(char *buffer, const char *vert_src, const char *frag_src, const char *program_name) {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, process_shader_file(buffer, vert_src), program_name);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, process_shader_file(buffer, frag_src), program_name);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if ( !success ) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        printf("Shader linking failed:\n%s\n", log);
        error_abort("Shader linking failed");
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

static void create_orthographic_matrix(const float left, const float right, const float bottom, const float top, float *matrix) {
    memset(matrix, 0, PROJECTION_MATRIX_SIZE * sizeof(float));
    matrix[0] = 2.0f / (right - left);
    matrix[5] = 2.0f / (top - bottom);
    matrix[10] = -1.0f;
    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[15] = 1.0f;
}

static void create_quad_vertices(const float x, const float y, const float w, const float h, float *vertices) {
    const float canon_vertices[] = {x, (y + h), 0.0f, 1.0f, x,       y, 0.0f, 0.0f, (x + w), y,       1.0f, 0.0f,
                                    x, (y + h), 0.0f, 1.0f, (x + w), y, 1.0f, 0.0f, (x + w), (y + h), 1.0f, 1.0f};

    memcpy(vertices, canon_vertices, sizeof(canon_vertices));
}

static void update_projection_matrix(void) {
    const float w = (float)g_renderer->viewport.w;
    const float h = (float)g_renderer->viewport.h;

    create_orthographic_matrix(0.f, w, h, 0.f, g_renderer->projection_matrix);
}

static Texture_t *create_texture_from_surface(SDL_Surface *surface) {
    SDL_Surface *rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    if ( rgba_surface == nullptr ) {
        error_abort("Failed to convert surface to ABGR8888");
    }

    Texture_t *texture = calloc(1, sizeof(*texture));
    if ( texture == nullptr ) {
        error_abort("Failed to allocate texture");
    }

    texture->width = rgba_surface->w;
    texture->height = rgba_surface->h;

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba_surface->w, rgba_surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_surface->pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    SDL_FreeSurface(rgba_surface);
    texture->id = texture_id;

    return texture;
}

void render_init(void) {
    if ( g_renderer != nullptr ) {
        printf("Warning: renderer already initialized\n");
        return;
    }

    g_renderer = calloc(1, sizeof(*g_renderer));
    if ( g_renderer == nullptr ) {
        error_abort("Failed to allocate renderer");
    }

#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    constexpr int pos = SDL_WINDOWPOS_CENTERED;
    constexpr int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
    g_renderer->window = SDL_CreateWindow(DEFAULT_TITLE, pos, pos, DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if ( g_renderer->window == nullptr ) {
        error_abort("Failed to create window");
    }

    g_renderer->gl_context = SDL_GL_CreateContext(g_renderer->window);
    if ( g_renderer->gl_context == nullptr ) {
        printf("SDL Error: %s\n", SDL_GetError());

        error_abort("Failed to create OpenGL context");
    }

#ifndef __EMSCRIPTEN__
    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if ( err != GLEW_OK ) {
        printf("GLEW Error: %s\n", (char *)glewGetErrorString(err));
        error_abort("Failed to initialize GLEW");
    }
    glGetError();
    SDL_GL_SetSwapInterval(1);
#endif

    g_renderer->bg_color = (Color_t){0, 0, 0, 255};
    g_renderer->draw_color = (Color_t){255, 255, 255, 255};
    g_renderer->bg_type = BACKGROUND_NONE;

#ifdef __EMSCRIPTEN__
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    const int32_t width = (int32_t)cssWidth;
    const int32_t height = (int32_t)cssHeight;
    SDL_SetWindowSize(g_renderer->window, width, height);
#endif

    render_on_window_changed();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Create VAO and VBO
    glGenVertexArrays(1, &g_renderer->VAO);
    glGenBuffers(1, &g_renderer->VBO);

    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);

    // Allocate buffer (4 vertices * 4 floats per vertex: x, y, u, v)
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 4, nullptr, GL_DYNAMIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    // TexCoord attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glBindVertexArray(0);

    // Compile shaders
    char *buffer = begin_shader_compilation();
    g_renderer->texture_shader = create_shader_program(buffer, incbin_texture_vert_shader, incbin_texture_frag_shader, "tex");
    g_renderer->rect_shader = create_shader_program(buffer, incbin_rect_vert_shader, incbin_rect_frag_shader, "rect");
    g_renderer->gradient_shader =
        create_shader_program(buffer, incbin_gradient_vert_shader, incbin_gradient_frag_shader, "gradient");
    g_renderer->dyn_gradient_shader =
        create_shader_program(buffer, incbin_dyn_gradient_vert_shader, incbin_dyn_gradient_frag_shader, "dyn_gradient");
    g_renderer->rand_gradient_shader =
        create_shader_program(buffer, incbin_rand_gradient_vert_shader, incbin_rand_gradient_frag_shader, "rand_gradient");
    g_renderer->blur_shader = create_shader_program(buffer, incbin_blur_vert_shader, incbin_blur_frag_shader, "blur");
    g_renderer->copy_shader = create_shader_program(buffer, incbin_copy_vert_shader, incbin_copy_frag_shader, "copy");
    end_shader_compilation(buffer);

    // Get uniform locations for texture shader
    g_renderer->tex_projection_loc = glGetUniformLocation(g_renderer->texture_shader, "projection");
    g_renderer->tex_alpha_loc = glGetUniformLocation(g_renderer->texture_shader, "alpha");
    g_renderer->tex_bounds_loc = glGetUniformLocation(g_renderer->texture_shader, "bounds");
    g_renderer->tex_border_radius_loc = glGetUniformLocation(g_renderer->texture_shader, "borderRadius");
    g_renderer->tex_rect_size_loc = glGetUniformLocation(g_renderer->texture_shader, "rectSize");

    // Get uniform locations for rect shader
    g_renderer->rect_projection_loc = glGetUniformLocation(g_renderer->rect_shader, "projection");
    g_renderer->rect_color_loc = glGetUniformLocation(g_renderer->rect_shader, "color");
    g_renderer->rect_pos_loc = glGetUniformLocation(g_renderer->rect_shader, "rectPos");
    g_renderer->rect_size_loc = glGetUniformLocation(g_renderer->rect_shader, "rectSize");
    g_renderer->rect_radius_loc = glGetUniformLocation(g_renderer->rect_shader, "cornerRadius");

    // Get uniform locations for the gradient shader
    g_renderer->gradient_top_color_loc = glGetUniformLocation(g_renderer->gradient_shader, "topColor");
    g_renderer->gradient_bottom_color_loc = glGetUniformLocation(g_renderer->gradient_shader, "bottomColor");
    g_renderer->gradient_projection_loc = glGetUniformLocation(g_renderer->gradient_shader, "projection");

    // Get uniform locations for the random gradient shader
    g_renderer->rand_grad_time_loc = glGetUniformLocation(g_renderer->rand_gradient_shader, "uTime");
    g_renderer->rand_grad_noise_scale_loc = glGetUniformLocation(g_renderer->rand_gradient_shader, "uNoiseScale");
    g_renderer->rand_grad_resolution_loc = glGetUniformLocation(g_renderer->rand_gradient_shader, "uResolution");

    // Get uniform locations for blur shader
    g_renderer->blur_texture_loc = glGetUniformLocation(g_renderer->blur_shader, "u_texture");
    g_renderer->blur_direction_loc = glGetUniformLocation(g_renderer->blur_shader, "u_direction");
    g_renderer->blur_size_loc = glGetUniformLocation(g_renderer->blur_shader, "u_blur_size");
    g_renderer->blur_projection_loc = glGetUniformLocation(g_renderer->blur_shader, "u_projection");
}

void render_finish(void) {
    if ( g_renderer == nullptr ) {
        return;
    }

    // Unload fonts
    if ( g_renderer->ui_font != nullptr )
        TTF_CloseFont(g_renderer->ui_font);
    if ( g_renderer->lyrics_font != nullptr )
        TTF_CloseFont(g_renderer->lyrics_font);

    // Delete OpenGL objects
    glDeleteProgram(g_renderer->texture_shader);
    glDeleteProgram(g_renderer->rect_shader);
    glDeleteProgram(g_renderer->gradient_shader);
    glDeleteProgram(g_renderer->dyn_gradient_shader);
    glDeleteProgram(g_renderer->rand_gradient_shader);
    glDeleteProgram(g_renderer->blur_shader);
    glDeleteBuffers(1, &g_renderer->VBO);
    glDeleteVertexArrays(1, &g_renderer->VAO);

    // Destroy GL context
    SDL_GL_DeleteContext(g_renderer->gl_context);
    SDL_DestroyWindow(g_renderer->window);

    // Cleanup
    free(g_renderer);
    g_renderer = nullptr;
}

void render_on_window_changed(void) {
    int32_t outW, outH;
    SDL_GL_GetDrawableSize(g_renderer->window, &outW, &outH);

    int32_t window_w;
    SDL_GetWindowSize(g_renderer->window, &window_w, nullptr);

    g_renderer->window_pixel_scale = (double)outW / (double)window_w;

    events_set_window_pixel_scale(g_renderer->window_pixel_scale);

    float hdpi_temp, v_dpi_temp;
    if ( SDL_GetDisplayDPI(0, nullptr, &hdpi_temp, &v_dpi_temp) != 0 ) {
        puts(SDL_GetError());
        error_abort("Failed to get DPI");
    }
    g_renderer->h_dpi = hdpi_temp;
    g_renderer->v_dpi = v_dpi_temp;

    g_renderer->viewport = (Bounds_t){.x = 0, .y = 0, .w = (double)outW, .h = (double)outH};

    glViewport(0, 0, outW, outH);
    update_projection_matrix();

    // Update background texture
    if ( g_renderer->bg_texture != nullptr ) {
        render_destroy_texture(g_renderer->bg_texture);
        g_renderer->bg_texture = nullptr;
    }
}

static void deconstruct_colors_opengl(const Color_t *color, float *r, float *g, float *b, float *a) {
    *r = (float)color->r / 255.0f;
    *g = (float)color->g / 255.0f;
    *b = (float)color->b / 255.0f;
    *a = (float)color->a / 255.0f;
}

static float hueToRgb(const float p, const float q, float t) {
    if ( t < 0 )
        t += 1;
    if ( t > 1 )
        t -= 1;
    if ( t < 1.f / 6 )
        return p + (q - p) * 6 * t;
    if ( t < 1.f / 2 )
        return q;
    if ( t < 2.f / 3 )
        return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
}

static void hslToRgb(const float h, const float s, const float l, float *dst) {
    float r, g, b;

    if ( s == 0 ) {
        r = g = b = l;
    } else {
        const auto q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const auto p = 2 * l - q;
        r = hueToRgb(p, q, h + 1.f / 3);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.f / 3);
    }

    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
}

static Texture_t *internal_create_random_gradient_background_texture() {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    constexpr float rate = 0.005f;
    static float progress = 0.f;
    static float noise_magnitude = 0.1f;

    constexpr float target_magnitude = 0.2f;

    progress += (float)(rate * events_get_delta_time());
    noise_magnitude = noise_magnitude * (1.f - progress) + target_magnitude * progress;

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const auto target = render_make_texture_target(width, height);

    glUseProgram(g_renderer->rand_gradient_shader);

    glUniform1f(g_renderer->rand_grad_time_loc, (float)events_get_elapsed_time());
    glUniform1f(g_renderer->rand_grad_noise_scale_loc, noise_magnitude);
    glUniform2f(g_renderer->rand_grad_resolution_loc, (float)width, (float)height);

    constexpr float quadVertices[] = {-1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
                                      -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};

    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const auto result = target->texture;
    render_restore_texture_target();
    render_set_blend_mode(saved_blend);

    return result;
}

static Texture_t *internal_create_dynamic_gradient_background_texture() {
    const auto saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    constexpr float rate = 0.005f;
    static float progress = 0.f;
    static float noise_magnitude = 0.1f;

    constexpr float target_magnitude = 0.2f;

    progress += (float)(rate * events_get_delta_time());
    noise_magnitude = noise_magnitude * (1.f - progress) + target_magnitude * progress;

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const auto target = render_make_texture_target(width, height);

    static bool colors_initialized = false;
    static float colors[5][3] = {
        {0.5f, 0.5f, 0.5f}, {0.2f, 0.5f, 0.1f}, {0.7f, 0.8f, 0.2f}, {0.3f, 0.2f, 0.1f}, {0.1f, 0.4f, 0.7f},
    };
    if ( !colors_initialized ) {
        memset(colors, 0, sizeof(colors));
        srand(time(nullptr)); // NOLINT(*-msc51-cpp)
        for ( int i = 0; i < 5; i++ ) {
            const float h = (float)(rand() % 255) / 255.f;               // NOLINT(*-msc50-cpp)
            const float s = (float)(rand() % 255) / 255.f * 0.2f + 0.3f; // NOLINT(*-msc50-cpp)
            const float l = (float)(rand() % 255) / 255.f * 0.2f + 0.7f; // NOLINT(*-msc50-cpp)
            hslToRgb(h, s, l, &colors[i][0]);
        }
        colors_initialized = true;
    }

    glUseProgram(g_renderer->dyn_gradient_shader);

    glUniform1f(glGetUniformLocation(g_renderer->dyn_gradient_shader, "u_time"), (float)events_get_elapsed_time());
    glUniform1f(glGetUniformLocation(g_renderer->dyn_gradient_shader, "u_noise_magnitude"), noise_magnitude);
    glUniform3fv(glGetUniformLocation(g_renderer->dyn_gradient_shader, "u_colors"), 5, &colors[0][0]);

    // float quadVertices[QUAD_VERTICES_SIZE] = {0};
    // create_quad_vertices(0, 0, (float)width, (float)height, quadVertices);

    constexpr float quadVertices[] = {-1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
                                      -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const auto result = target->texture;
    render_restore_texture_target();
    render_set_blend_mode(saved_blend);

    return result;
}

static Texture_t *internal_create_gradient_background_texture() {
    const auto saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const auto target = render_make_texture_target(width, height);

    glUseProgram(g_renderer->gradient_shader);

    const float w = (float)width, h = (float)height;

    float r, g, b, a;
    deconstruct_colors_opengl(&g_renderer->bg_color, &r, &g, &b, &a);
    glUniform4f(g_renderer->gradient_top_color_loc, r, g, b, a);
    deconstruct_colors_opengl(&g_renderer->bg_color_secondary, &r, &g, &b, &a);
    glUniform4f(g_renderer->gradient_bottom_color_loc, r, g, b, a);
    glUniformMatrix4fv(g_renderer->gradient_projection_loc, 1, GL_FALSE, target->projection);

    float quadVertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices(0, 0, w, h, quadVertices);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const auto texture = target->texture;

    render_restore_texture_target();
    render_set_blend_mode(saved_blend);
    return texture;
}

Texture_t *render_blur_texture(const Texture_t *source, const float blur_radius) {
    if ( !source || blur_radius <= 0 || source->width <= 0 || source->height <= 0 ) {
        error_abort("Fail at render_blur_texture_radius");
    }

    const auto saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = source->width, height = source->height;
    const auto first_target = render_make_texture_target(width, height);

    const float w = (float)source->width;
    const float h = (float)source->height;

    glUseProgram(g_renderer->blur_shader);
    glUniformMatrix4fv(g_renderer->blur_projection_loc, 1, GL_FALSE, first_target->projection);
    glUniform1f(g_renderer->blur_size_loc, blur_radius);
    glUniform2f(g_renderer->blur_direction_loc, 1.0f, 0.0f);

    glBindTexture(GL_TEXTURE_2D, source->id);

    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices(0, 0, w, h, vertices);

    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const auto vertical_texture = first_target->texture;
    render_restore_texture_target();

    const auto second_target = render_make_texture_target(width, height);
    glUniform2f(g_renderer->blur_direction_loc, 0.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, vertical_texture->id);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const auto horizontal_texture = second_target->texture;
    horizontal_texture->border_radius = source->border_radius;
    render_restore_texture_target();
    render_destroy_texture(vertical_texture);

    // Restore
    render_set_blend_mode(saved_blend);
    return horizontal_texture;
}

Texture_t *render_blur_texture_replace(Texture_t *source, const float blur_radius) {
    const auto blurred = render_blur_texture(source, blur_radius);
    render_destroy_texture(source);
    return blurred;
}

void render_clear() {
    float r, g, b, a;
    deconstruct_colors_opengl(&g_renderer->bg_color, &r, &g, &b, &a);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if ( g_renderer->bg_type == BACKGROUND_GRADIENT ) {
        if ( g_renderer->bg_texture == nullptr ) {
            g_renderer->bg_texture = internal_create_gradient_background_texture();
        }
        render_draw_texture(g_renderer->bg_texture, &(Bounds_t){0}, 255);
    } else if ( g_renderer->bg_type == BACKGROUND_DYNAMIC_GRADIENT ) {
        auto bg_texture = internal_create_dynamic_gradient_background_texture();
        bg_texture = render_blur_texture_replace(bg_texture, 50);

        render_draw_texture(bg_texture, &(Bounds_t){0}, 255);

        render_destroy_texture(bg_texture);
    } else if ( g_renderer->bg_type == BACKGROUND_RANDOM_GRADIENT ) {
        const auto bg_texture = internal_create_random_gradient_background_texture();
        render_draw_texture(bg_texture, &(Bounds_t){0}, 255);

        render_destroy_texture(bg_texture);
    }
}

void render_present(void) { SDL_GL_SwapWindow(g_renderer->window); }

const Bounds_t *render_get_viewport(void) { return &g_renderer->viewport; }

double render_get_pixel_scale(void) { return g_renderer->window_pixel_scale; }

void render_load_font(const char *path, const FontType_t type) {
    TTF_Font *font = TTF_OpenFontDPI(path, DEFAULT_PT, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->h_dpi);

    if ( font == nullptr ) {
        puts(TTF_GetError());
        error_abort("Could not load font");
    }

    TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
    TTF_SetFontKerning(font, 1);

    if ( type == FONT_UI ) {
        g_renderer->ui_font = font;
    } else if ( type == FONT_LYRICS ) {
        g_renderer->lyrics_font = font;
    } else {
        error_abort("Invalid font kind");
    }
}

void render_set_window_title(const char *title) { SDL_SetWindowTitle(g_renderer->window, title); }

void render_measure_text_size(const char *text, const int32_t pt, int32_t *w, int32_t *h, const FontType_t kind) {
    TTF_Font *font = kind == FONT_UI ? g_renderer->ui_font : g_renderer->lyrics_font;
    if ( TTF_SetFontSizeDPI(font, pt, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->v_dpi) != 0 ) {
        error_abort("Failed to set font size/DPI");
    }

    if ( TTF_SizeUTF8(font, text, w, h) != 0 ) {
        error_abort("Failed to measure line size");
    }
}

int32_t render_measure_pt_from_em(const double em) {
    const double scale = g_renderer->viewport.w / DEFAULT_WIDTH;
    const double rem = fmax(12.0, round(DEFAULT_PT * scale));
    const double pixels = em * rem;
    const int32_t pt_size = (int32_t)lround(pixels * 72.0 / g_renderer->h_dpi);
    return pt_size;
}

static void get_render_params(float **projection, GLuint *fbo) {
    GLuint t_fbo;
    if ( g_renderer->render_target == nullptr ) {
        t_fbo = 0;
    } else {
        t_fbo = g_renderer->render_target->fbo;
    }

    if ( projection != nullptr )
        *projection =
            g_renderer->render_target == nullptr ? g_renderer->projection_matrix : g_renderer->render_target->projection;

    if ( fbo != nullptr )
        *fbo = t_fbo;
}

const RenderTarget_t *render_make_texture_target(const int32_t width, const int32_t height) {
    RenderTarget_t *target = calloc(1, sizeof(*target));
    if ( target == nullptr ) {
        error_abort("Failed to allocate render target");
    }

    target->texture = calloc(1, sizeof(*target->texture));
    if ( target->texture == nullptr ) {
        error_abort("Failed to allocate texture for render target");
    }

    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    target->texture->width = width;
    target->texture->height = height;
    target->prev_target = g_renderer->render_target;
    g_renderer->render_target = target;

    glGenFramebuffers(1, &target->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);

    glGenTextures(1, &target->texture->id);

    glBindTexture(GL_TEXTURE_2D, target->texture->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture->id, 0);

    glViewport(0, 0, width, height);

    create_orthographic_matrix(0.0f, (float)width, 0.0f, (float)height, target->projection);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    render_set_blend_mode(saved_blend);

    return target;
}

void render_restore_texture_target(void) {
    if ( g_renderer->render_target == nullptr ) {
        error_abort("No render target to restore");
    }

    RenderTarget_t *current = g_renderer->render_target;
    RenderTarget_t *prev = current->prev_target;
    g_renderer->render_target = prev;

    // Bind appropriate framebuffer
    if ( prev == nullptr ) {
        glViewport(0, 0, (int32_t)g_renderer->viewport.w, (int32_t)g_renderer->viewport.h);
        // Restore default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glViewport(prev->viewport[0], prev->viewport[1], prev->viewport[2], prev->viewport[3]);
        // Bind previous FBO
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer->render_target->fbo);
    }

    glDeleteFramebuffers(1, &current->fbo);
    // Assume the texture is going to be freed on its own, or else there's no point to making a
    // render target in the first place.
    free(current);
}

void render_destroy_texture(Texture_t *texture) {
    glDeleteTextures(1, &texture->id);
    free(texture);
}

Color_t render_color_parse(const uint32_t color) {
    const uint8_t a = color >> 24;
    const uint8_t r = color >> 16;
    const uint8_t g = color >> 8;
    const uint8_t b = color & 0xFF;
    return (Color_t){.r = r, .g = g, .b = b, .a = a};
}

Color_t render_color_darken(Color_t color) {
    constexpr double amount = 0.8;
    color.r = (uint8_t)fmax(0, color.r * amount);
    color.g = (uint8_t)fmax(0, color.g * amount);
    color.b = (uint8_t)fmax(0, color.b * amount);
    return color;
}

void render_set_bg_color(const Color_t color) {
    g_renderer->bg_color = color;
    g_renderer->bg_type = BACKGROUND_NONE;
}

void render_set_bg_gradient(const Color_t top_color, const Color_t bottom_color, BackgroundType_t type) {
    g_renderer->bg_color = top_color;
    g_renderer->bg_color_secondary = bottom_color;
    g_renderer->bg_type = type;
}

void render_set_blend_mode(const BlendMode_t mode) {
    g_renderer->blend_mode = mode;

    switch ( mode ) {
    case BLEND_MODE_BLEND:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case BLEND_MODE_ADD:
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case BLEND_MODE_NONE:
        glDisable(GL_BLEND);
        break;
    default:
        error_abort("Invalid blend mode");
    }
}

BlendMode_t render_get_blend_mode(void) { return g_renderer->blend_mode; }

Texture_t *render_make_text(const char *text, const int32_t pt_size, const bool bold, const Color_t *color,
                            const FontType_t font_type) {
    TTF_Font *font = font_type == FONT_UI ? g_renderer->ui_font : g_renderer->lyrics_font;
    if ( font == nullptr ) {
        error_abort("Font not loaded");
    }
    if ( strnlen(text, MAX_TEXT_SIZE) == 0 ) {
        error_abort("Text is empty");
    }

    if ( TTF_SetFontSizeDPI(font, pt_size, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->v_dpi) != 0 ) {
        puts(TTF_GetError());
        error_abort("Failed to set font size/DPI");
    }
    const int style = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
    TTF_SetFontStyle(font, style);

    const auto sdl_color = (SDL_Color){color->r, color->g, color->b, color->a};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, sdl_color);
    if ( surface == nullptr ) {
        puts(TTF_GetError());
        error_abort("Failed to render to surface");
    }

    Texture_t *texture = create_texture_from_surface(surface);
    SDL_FreeSurface(surface);

    return texture;
}

static Texture_t *create_test_texture(void) {
    constexpr int size = 256;
    unsigned char *pixels = malloc(size * size * 4);

    // Create a checkerboard pattern
    for ( int y = 0; y < size; y++ ) {
        for ( int x = 0; x < size; x++ ) {
            const int checker = (x / 32 + y / 32) % 2;
            const unsigned char color = checker ? 255 : 0;

            const int index = (y * size + x) * 4;
            pixels[index + 0] = color;
            pixels[index + 1] = color;
            pixels[index + 2] = color;
            pixels[index + 3] = 255;
        }
    }

    Texture_t *texture = calloc(1, sizeof(*texture));
    texture->width = size;
    texture->height = size;

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    free(pixels);
    texture->id = texture_id;

    return texture;
}

Texture_t *render_make_image(const char *file_path, const double border_radius_em) {
    SDL_Surface *loaded = IMG_Load(file_path);
    if ( loaded == nullptr ) {
        printf("IMG_GetError: %s\n", IMG_GetError());
        error_abort("Failed to load image");
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA8888, 0);
    if ( converted == nullptr ) {
        error_abort("Failed to convert image surface to appropriate pixel format");
    }
    SDL_FreeSurface(loaded);

    Texture_t *final_texture = create_texture_from_surface(converted);
    SDL_FreeSurface(converted);

    if ( border_radius_em > 0 ) {
        final_texture->border_radius = (float)render_measure_pt_from_em(border_radius_em);
    }

    return final_texture;
}

Texture_t *render_make_dummy_image(const double border_radius_em) {
    Texture_t *texture = create_test_texture();

    if ( border_radius_em > 0 ) {
        texture->border_radius = (float)render_measure_pt_from_em(border_radius_em);
    }

    return texture;
}

void render_draw_rounded_rect(const Bounds_t *bounds, const Color_t *color, const float border_radius) {
    if ( bounds->w <= 0 ) {
        return;
    }

    glUseProgram(g_renderer->rect_shader);

    float r, g, b, a;
    deconstruct_colors_opengl(color, &r, &g, &b, &a);
    glUniform4f(g_renderer->rect_color_loc, r, g, b, a);
    glUniform2f(g_renderer->rect_pos_loc, (float)bounds->x, (float)bounds->y);
    glUniform2f(g_renderer->rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->rect_radius_loc, border_radius);
    glUniformMatrix4fv(g_renderer->rect_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    const float padding = border_radius;
    const float vertices[] = {(float)bounds->x - padding,
                              (float)bounds->y - padding,
                              0.0f,
                              0.0f,
                              (float)(bounds->x + bounds->w) + padding,
                              (float)bounds->y - padding,
                              1.0f,
                              0.0f,
                              (float)(bounds->x + bounds->w) + padding,
                              (float)(bounds->y + bounds->h) + padding,
                              1.0f,
                              1.0f,
                              (float)bounds->x - padding,
                              (float)(bounds->y + bounds->h) + padding,
                              0.0f,
                              1.0f};

    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void render_draw_texture(const Texture_t *texture, const Bounds_t *at, const int32_t alpha_mod) {
    if ( texture == nullptr || texture->id == 0 ) {
        error_abort("Warning: Attempting to draw invalid texture\n");
    }

    const float w = (float)(at->w == 0 ? texture->width : at->w);
    const float h = (float)(at->w == 0 ? texture->height : at->h);

    if ( at->x + w < 0 || at->x > g_renderer->viewport.w || at->y + h < 0 || at->y > g_renderer->viewport.h ) {
        return;
    }

    float *projection;
    get_render_params(&projection, nullptr);

    glUseProgram(g_renderer->texture_shader);

    glUniform1f(g_renderer->tex_border_radius_loc, texture->border_radius);
    glUniform1f(g_renderer->tex_alpha_loc, (float)alpha_mod / 255.0f);
    glUniform2f(g_renderer->tex_rect_size_loc, w, h);
    glUniform4f(g_renderer->tex_bounds_loc, (float)at->x, (float)at->y, w, h);
    glUniformMatrix4fv(g_renderer->tex_projection_loc, 1, GL_FALSE, projection);

    glBindTexture(GL_TEXTURE_2D, texture->id);

    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices((float)at->x, (float)at->y, w, h, vertices);

    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
}
