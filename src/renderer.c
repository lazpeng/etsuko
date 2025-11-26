/**
 * renderer.c - OpenGL-based rendering backend
 */

#include "renderer.h"

#include "constants.h"
#include "error.h"
#include "events.h"

#include "contrib/stb_image.h"
#include "contrib/stb_truetype.h"

#include <unicode/utf8.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define RESOURCE_INCLUDE_SHADERS
#include "resource_includes.h"

#define BASE_DPI 72.f
// 1MB
#define MAX_SHADER_SIZE (1 * 1024 * 1024)
#define QUAD_VERTICES_SIZE (4 /*points*/ * 3 /*vertices per triangle*/ * 2 /*triangles*/)
#define PROJECTION_MATRIX_SIZE (16)

typedef struct Renderer_t {
    GLFWwindow *window;
    Bounds_t viewport;
    stbtt_fontinfo ui_font_info, lyrics_font_info;
    unsigned char *ui_font_data, *lyrics_font_data;
    double h_dpi, v_dpi;
    Color_t bg_color, bg_color_secondary;
    RenderTarget_t *render_target;
    BlendMode_t blend_mode;
    bool rendering_to_fbo;
    double window_pixel_scale;
    Texture_t *bg_texture;
    BackgroundType_t bg_type;
    float dynamic_bg_colors[5][3];
    bool dynamic_bg_colors_initialized;

    // OpenGL objects
    GLuint active_shader_program;
    GLuint texture_shader;
    GLuint rect_shader;
    GLuint gradient_shader;
    GLuint dyn_gradient_shader;
    GLuint am_gradient_shader;
    GLuint cloud_gradient_shader;
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
    GLint tex_color_mod_loc;
    GLint tex_num_regions_loc;
    GLint tex_regions_loc;
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
    GLint dyn_grad_time_loc;
    GLint dyn_grad_noise_mag_loc;
    GLint dyn_grad_colors;
} Renderer_t;

static Renderer_t *g_renderer = NULL;

static GLuint compile_shader(const GLenum type, const char *source, const char *name) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if ( !success ) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        const char *const type_str = type == GL_VERTEX_SHADER ? "vert" : "frag";
        printf("Shader compilation failed for %s.%s:\n%s\n", name, type_str, log);
        error_abort("Shader compilation failed");
    }

    return shader;
}

static char *begin_shader_compilation(void) {
    char *buffer = calloc(1, MAX_SHADER_SIZE);
    if ( buffer == NULL ) {
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
        glGetProgramInfoLog(program, 512, NULL, log);
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

static void create_quad_vertices(const float x, const float y, const float w, const float h, float *dest_vertices) {
    const float vertices[] = {x, (y + h), 0.0f, 1.0f, x,       y, 0.0f, 0.0f, (x + w), y,       1.0f, 0.0f,
                              x, (y + h), 0.0f, 1.0f, (x + w), y, 1.0f, 0.0f, (x + w), (y + h), 1.0f, 1.0f};

    memcpy(dest_vertices, vertices, sizeof(vertices));
}

static void update_projection_matrix(void) {
    const float w = (float)g_renderer->viewport.w;
    const float h = (float)g_renderer->viewport.h;

    create_orthographic_matrix(0.f, w, h, 0.f, g_renderer->projection_matrix);
}

static void set_shader_program(const GLuint program) {
    if ( g_renderer->active_shader_program != program ) {
        glUseProgram(program);
        g_renderer->active_shader_program = program;
    }
}

static bool texture_needs_reconfigure(const Texture_t *texture, const Bounds_t *at) {
    return texture->buf_w != at->w || texture->buf_h != at->h || texture->buf_x != at->x || texture->buf_y != at->y;
}

static void mark_texture_configured(Texture_t *texture, const Bounds_t *at) {
    texture->buf_x = (int32_t)at->x;
    texture->buf_y = (int32_t)at->y;
    texture->buf_w = (int32_t)at->w;
    texture->buf_h = (int32_t)at->h;
}

#ifdef __EMSCRIPTEN__
static EM_BOOL on_web_resize(const int eventType, const EmscriptenUiEvent *uiEvent, void *) {
    if ( eventType == EMSCRIPTEN_EVENT_RESIZE ) {
        const int width = uiEvent->windowInnerWidth;
        const int height = uiEvent->windowInnerHeight;

        glfwSetWindowSize(g_renderer->window, width, height);
        emscripten_set_element_css_size("#canvas", width, height);

        return EM_TRUE;
    }
    return EM_FALSE;
}
#endif

void render_init(void) {
    if ( g_renderer != NULL ) {
        printf("Warning: renderer already initialized\n");
        return;
    }

    g_renderer = calloc(1, sizeof(*g_renderer));
    if ( g_renderer == NULL ) {
        error_abort("Failed to allocate renderer");
    }

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    const int width = emscripten_run_script_int("window.innerWidth");
    const int height = emscripten_run_script_int("window.innerHeight");
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    const int width = DEFAULT_WIDTH;
    const int height = DEFAULT_HEIGHT;
#endif
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    g_renderer->window = glfwCreateWindow(width, height, DEFAULT_TITLE, NULL, NULL);
    if ( g_renderer->window == NULL ) {
        error_abort("Failed to create window");
    }

#ifdef __EMSCRIPTEN__
    // Force CSS size to match logical size, while window/buffer is scaled
    // This is unfortunately necessary because under GLFW it seems you can't have the framebuffer and window at different
    // sizes. I'm not sure what the problem is but while dirty, this works currently, and I'll leave to investigate this later
    emscripten_set_element_css_size("#canvas", width, height);
#endif

    events_setup_callbacks(g_renderer->window);

    glfwMakeContextCurrent(g_renderer->window);

#ifndef __EMSCRIPTEN__
    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if ( err != GLEW_OK ) {
        printf("GLEW Error: %s\n", (char *)glewGetErrorString(err));
        error_abort("Failed to initialize GLEW");
    }
    glGetError();
    glfwSwapInterval(1);
#else
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, false, on_web_resize);
#endif

    g_renderer->bg_color = (Color_t){0, 0, 0, 255};
    g_renderer->bg_type = BACKGROUND_NONE;

    render_on_window_changed();

    g_renderer->blend_mode = BLEND_MODE_NONE;
    render_set_blend_mode(BLEND_MODE_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Compile shaders
    char *buffer = begin_shader_compilation();
    g_renderer->texture_shader = create_shader_program(buffer, incbin_texture_vert_shader, incbin_texture_frag_shader, "tex");
    g_renderer->rect_shader = create_shader_program(buffer, incbin_rect_vert_shader, incbin_rect_frag_shader, "rect");
    g_renderer->gradient_shader =
        create_shader_program(buffer, incbin_gradient_vert_shader, incbin_gradient_frag_shader, "gradient");
    g_renderer->dyn_gradient_shader =
        create_shader_program(buffer, incbin_dyn_gradient_vert_shader, incbin_dyn_gradient_frag_shader, "dyn_gradient");
    g_renderer->am_gradient_shader =
        create_shader_program(buffer, incbin_am_gradient_vert_shader, incbin_am_gradient_frag_shader, "am_gradient");
    g_renderer->cloud_gradient_shader =
        create_shader_program(buffer, incbin_am_gradient_vert_shader, incbin_cloud_gradient_frag_shader, "cloud_gradient");
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
    g_renderer->tex_color_mod_loc = glGetUniformLocation(g_renderer->texture_shader, "colorModFactor");
    g_renderer->tex_num_regions_loc = glGetUniformLocation(g_renderer->texture_shader, "num_regions");
    g_renderer->tex_regions_loc = glGetUniformLocation(g_renderer->texture_shader, "regions");

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

    // Get uniform locations for the dynamic gradient shader
    g_renderer->dyn_grad_time_loc = glGetUniformLocation(g_renderer->dyn_gradient_shader, "u_time");
    g_renderer->dyn_grad_noise_mag_loc = glGetUniformLocation(g_renderer->dyn_gradient_shader, "u_noise_magnitude");
    g_renderer->dyn_grad_colors = glGetUniformLocation(g_renderer->dyn_gradient_shader, "u_colors");

    // Get uniform locations for blur shader
    g_renderer->blur_texture_loc = glGetUniformLocation(g_renderer->blur_shader, "u_texture");
    g_renderer->blur_direction_loc = glGetUniformLocation(g_renderer->blur_shader, "u_direction");
    g_renderer->blur_size_loc = glGetUniformLocation(g_renderer->blur_shader, "u_blur_size");
    g_renderer->blur_projection_loc = glGetUniformLocation(g_renderer->blur_shader, "u_projection");
}

void render_finish(void) {
    if ( g_renderer == NULL ) {
        return;
    }

    // Unload fonts
    if ( g_renderer->ui_font_data != NULL )
        free(g_renderer->ui_font_data);
    if ( g_renderer->lyrics_font_data != NULL )
        free(g_renderer->lyrics_font_data);

    // Delete OpenGL objects
    glDeleteProgram(g_renderer->texture_shader);
    glDeleteProgram(g_renderer->rect_shader);
    glDeleteProgram(g_renderer->gradient_shader);
    glDeleteProgram(g_renderer->dyn_gradient_shader);
    glDeleteProgram(g_renderer->rand_gradient_shader);
    glDeleteProgram(g_renderer->blur_shader);
    glDeleteProgram(g_renderer->am_gradient_shader);
    glDeleteProgram(g_renderer->cloud_gradient_shader);
    glDeleteProgram(g_renderer->copy_shader);

    // Destroy GL context (GLFW destroys context with window)
    glfwDestroyWindow(g_renderer->window);

    // Cleanup
    free(g_renderer);
    g_renderer = NULL;
}

void render_on_window_changed(void) {
    int32_t outW, outH;
    glfwGetFramebufferSize(g_renderer->window, &outW, &outH);

#ifdef __EMSCRIPTEN__
    g_renderer->window_pixel_scale = emscripten_get_device_pixel_ratio();
#else
    int32_t window_w;
    glfwGetWindowSize(g_renderer->window, &window_w, NULL);

    g_renderer->window_pixel_scale = (double)outW / (double)window_w;
#endif

    events_set_window_pixel_scale(g_renderer->window_pixel_scale);

    float x_scale, y_scale;
    glfwGetWindowContentScale(g_renderer->window, &x_scale, &y_scale);

    // Approximate DPI based on scale factor
    g_renderer->h_dpi = BASE_DPI * x_scale;
    g_renderer->v_dpi = BASE_DPI * y_scale;

    g_renderer->viewport = (Bounds_t){.x = 0, .y = 0, .w = (double)outW, .h = (double)outH};

    glViewport(0, 0, outW, outH);
    update_projection_matrix();

    // Update background texture
    if ( g_renderer->bg_texture != NULL ) {
        render_destroy_texture(g_renderer->bg_texture);
        g_renderer->bg_texture = NULL;
    }
}

static void deconstruct_colors_opengl(const Color_t *color, float *r, float *g, float *b, float *a) {
    if ( r )
        *r = (float)color->r / 255.0f;
    if ( g )
        *g = (float)color->g / 255.0f;
    if ( b )
        *b = (float)color->b / 255.0f;
    if ( a )
        *a = (float)color->a / 255.0f;
}

static void draw_random_gradient_bg(void) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const float rate = 0.005f;
    static float progress = 0.f;
    static float noise_magnitude = 0.1f;

    const float target_magnitude = 0.2f;

    progress += (float)(rate * events_get_delta_time());
    noise_magnitude = noise_magnitude * (1.f - progress) + target_magnitude * progress;

    if ( g_renderer->bg_texture == NULL ) {
        g_renderer->bg_texture = render_make_null();
        // At this point, the texture is unconfigured
    }

    set_shader_program(g_renderer->rand_gradient_shader);
    glBindVertexArray(g_renderer->bg_texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->bg_texture->vbo);

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const Bounds_t bounds = {.x = 0, .y = 0, .w = (double)width, .h = (double)height};
    if ( texture_needs_reconfigure(g_renderer->bg_texture, &bounds) ) {
        // Update vertex data for the vbo
        static float quadVertices[] = {-1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
                                       -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        mark_texture_configured(g_renderer->bg_texture, &bounds);
    }

    glUniform1f(g_renderer->rand_grad_time_loc, (float)events_get_elapsed_time());
    glUniform1f(g_renderer->rand_grad_noise_scale_loc, noise_magnitude);
    glUniform2f(g_renderer->rand_grad_resolution_loc, (float)width, (float)height);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    render_set_blend_mode(saved_blend);
}

static void draw_dynamic_gradient_bg(void) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    if ( g_renderer->bg_texture == NULL ) {
        g_renderer->bg_texture = render_make_null();
    }

    set_shader_program(g_renderer->dyn_gradient_shader);

    glBindVertexArray(g_renderer->bg_texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->bg_texture->vbo);

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const Bounds_t bounds = {.x = 0, .y = 0, .w = (double)width, .h = (double)height};
    if ( texture_needs_reconfigure(g_renderer->bg_texture, &bounds) ) {
        // Update vertex data for the vbo
        static float quadVertices[] = {-1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
                                       -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        mark_texture_configured(g_renderer->bg_texture, &bounds);
    }

    glUniform1f(g_renderer->dyn_grad_time_loc, (float)events_get_elapsed_time() / 5.f);
    glUniform1f(g_renderer->dyn_grad_noise_mag_loc, 0.1f);
    glUniform3fv(g_renderer->dyn_grad_colors, 5, &g_renderer->dynamic_bg_colors[0][0]);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    render_set_blend_mode(saved_blend);
}

static void draw_am_like_bg(const BackgroundType_t type) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    GLuint shader_program;
    if ( type == BACKGROUND_AM_LIKE_GRADIENT ) {
        shader_program = g_renderer->am_gradient_shader;
    } else {
        shader_program = g_renderer->cloud_gradient_shader;
    }

    if ( g_renderer->bg_texture == NULL ) {
        g_renderer->bg_texture = render_make_null();
    }

    set_shader_program(shader_program);
    glBindVertexArray(g_renderer->bg_texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer->bg_texture->vbo);

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const Bounds_t bounds = {.x = 0, .y = 0, .w = (double)width, .h = (double)height};
    if ( texture_needs_reconfigure(g_renderer->bg_texture, &bounds) ) {

        static float quadVertices[] = {-1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
                                       -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f};
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        mark_texture_configured(g_renderer->bg_texture, &bounds);
    }

    glUniform1f(glGetUniformLocation(shader_program, "iTime"), (float)events_get_elapsed_time());
    glUniform3f(glGetUniformLocation(shader_program, "iResolution"), 1.f, 1.f, 0.f);
    glUniform3fv(glGetUniformLocation(shader_program, "iColors"), 5, &g_renderer->dynamic_bg_colors[0][0]);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    render_set_blend_mode(saved_blend);
}

static Texture_t *internal_create_gradient_background_texture(void) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = (int32_t)g_renderer->viewport.w, height = (int32_t)g_renderer->viewport.h;
    const RenderTarget_t *target = render_make_texture_target(width, height);

    set_shader_program(g_renderer->gradient_shader);

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
    glBindVertexArray(target->texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, target->texture->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    Texture_t *texture = render_restore_texture_target();
    render_set_blend_mode(saved_blend);
    return texture;
}

Texture_t *render_blur_texture(const Texture_t *source, const float blur_radius) {
    if ( !source || blur_radius <= 0 || source->width <= 0 || source->height <= 0 ) {
        error_abort("Fail at render_blur_texture_radius");
    }

    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = source->width, height = source->height;
    const RenderTarget_t *first_target = render_make_texture_target(width, height);

    const float w = (float)source->width;
    const float h = (float)source->height;

    set_shader_program(g_renderer->blur_shader);
    glUniformMatrix4fv(g_renderer->blur_projection_loc, 1, GL_FALSE, first_target->projection);
    glUniform1f(g_renderer->blur_size_loc, blur_radius);
    glUniform2f(g_renderer->blur_direction_loc, 1.0f, 0.0f);

    glBindTexture(GL_TEXTURE_2D, source->id);

    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices(0, 0, w, h, vertices);

    glBindVertexArray(first_target->texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, first_target->texture->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    Texture_t *vertical_texture = render_restore_texture_target();

    const RenderTarget_t *second_target = render_make_texture_target(width, height);

    glUniform2f(g_renderer->blur_direction_loc, 0.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, vertical_texture->id);
    glBindVertexArray(second_target->texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, second_target->texture->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STREAM_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    Texture_t *horizontal_texture = render_restore_texture_target();
    horizontal_texture->border_radius = source->border_radius;
    render_destroy_texture(vertical_texture);

    render_set_blend_mode(saved_blend);
    return horizontal_texture;
}

Texture_t *render_blur_texture_replace(Texture_t *source, const float blur_radius) {
    Texture_t *blurred = render_blur_texture(source, blur_radius);
    render_destroy_texture(source);
    return blurred;
}

void render_clear(void) {
    // Return early if it's just a solid background, or we haven't initialized all the required params to draw the bg yet
    const bool bg_not_initialized = g_renderer->bg_type != BACKGROUND_GRADIENT && !g_renderer->dynamic_bg_colors_initialized;
    if ( g_renderer->bg_type == BACKGROUND_NONE || bg_not_initialized ) {
        float r, g, b, a;
        deconstruct_colors_opengl(&g_renderer->bg_color, &r, &g, &b, &a);
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if ( g_renderer->bg_type == BACKGROUND_GRADIENT ) {
        if ( g_renderer->bg_texture == NULL ) {
            g_renderer->bg_texture = internal_create_gradient_background_texture();
        }
        static DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 1.f};
        render_draw_texture(g_renderer->bg_texture, &(Bounds_t){0}, &opts);
    } else if ( g_renderer->bg_type == BACKGROUND_DYNAMIC_GRADIENT ) {
        draw_dynamic_gradient_bg();
    } else if ( g_renderer->bg_type == BACKGROUND_RANDOM_GRADIENT ) {
        draw_random_gradient_bg();
    } else if ( g_renderer->bg_type == BACKGROUND_AM_LIKE_GRADIENT || g_renderer->bg_type == BACKGROUND_CLOUD_GRADIENT ) {
        draw_am_like_bg(g_renderer->bg_type);
    }
}

void render_present(void) { glfwSwapBuffers(g_renderer->window); }

const Bounds_t *render_get_viewport(void) { return &g_renderer->viewport; }

double render_get_pixel_scale(void) { return g_renderer->window_pixel_scale; }

void render_load_font(const unsigned char *data, const int data_size, const FontType_t type) {
    unsigned char *data_copy = calloc(1, data_size);
    memcpy(data_copy, data, data_size);

    stbtt_fontinfo *info;
    if ( type == FONT_UI ) {
        info = &g_renderer->ui_font_info;
        g_renderer->ui_font_data = data_copy;
    } else if ( type == FONT_LYRICS ) {
        info = &g_renderer->lyrics_font_info;
        g_renderer->lyrics_font_data = data_copy;
    } else {
        error_abort("Invalid font kind");
    }

    if ( !stbtt_InitFont(info, data_copy, 0) ) {
        error_abort("Could not load font");
    }
}

void render_set_window_title(const char *title) { glfwSetWindowTitle(g_renderer->window, title); }

void render_measure_text_size(const char *text, const int32_t pixels, int32_t *w, int32_t *h, const FontType_t kind) {
    const stbtt_fontinfo *font = kind == FONT_UI ? &g_renderer->ui_font_info : &g_renderer->lyrics_font_info;

    const float pixel_height = (float)pixels;
    const float scale = stbtt_ScaleForMappingEmToPixels(font, pixel_height);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);

    *h = (int32_t)((ascent - descent + lineGap) * (double)scale);

    int width = 0;
    int32_t i = 0;
    const int32_t len = (int32_t)strlen(text);
    UChar32 c = 0;
    UChar32 prev_c = -1;

    while ( i < len ) {
        U8_NEXT(text, i, len, c);
        if ( c < 0 )
            continue;

        int advance, lsb;
        stbtt_GetCodepointHMetrics(font, c, &advance, &lsb);

        if ( prev_c != -1 ) {
            width += stbtt_GetCodepointKernAdvance(font, prev_c, c);
        }

        width += advance;
        prev_c = c;
    }

    *w = (int32_t)(width * (double)scale);
}

int32_t render_measure_pixels_from_em(const double em) {
    const double scale = g_renderer->viewport.w / DEFAULT_WIDTH;
    const double rem = fmax(12.0, round(DEFAULT_PT * scale));
    const double pixels = em * rem;
    return (int32_t)pixels;
}

int32_t render_measure_pt_from_em(const double em) {
    const double pixels = render_measure_pixels_from_em(em);
    const int32_t pt_size = (int32_t)lround(pixels * BASE_DPI / g_renderer->h_dpi);
    return pt_size;
}

void render_measure_char_bounds(const UChar32 c, const UChar32 prev_c, const int32_t pixels, CharBounds_t *out_bounds,
                                const FontType_t font) {
    const stbtt_fontinfo *font_info = font == FONT_UI ? &g_renderer->ui_font_info : &g_renderer->lyrics_font_info;

    const float scale = stbtt_ScaleForMappingEmToPixels(font_info, (float)pixels);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(font_info, &ascent, &descent, &lineGap);

    const double height = (ascent - descent + lineGap) * (double)scale;

    int advance, lsb;
    stbtt_GetCodepointHMetrics(font_info, c, &advance, &lsb);

    double kerning = 0.0;
    if ( prev_c > 0 ) {
        kerning = stbtt_GetCodepointKernAdvance(font_info, prev_c, c);
    }

    out_bounds->kerning = kerning * (double)scale;
    out_bounds->advance = advance * (double)scale;
    out_bounds->width = (kerning + advance) * (double)scale;
    out_bounds->font_height = height;
}

static float *get_projection_matrix(void) {
    if ( g_renderer->render_target == NULL ) {
        return g_renderer->projection_matrix;
    }
    return g_renderer->render_target->projection;
}

const RenderTarget_t *render_make_texture_target(const int32_t width, const int32_t height) {
    RenderTarget_t *target = calloc(1, sizeof(*target));
    if ( target == NULL ) {
        error_abort("Failed to allocate render target");
    }

    target->texture = render_make_null();

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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

Texture_t *render_restore_texture_target(void) {
    if ( g_renderer->render_target == NULL ) {
        error_abort("No render target to restore");
    }

    RenderTarget_t *current = g_renderer->render_target;
    RenderTarget_t *prev = current->prev_target;
    g_renderer->render_target = prev;

    // Bind appropriate framebuffer
    if ( prev == NULL ) {
        glViewport(0, 0, (int32_t)g_renderer->viewport.w, (int32_t)g_renderer->viewport.h);
        // Restore default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glViewport(prev->viewport[0], prev->viewport[1], prev->viewport[2], prev->viewport[3]);
        // Bind previous FBO
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer->render_target->fbo);
    }

    Texture_t *texture = current->texture;

    glDeleteFramebuffers(1, &current->fbo);
    // Assume the texture is going to be freed on its own, or else there's no point to making a
    // render target in the first place.
    free(current);

    return texture;
}

void render_destroy_texture(Texture_t *texture) {
    if ( texture->id != 0 )
        glDeleteTextures(1, &texture->id);
    if ( texture->vao != 0 )
        glDeleteVertexArrays(1, &texture->vao);
    if ( texture->vbo != 0 )
        glDeleteBuffers(1, &texture->vbo);
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
    const double amount = 0.8;
    color.r = (uint8_t)fmax(0, color.r * amount);
    color.g = (uint8_t)fmax(0, color.g * amount);
    color.b = (uint8_t)fmax(0, color.b * amount);
    return color;
}

void render_set_bg_color(const Color_t color) {
    g_renderer->bg_color = color;
    g_renderer->bg_type = BACKGROUND_NONE;
}

void render_set_bg_gradient(const Color_t top_color, const Color_t bottom_color, const BackgroundType_t type) {
    g_renderer->bg_color = top_color;
    g_renderer->bg_color_secondary = bottom_color;
    g_renderer->bg_type = type;
}

static float calculate_color_luminance(const Color_t *color) {
    return 0.299f * (float)color->r + 0.587f * (float)color->g + 0.114f * (float)color->b;
}

void render_sample_bg_colors_from_image(const unsigned char *bytes, const int length) {
    int width, height, channels;
    unsigned char *image_data = stbi_load_from_memory(bytes, length, &width, &height, &channels, 3);

    if ( !image_data ) {
        fprintf(stderr, "Failed to load background image for color sampling\n");
        return;
    }

    // Sample pixels from the image (use stride for large images)
    const int total_pixels = width * height;
    const int sample_stride = total_pixels > 10000 ? (int)sqrt(total_pixels / 10000.0) : 1;

    Color_t *samples = malloc(sizeof(Color_t) * (total_pixels / (sample_stride * sample_stride) + 1000));
    int sample_count = 0;

    for ( int y = 0; y < height; y += sample_stride ) {
        for ( int x = 0; x < width; x += sample_stride ) {
            const int idx = (y * width + x) * 3;
            const uint8_t r = image_data[idx + 0];
            const uint8_t g = image_data[idx + 1];
            const uint8_t b = image_data[idx + 2];

            const float lum = calculate_color_luminance(&(Color_t){r, g, b, 0});

            // Filter out very dark or very bright colors
            if ( lum > 15.0f && lum < 240.0f ) {
                samples[sample_count].r = r;
                samples[sample_count].g = g;
                samples[sample_count].b = b;
                sample_count++;
            }
        }
    }

    // If we filtered out too much, relax the constraint
    if ( sample_count < 50 ) {
        sample_count = 0;
        for ( int y = 0; y < height; y += sample_stride ) {
            for ( int x = 0; x < width; x += sample_stride ) {
                const int idx = (y * width + x) * 3;
                samples[sample_count].r = image_data[idx + 0];
                samples[sample_count].g = image_data[idx + 1];
                samples[sample_count].b = image_data[idx + 2];
                sample_count++;
            }
        }
    }

    if ( sample_count == 0 ) {
        free(samples);
        stbi_image_free(image_data);
        return;
    }

    // Find 5 dominant colors
#define K 5
    Color_t centroids[K];
    int *assignments = calloc(1, sizeof(int) * sample_count);

    // Initialize centroids by spreading them across the sample space
    for ( int i = 0; i < K; i++ ) {
        const int idx = (i * sample_count / K + sample_count / (K * 2)) % sample_count;
        centroids[i] = samples[idx];
    }

    // Run k-means iterations
    for ( int iter = 0; iter < 15; iter++ ) {
        // Assignment step: assign each sample to nearest centroid
        for ( int i = 0; i < sample_count; i++ ) {
            int best_k = 0;
            float best_dist = INFINITY;

            for ( int k = 0; k < K; k++ ) {
                const int dr = (int)samples[i].r - (int)centroids[k].r;
                const int dg = (int)samples[i].g - (int)centroids[k].g;
                const int db = (int)samples[i].b - (int)centroids[k].b;
                const float dist = (float)(dr * dr + dg * dg + db * db);

                if ( dist < best_dist ) {
                    best_dist = dist;
                    best_k = k;
                }
            }
            assignments[i] = best_k;
        }

        // Update step: recalculate centroids
        float sums[K][3] = {{0}};
        int counts[K] = {0};

        for ( int i = 0; i < sample_count; i++ ) {
            const int k = assignments[i];
            sums[k][0] += (float)samples[i].r;
            sums[k][1] += (float)samples[i].g;
            sums[k][2] += (float)samples[i].b;
            counts[k]++;
        }

        for ( int k = 0; k < K; k++ ) {
            if ( counts[k] > 0 ) {
                centroids[k].r = (unsigned char)(sums[k][0] / (float)counts[k] + 0.5f);
                centroids[k].g = (unsigned char)(sums[k][1] / (float)counts[k] + 0.5f);
                centroids[k].b = (unsigned char)(sums[k][2] / (float)counts[k] + 0.5f);
            }
        }
    }

    typedef struct {
        Color_t color;
        float luminance;
    } ColorLum;

    ColorLum sorted[K];
    for ( int i = 0; i < K; i++ ) {
        sorted[i].color = centroids[i];
        sorted[i].luminance = calculate_color_luminance(&centroids[i]);
    }

    // bubble sort
    for ( int i = 0; i < K - 1; i++ ) {
        for ( int j = 0; j < K - 1 - i; j++ ) {
            if ( sorted[j].luminance > sorted[j + 1].luminance ) {
                const ColorLum temp = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = temp;
            }
        }
    }

    for ( int i = 0; i < K; i++ ) {
        float *r = &g_renderer->dynamic_bg_colors[i][0];
        float *g = &g_renderer->dynamic_bg_colors[i][1];
        float *b = &g_renderer->dynamic_bg_colors[i][2];
        deconstruct_colors_opengl(&sorted[i].color, r, g, b, NULL);
    }
    // Mark as initialized
    g_renderer->dynamic_bg_colors_initialized = true;

    // Cleanup
    free(assignments);
    free(samples);
    stbi_image_free(image_data);
}

void render_set_blend_mode(const BlendMode_t mode) {
    if ( mode == g_renderer->blend_mode )
        return;
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
    case BLEND_MODE_ERASE:
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        break;
    default:
        error_abort("Invalid blend mode");
    }
}

BlendMode_t render_get_blend_mode(void) { return g_renderer->blend_mode; }

Texture_t *render_make_null(void) {
    Texture_t *texture = calloc(1, sizeof(*texture));
    texture->width = 0;
    texture->height = 0;
    texture->id = 0;
    // Init VBO

    glGenVertexArrays(1, &texture->vao);
    glGenBuffers(1, &texture->vbo);

    glBindVertexArray(texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, texture->vbo);

    // Allocate buffer (4 vertices * 4 floats per vertex: x, y, u, v)
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 4, NULL, GL_DYNAMIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);

    // TexCoord attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return texture;
}

Texture_t *render_make_text(const char *text, const int32_t pixels_size, const Color_t *color, const FontType_t font_type) {
    const stbtt_fontinfo *font = font_type == FONT_UI ? &g_renderer->ui_font_info : &g_renderer->lyrics_font_info;

    if ( strnlen(text, MAX_TEXT_SIZE) == 0 ) {
        error_abort("render_make_text: Text is empty");
    }

    // TODO: Improve performance (single pass bitmap creation, reusing bitmap buffers)
    // TODO: Maybe look into SDF and making a texture atlas

    const float pixel_height = (float)pixels_size;
    const float scale = stbtt_ScaleForMappingEmToPixels(font, pixel_height);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);

    const int baseline = (int)(ascent * (double)scale);
    const int height = (int)((ascent - descent + lineGap) * (double)scale);

    int width = 0;
    int32_t i = 0;
    const int32_t len = (int32_t)strlen(text);
    UChar32 c = 0;
    UChar32 prev_c = -1;

    while ( i < len ) {
        U8_NEXT(text, i, len, c);
        if ( c < 0 )
            continue;

        int advance, lsb;
        stbtt_GetCodepointHMetrics(font, c, &advance, &lsb);

        if ( prev_c != -1 ) {
            width += stbtt_GetCodepointKernAdvance(font, prev_c, c);
        }

        width += advance;
        prev_c = c;
    }
    width = (int)(width * (double)scale);

    unsigned char *bitmap = calloc(1, width * height);

    double x = 0;
    i = 0;
    prev_c = -1;

    while ( i < len ) {
        U8_NEXT(text, i, len, c);
        if ( c < 0 )
            continue;

        int advance, lsb;
        stbtt_GetCodepointHMetrics(font, c, &advance, &lsb);

        if ( prev_c != -1 ) {
            x += stbtt_GetCodepointKernAdvance(font, prev_c, c) * (double)scale;
        }

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(font, c, scale, scale, 0, 0, &x0, &y0, &x1, &y1);

        int c_w, c_h, c_xoff, c_yoff;
        unsigned char *char_bitmap = stbtt_GetCodepointBitmapSubpixel(font, 0, scale, 0, 0, c, &c_w, &c_h, &c_xoff, &c_yoff);

        if ( char_bitmap ) {
            const int target_x = (int)x + c_xoff;
            const int target_y = baseline + c_yoff;

            for ( int y = 0; y < c_h; ++y ) {
                for ( int x_pix = 0; x_pix < c_w; ++x_pix ) {
                    const int out_x = target_x + x_pix;
                    const int out_y = target_y + y;

                    if ( out_x >= 0 && out_x < width && out_y >= 0 && out_y < height ) {
                        const unsigned char val = char_bitmap[y * c_w + x_pix];
                        if ( val > 0 ) {
                            bitmap[out_y * width + out_x] = val;
                        }
                    }
                }
            }
            stbtt_FreeBitmap(char_bitmap, NULL);
        }

        x += advance * (double)scale;
        prev_c = c;
    }

    unsigned char *rgba = malloc(width * height * 4);
    for ( int j = 0; j < width * height; ++j ) {
        rgba[j * 4 + 0] = color->r;
        rgba[j * 4 + 1] = color->g;
        rgba[j * 4 + 2] = color->b;
        rgba[j * 4 + 3] = bitmap[j];
    }
    free(bitmap);

    Texture_t *texture = render_make_null();
    texture->width = width;
    texture->height = height;

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    free(rgba);
    texture->id = texture_id;

    return texture;
}

static Texture_t *create_test_texture(void) {
    const int size = 256;
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

    Texture_t *texture = render_make_null();
    texture->width = size;
    texture->height = size;

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    free(pixels);
    texture->id = texture_id;

    return texture;
}

Texture_t *render_make_image(const unsigned char *bytes, const int length, const double border_radius_em) {
    int32_t w, h;
    unsigned char *pixels = stbi_load_from_memory(bytes, length, &w, &h, NULL, 4);
    if ( pixels == NULL ) {
        error_abort("Failed to load image");
    }

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);

    Texture_t *texture = render_make_null();
    texture->width = w;
    texture->height = h;
    texture->id = texture_id;

    if ( border_radius_em > 0 ) {
        texture->border_radius = (float)render_measure_pt_from_em(border_radius_em);
    }

    return texture;
}

Texture_t *render_make_dummy_image(const double border_radius_em) {
    Texture_t *texture = create_test_texture();

    if ( border_radius_em > 0 ) {
        texture->border_radius = (float)render_measure_pt_from_em(border_radius_em);
    }

    return texture;
}

void render_draw_rounded_rect(const Texture_t *null_tex, const Bounds_t *bounds, const Color_t *color,
                              const float border_radius) {
    if ( bounds->w <= 0 ) {
        return;
    }

    set_shader_program(g_renderer->rect_shader);

    float r, g, b, a;
    deconstruct_colors_opengl(color, &r, &g, &b, &a);
    glUniform4f(g_renderer->rect_color_loc, r, g, b, a);
    glUniform2f(g_renderer->rect_pos_loc, (float)bounds->x, (float)bounds->y);
    glUniform2f(g_renderer->rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->rect_radius_loc, border_radius);
    glUniformMatrix4fv(g_renderer->rect_projection_loc, 1, GL_FALSE, get_projection_matrix());

    glBindVertexArray(null_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, null_tex->vbo);

    if ( texture_needs_reconfigure(null_tex, bounds) ) {
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
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void render_draw_texture(Texture_t *texture, const Bounds_t *at, const DrawTextureOpts_t *opts) {
    if ( texture == NULL || texture->id == 0 ) {
        error_abort("Warning: Attempting to draw invalid texture\n");
    }

    const float scale = MAX(0.f, 1.f + (float)at->scale_mod);
    const float w = (float)(at->w == 0 ? (float)texture->width : at->w) * scale;
    const float h = (float)(at->w == 0 ? (float)texture->height : at->h) * scale;

    if ( at->x + w < 0 || at->x > g_renderer->viewport.w || at->y + h < 0 || at->y > g_renderer->viewport.h ) {
        return;
    }

    const float *projection = get_projection_matrix();

    set_shader_program(g_renderer->texture_shader);

    int num_draw_regions = 0;
    if ( opts->draw_regions != NULL ) {
        num_draw_regions = opts->draw_regions->num_regions;
    }

    float regions[MAX_DRAW_SUB_REGIONS][4] = {0};
    if ( num_draw_regions > 0 ) {
        for ( int i = 0; i < num_draw_regions; i++ ) {
            const DrawRegionOpt_t *region = &opts->draw_regions->regions[i];
            regions[i][0] = region->x0_perc;
            regions[i][1] = region->y0_perc;
            regions[i][2] = region->x1_perc;
            regions[i][3] = region->y1_perc;
        }
    }

    glUniform1f(g_renderer->tex_border_radius_loc, texture->border_radius);
    glUniform1f(g_renderer->tex_alpha_loc, (float)opts->alpha_mod / 255.0f);
    glUniform2f(g_renderer->tex_rect_size_loc, w, h);
    glUniform4f(g_renderer->tex_bounds_loc, (float)at->x, (float)at->y, w, h);
    glUniformMatrix4fv(g_renderer->tex_projection_loc, 1, GL_FALSE, projection);
    glUniform1f(g_renderer->tex_color_mod_loc, opts->color_mod);
    glUniform1i(g_renderer->tex_num_regions_loc, num_draw_regions);
    if ( num_draw_regions > 0 ) {
        glUniform4fv(g_renderer->tex_regions_loc, 4, &regions[0][0]);
    }

    glBindTexture(GL_TEXTURE_2D, texture->id);

    glBindVertexArray(texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, texture->vbo);

    const Bounds_t final_bounds = {.x = at->x, .y = at->y, .w = (int32_t)w, .h = (int32_t)h};
    if ( texture_needs_reconfigure(texture, &final_bounds) ) {
        float vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices((float)at->x, (float)at->y, w, h, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        mark_texture_configured(texture, &final_bounds);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void render_destroy_shadow(Shadow_t *shadow) {
    if ( shadow->texture ) {
        render_destroy_texture(shadow->texture);
    }
    free(shadow);
}

Shadow_t *render_make_shadow(Texture_t *texture, const Bounds_t *src_bounds, const float blur_radius, const int32_t offset) {
    const int32_t padding = offset / 2; // Leave some pixels for the blur
    const int32_t width = (int32_t)src_bounds->w + offset + padding, height = (int32_t)src_bounds->h + offset + padding;

    render_make_texture_target(width, height);
    Bounds_t bounds = {.x = offset, .y = offset, .w = src_bounds->w, .h = src_bounds->h};
    DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 0.f};
    render_draw_texture(texture, &bounds, &opts);
    // Erase the original texture
    const BlendMode_t saved_blend = render_get_blend_mode();
    render_set_blend_mode(BLEND_MODE_ERASE);

    opts.color_mod = 1.f;
    bounds.x = bounds.y = 0;
    render_draw_texture(texture, &bounds, &opts);

    render_set_blend_mode(saved_blend);

    Texture_t *result = render_restore_texture_target();

    result->border_radius = texture->border_radius;
    if ( blur_radius > 0.f ) {
        result = render_blur_texture_replace(result, blur_radius);
    }

    Shadow_t *shadow = calloc(1, sizeof(*shadow));
    shadow->offset = offset;
    shadow->texture = result;
    shadow->bounds = (Bounds_t){.w = width, .h = height};

    return shadow;
}
