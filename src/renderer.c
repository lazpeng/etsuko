/**
 * renderer.c - OpenGL-based rendering backend
 */

#include "renderer.h"

#include "constants.h"
#include "container_utils.h"
#include "error.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdbool.h>

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

typedef struct etsuko_Renderer_t {
    SDL_Window *window;
    SDL_GLContext gl_context;
    etsuko_Bounds_t viewport;
    TTF_Font *ui_font, *lyrics_font;
    double h_dpi, v_dpi;
    etsuko_Color_t bg_color;
    etsuko_Color_t draw_color;
    etsuko_RenderTarget_t *render_target;
    etsuko_BlendMode_t blend_mode;
    bool rendering_to_fbo;
    double window_pixel_scale;

    // OpenGL objects
    GLuint VAO, VBO;
    GLuint texture_shader;
    GLuint rect_shader;
    GLuint rounded_corners_shader;
    float projection_matrix[16];

    // Shader uniform locations
    GLint tex_projection_loc;
    GLint tex_alpha_loc;
    GLint rect_projection_loc;
    GLint rect_color_loc;
    GLint rect_pos_loc;
    GLint rect_size_loc;
    GLint rect_radius_loc;
} etsuko_Renderer_t;

static etsuko_Renderer_t *g_renderer = NULL;

// ============================================================================
// SHADERS
// ============================================================================

static const char *texture_vertex_shader =
    GLSL_VERSION GLSL_PRECISION "layout (location = 0) in vec2 position;\n"
                                "layout (location = 1) in vec2 texCoord;\n"
                                "out vec2 TexCoord;\n"
                                "uniform mat4 projection;\n"
                                "void main() {\n"
                                "    gl_Position = projection * vec4(position, 0.0, 1.0);\n"
                                "    TexCoord = texCoord;\n"
                                "}\n";

static const char *texture_fragment_shader =
    GLSL_VERSION GLSL_PRECISION "in vec2 TexCoord;\n"
                                "out vec4 FragColor;\n"
                                "uniform sampler2D tex;\n"
                                "uniform float alpha;\n"
                                "void main() {\n"
                                "    vec4 texColor = texture(tex, TexCoord);\n"
                                "    FragColor = vec4(texColor.rgb, texColor.a * alpha);\n"
                                "}\n";

static const char *rect_vertex_shader = GLSL_VERSION GLSL_PRECISION "layout (location = 0) in vec2 position;\n"
                                                                    "layout (location = 1) in vec2 texCoord;\n"
                                                                    "out vec2 FragPos;\n"
                                                                    "uniform mat4 projection;\n"
                                                                    "void main() {\n"
                                                                    "    gl_Position = projection * vec4(position, 0.0, 1.0);\n"
                                                                    "    FragPos = position;\n"
                                                                    "}\n";

static const char *rect_fragment_shader =
    GLSL_VERSION GLSL_PRECISION "in vec2 FragPos;\n"
                                "out vec4 FragColor;\n"
                                "uniform vec4 color;\n"
                                "uniform vec2 rectPos;\n"
                                "uniform vec2 rectSize;\n"
                                "uniform float cornerRadius;\n"
                                "\n"
                                "float roundedBoxSDF(vec2 centerPos, vec2 size, float radius) {\n"
                                "    return length(max(abs(centerPos) - size + radius, 0.0)) - radius;\n"
                                "}\n"
                                "\n"
                                "void main() {\n"
                                "    vec2 center = rectPos + rectSize * 0.5;\n"
                                "    vec2 pos = FragPos - center;\n"
                                "    float dist = roundedBoxSDF(pos, rectSize * 0.5, cornerRadius);\n"
                                "    float alpha = 1.0 - smoothstep(-1.0, 1.0, dist);\n"
                                "    FragColor = vec4(color.rgb, color.a * alpha);\n"
                                "}\n";

static const char *rounded_corners_vertex_shader =
    GLSL_VERSION GLSL_PRECISION "layout(location = 0) in vec2 aPos;\n"
                                "layout(location = 1) in vec2 aTexCoord;\n"
                                "out vec2 TexCoord;\n"
                                "out vec2 FragPos;\n"
                                "uniform mat4 uProjection;\n"
                                "uniform vec4 uBounds;\n"
                                "void main() {\n"
                                "    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);\n"
                                "    TexCoord = aTexCoord;\n"
                                "    FragPos = aTexCoord * uBounds.zw;\n"
                                "}\n";

static const char *rounded_corners_fragment_shader =
    GLSL_VERSION GLSL_PRECISION "in vec2 TexCoord;\n"
                                "in vec2 FragPos;\n"
                                "out vec4 FragColor;\n"
                                "uniform sampler2D uTexture;\n"
                                "uniform float uCornerRadius;\n"
                                "uniform vec2 uRectSize; // width, height\n"
                                "uniform int uRounded; // 0 or 1\n"
                                "void main() {\n"
                                "    vec4 texColor = texture(uTexture, TexCoord);\n"
                                "    // Distance from edges\n"
                                "    vec2 halfSize = uRectSize * 0.5;\n"
                                "    vec2 pos = FragPos - halfSize;\n"
                                "    // Distance to nearest corner (only outside the inner rectangle)\n"
                                "    vec2 cornerDist = max(vec2(0.0), abs(pos) - (halfSize - uCornerRadius));\n"
                                "    float dist = length(cornerDist);\n"
                                "    if (dist > uCornerRadius) {\n"
                                "        discard;\n"
                                "    }\n"
                                "    // Optional: smooth edges with anti-aliasing\n"
                                "    float alpha = 1.0 - smoothstep(uCornerRadius - 1.0, uCornerRadius, dist);\n"
                                "    texColor.a *= alpha;\n"
                                "    FragColor = texColor;\n"
                                "}\n";

// ============================================================================
// SHADER HELPERS
// ============================================================================

static GLuint compile_shader(const GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if ( !success ) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        printf("Shader compilation failed:\n%s\n", log);
        error_abort("Shader compilation failed");
    }

    return shader;
}

static GLuint create_shader_program(const char *vert_src, const char *frag_src) {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vert_src);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, frag_src);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if ( !success ) {
        char log[512];
        glGetProgramInfoLog(program, 512, NULL, log);
        printf("Shader linking failed:\n%s\n", log);
        error_abort("Shader linking failed");
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

// ============================================================================
// PROJECTION MATRIX
// ============================================================================

static void update_projection_matrix(void) {
    const float w = (float)g_renderer->viewport.w;
    const float h = (float)g_renderer->viewport.h;

    // Orthographic projection matrix
    // Maps (0, 0) to top-left, (w, h) to bottom-right
    g_renderer->projection_matrix[0] = 2.0f / w;
    g_renderer->projection_matrix[1] = 0.0f;
    g_renderer->projection_matrix[2] = 0.0f;
    g_renderer->projection_matrix[3] = 0.0f;

    g_renderer->projection_matrix[4] = 0.0f;
    g_renderer->projection_matrix[5] = -2.0f / h;
    g_renderer->projection_matrix[6] = 0.0f;
    g_renderer->projection_matrix[7] = 0.0f;

    g_renderer->projection_matrix[8] = 0.0f;
    g_renderer->projection_matrix[9] = 0.0f;
    g_renderer->projection_matrix[10] = -1.0f;
    g_renderer->projection_matrix[11] = 0.0f;

    g_renderer->projection_matrix[12] = -1.0f;
    g_renderer->projection_matrix[13] = 1.0f;
    g_renderer->projection_matrix[14] = 0.0f;
    g_renderer->projection_matrix[15] = 1.0f;
}

static etsuko_Texture_t *create_texture_from_surface(SDL_Surface *surface) {
    SDL_Surface *rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    if ( rgba_surface == NULL ) {
        error_abort("Failed to convert surface to ABGR8888");
    }

    etsuko_Texture_t *texture = calloc(1, sizeof(*texture));
    if ( texture == NULL ) {
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
    if ( g_renderer != NULL ) {
        render_finish();
        g_renderer = NULL;
    }

    g_renderer = calloc(1, sizeof(*g_renderer));
    if ( g_renderer == NULL ) {
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

    const int pos = SDL_WINDOWPOS_CENTERED;
    const int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
    g_renderer->window = SDL_CreateWindow(DEFAULT_TITLE, pos, pos, DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if ( g_renderer->window == NULL ) {
        error_abort("Failed to create window");
    }

    g_renderer->gl_context = SDL_GL_CreateContext(g_renderer->window);
    if ( g_renderer->gl_context == NULL ) {
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

    g_renderer->bg_color = (etsuko_Color_t){0, 0, 0, 255};
    g_renderer->draw_color = (etsuko_Color_t){255, 255, 255, 255};

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
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 4, NULL, GL_DYNAMIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);

    // TexCoord attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glBindVertexArray(0);

    // Compile shaders
    g_renderer->texture_shader = create_shader_program(texture_vertex_shader, texture_fragment_shader);
    g_renderer->rect_shader = create_shader_program(rect_vertex_shader, rect_fragment_shader);
    g_renderer->rounded_corners_shader = create_shader_program(rounded_corners_vertex_shader, rounded_corners_fragment_shader);

    // Get uniform locations for texture shader
    g_renderer->tex_projection_loc = glGetUniformLocation(g_renderer->texture_shader, "projection");
    g_renderer->tex_alpha_loc = glGetUniformLocation(g_renderer->texture_shader, "alpha");

    // Get uniform locations for rect shader
    g_renderer->rect_projection_loc = glGetUniformLocation(g_renderer->rect_shader, "projection");
    g_renderer->rect_color_loc = glGetUniformLocation(g_renderer->rect_shader, "color");
    g_renderer->rect_pos_loc = glGetUniformLocation(g_renderer->rect_shader, "rectPos");
    g_renderer->rect_size_loc = glGetUniformLocation(g_renderer->rect_shader, "rectSize");
    g_renderer->rect_radius_loc = glGetUniformLocation(g_renderer->rect_shader, "cornerRadius");

    // Get uniform locations for rounded corners shader
}

void render_finish(void) {
    if ( g_renderer == NULL ) {
        return;
    }

    // Unload fonts
    if ( g_renderer->ui_font != NULL )
        TTF_CloseFont(g_renderer->ui_font);
    if ( g_renderer->lyrics_font != NULL )
        TTF_CloseFont(g_renderer->lyrics_font);

    // Delete OpenGL objects
    glDeleteProgram(g_renderer->texture_shader);
    glDeleteProgram(g_renderer->rect_shader);
    glDeleteBuffers(1, &g_renderer->VBO);
    glDeleteVertexArrays(1, &g_renderer->VAO);

    // Destroy GL context
    SDL_GL_DeleteContext(g_renderer->gl_context);
    SDL_DestroyWindow(g_renderer->window);

    // Cleanup
    free(g_renderer);
    g_renderer = NULL;
}

void render_on_window_changed(void) {
    int32_t outW, outH;
    SDL_GL_GetDrawableSize(g_renderer->window, &outW, &outH);

#ifdef __APPLE__
    int32_t window_w;
    SDL_GetWindowSize(g_renderer->window, &window_w, NULL);

    g_renderer->window_pixel_scale = (double)outW / (double)window_w;
#else
    g_renderer->window_pixel_scale = 1.0;
#endif

    float hdpi_temp, v_dpi_temp;
    if ( SDL_GetDisplayDPI(0, NULL, &hdpi_temp, &v_dpi_temp) != 0 ) {
        puts(SDL_GetError());
        error_abort("Failed to get DPI");
    }
    g_renderer->h_dpi = hdpi_temp;
    g_renderer->v_dpi = v_dpi_temp;

    g_renderer->viewport = (etsuko_Bounds_t){.x = 0, .y = 0, .w = (double)outW, .h = (double)outH};

    glViewport(0, 0, outW, outH);
    update_projection_matrix();
}

void render_clear(void) {
    glClearColor((float)g_renderer->bg_color.r / 255.0f, (float)g_renderer->bg_color.g / 255.0f,
                 (float)g_renderer->bg_color.b / 255.0f, (float)g_renderer->bg_color.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void render_present(void) { SDL_GL_SwapWindow(g_renderer->window); }

const etsuko_Bounds_t *render_get_viewport(void) { return &g_renderer->viewport; }

double render_get_pixel_scale(void) { return g_renderer->window_pixel_scale; }

void render_load_font(const char *path, const etsuko_FontType_t type) {
    TTF_Font *font = TTF_OpenFontDPI(path, DEFAULT_PT, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->h_dpi);

    if ( font == NULL ) {
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

void render_measure_text_size(const char *text, const int32_t pt, int32_t *w, int32_t *h, const etsuko_FontType_t kind) {
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

const etsuko_RenderTarget_t *render_make_texture_target(const int32_t w, const int32_t h) {
    etsuko_RenderTarget_t *target = calloc(1, sizeof(*target));
    if ( target == NULL ) {
        error_abort("Failed to allocate render target");
    }

    target->texture = calloc(1, sizeof(*target->texture));
    if ( target->texture == NULL ) {
        error_abort("Failed to allocate texture for render target");
    }

    target->texture->width = w;
    target->texture->height = h;

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    target->texture->id = texture_id;

    glGenFramebuffers(1, &target->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if ( status != GL_FRAMEBUFFER_COMPLETE ) {
        printf("Framebuffer not complete! Status: 0x%x\n", status);
        error_abort("Framebuffer is not complete!");
    }
    glGetIntegerv(GL_VIEWPORT, target->saved_viewport);

    // Save current projection matrix
    memcpy(target->saved_projection, g_renderer->projection_matrix, sizeof(float) * 16);

    // Set viewport to FBO size
    glViewport(0, 0, w, h);

    // Create projection matrix for FBO size
    float fbo_projection[16] = {0};
    fbo_projection[0] = 2.0f / (float)w;
    fbo_projection[5] = -2.0f / (float)h;
    fbo_projection[10] = -1.0f;
    fbo_projection[12] = -1.0f;
    fbo_projection[13] = 1.0f;
    fbo_projection[15] = 1.0f;

    // Update global projection matrix (used by drawing functions)
    memcpy(g_renderer->projection_matrix, fbo_projection, sizeof(float) * 16);

    // Update projection in shaders
    glUseProgram(g_renderer->texture_shader);
    glUniformMatrix4fv(g_renderer->tex_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    glUseProgram(g_renderer->rect_shader);
    glUniformMatrix4fv(g_renderer->rect_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    // Store previous target
    target->prev_target = g_renderer->render_target;
    g_renderer->render_target = target;
    g_renderer->rendering_to_fbo = true;

    // Clear the framebuffer
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    return target;
}

void render_restore_texture_target(void) {
    if ( g_renderer->render_target == NULL ) {
        error_abort("No render target to restore");
    }

    const etsuko_RenderTarget_t *current = g_renderer->render_target;
    g_renderer->render_target = current->prev_target;

    // Restore viewport
    glViewport(current->saved_viewport[0], current->saved_viewport[1], current->saved_viewport[2], current->saved_viewport[3]);

    // Restore projection matrix
    memcpy(g_renderer->projection_matrix, current->saved_projection, sizeof(float) * 16);

    // Update projection in shaders
    glUseProgram(g_renderer->texture_shader);
    glUniformMatrix4fv(g_renderer->tex_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    glUseProgram(g_renderer->rect_shader);
    glUniformMatrix4fv(g_renderer->rect_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    // Bind appropriate framebuffer
    if ( g_renderer->render_target == NULL ) {
        // Restore default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        // Bind previous FBO
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer->render_target->fbo);
    }
    g_renderer->rendering_to_fbo = g_renderer->render_target != NULL;
}

void render_destroy_texture(etsuko_Texture_t *texture) {
    glDeleteTextures(1, &texture->id);
    free(texture);
}

etsuko_Color_t render_color_parse(const uint32_t color) {
    const uint8_t a = color >> 24;
    const uint8_t r = color >> 16;
    const uint8_t g = color >> 8;
    const uint8_t b = color & 0xFF;
    return (etsuko_Color_t){.r = r, .g = g, .b = b, .a = a};
}

void render_set_bg_color(const etsuko_Color_t color) { g_renderer->bg_color = color; }

void render_set_blend_mode(const etsuko_BlendMode_t mode) {
    g_renderer->blend_mode = mode;

    switch ( mode ) {
    case BLEND_MODE_BLEND:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case BLEND_MODE_NONE:
        glDisable(GL_BLEND);
        break;
    default:
        error_abort("Invalid blend mode");
    }
}

etsuko_BlendMode_t render_get_blend_mode(void) { return g_renderer->blend_mode; }

etsuko_Texture_t *render_make_text(const char *text, const int32_t pt_size, const bool bold, const etsuko_Color_t *color,
                                   const etsuko_FontType_t font_type) {
    TTF_Font *font = font_type == FONT_UI ? g_renderer->ui_font : g_renderer->lyrics_font;
    if ( font == NULL ) {
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

    const SDL_Color sdl_color = (SDL_Color){color->r, color->g, color->b, color->a};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, sdl_color);
    if ( surface == NULL ) {
        puts(TTF_GetError());
        error_abort("Failed to render to surface");
    }

    etsuko_Texture_t *texture = create_texture_from_surface(surface);
    SDL_FreeSurface(surface);

    return texture;
}

static etsuko_Texture_t *create_test_texture(void) {
    const int size = 256;
    unsigned char *pixels = malloc(size * size * 4);

    // Create a checkerboard pattern
    for ( int y = 0; y < size; y++ ) {
        for ( int x = 0; x < size; x++ ) {
            const int checker = ((x / 32) + (y / 32)) % 2;
            const unsigned char color = checker ? 255 : 0;

            const int index = (y * size + x) * 4;
            pixels[index + 0] = color;
            pixels[index + 1] = color;
            pixels[index + 2] = color;
            pixels[index + 3] = 255;
        }
    }

    etsuko_Texture_t *texture = calloc(1, sizeof(*texture));
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

etsuko_Texture_t *render_make_image(const char *file_path, const double border_radius_em) {
    SDL_Surface *loaded = IMG_Load(file_path);
    if ( loaded == NULL ) {
        printf("IMG_GetError: %s\n", IMG_GetError());
        error_abort("Failed to load image");
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA8888, 0);
    if ( converted == NULL ) {
        error_abort("Failed to convert image surface to appropriate pixel format");
    }
    SDL_FreeSurface(loaded);

    etsuko_Texture_t *final_texture = create_texture_from_surface(converted);
    SDL_FreeSurface(converted);

    if ( border_radius_em > 0 ) {
        final_texture->border_radius_em = border_radius_em;
    }

    return final_texture;
}

etsuko_Texture_t *render_make_dummy_image(const double border_radius_em) {
    etsuko_Texture_t *texture = create_test_texture();

    if ( border_radius_em > 0 ) {
        texture->border_radius_em = border_radius_em;
    }

    return texture;
}

void render_draw_rounded_rect(const etsuko_Bounds_t *bounds, const etsuko_Color_t *color) {
    const double radius = bounds->h / 3.0;

    if ( bounds->w <= 0 ) {
        return;
    }

    glUseProgram(g_renderer->rect_shader);

    glUniform4f(g_renderer->rect_color_loc, (float)color->r / 255.0f, (float)color->g / 255.0f, (float)color->b / 255.0f,
                (float)color->a / 255.0f);
    glUniform2f(g_renderer->rect_pos_loc, (float)bounds->x, (float)bounds->y);
    glUniform2f(g_renderer->rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->rect_radius_loc, (float)radius);
    glUniformMatrix4fv(g_renderer->rect_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    const float padding = (float)radius;
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

void render_draw_texture(const etsuko_Texture_t *texture, const etsuko_Bounds_t *at, const int32_t alpha_mod) {
    if ( texture == NULL || texture->id == 0 ) {
        error_abort("Warning: Attempting to draw invalid texture\n");
    }

    if ( at->x + at->w < 0 || at->x > g_renderer->viewport.w || at->y + at->h < 0 || at->y > g_renderer->viewport.h ) {
        return;
    }

    glUseProgram(g_renderer->texture_shader);

    glUniform1f(g_renderer->tex_alpha_loc, (float)alpha_mod / 255.0f);
    glUniformMatrix4fv(g_renderer->tex_projection_loc, 1, GL_FALSE, g_renderer->projection_matrix);

    if ( texture->border_radius_em > 0 ) {
        const int border_radius = render_measure_pt_from_em(texture->border_radius_em);
        const GLuint program = g_renderer->rounded_corners_shader;
        glUseProgram(program);

        glUniform1f(glGetUniformLocation(program, "uCornerRadius"), (float)border_radius);
        glUniform2f(glGetUniformLocation(program, "uRectSize"), (float)at->w, (float)at->h);
        glUniform4f(glGetUniformLocation(program, "uBounds"), (float)at->x, (float)at->y, (float)at->w, (float)at->h);
        glUniformMatrix4fv(glGetUniformLocation(program, "uProjection"), 1, GL_FALSE, g_renderer->projection_matrix);
    }

    glBindTexture(GL_TEXTURE_2D, texture->id);

    const float tex_top = g_renderer->rendering_to_fbo ? 1.0f : 0.0f;
    const float tex_bot = g_renderer->rendering_to_fbo ? 0.0f : 1.0f;
    const float vertices[] = {(float)at->x,           (float)at->y,           0.0f, tex_top,
                              (float)(at->x + at->w), (float)at->y,           1.0f, tex_top,
                              (float)(at->x + at->w), (float)(at->y + at->h), 1.0f, tex_bot,
                              (float)at->x,           (float)(at->y + at->h), 0.0f, tex_bot};

    glBindVertexArray(g_renderer->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
}
