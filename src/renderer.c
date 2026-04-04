/**
 * renderer.c - OpenGL-based rendering backend
 */

#include "renderer.h"

#include "constants.h"
#include "error.h"
#include "events.h"

#include "contrib/stb_image.h"
#include "contrib/stb_truetype.h"
#include "str_utils.h"

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

#define DEFAULT_WIDTH (1280)
#define DEFAULT_HEIGHT (720)
#define DEFAULT_PT (16)
#define BASE_DPI 96.f
// 1MB
#define MAX_SHADER_SIZE (1 * 1024 * 1024)
#define QUAD_VERTICES_SIZE (4 /*points*/ * 3 /*vertices per triangle*/ * 2 /*triangles*/)
#define PROJECTION_MATRIX_SIZE (16)
#define MAX_SCISSOR_STACK (16)

typedef struct FontData_t {
    stbtt_fontinfo ui_font_info, lyrics_font_info;
    unsigned char *ui_font_data, *lyrics_font_data;
} FontData_t;

typedef struct TextureShaderData_t {
    GLuint id;
    GLint projection_loc;
    GLint alpha_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
    GLint border_radius_loc;
    GLint rect_size_loc;
    GLint color_mod_loc;
    GLint num_regions_loc;
    GLint regions_loc;
    GLint num_erase_regions_loc;
    GLint erase_regions_loc;
} TextureShaderData_t;

typedef struct RectShaderData_t {
    GLuint id;
    GLint projection_loc;
    GLint color_loc;
    GLint pos_loc;
    GLint size_loc;
    GLint radius_loc;
} RectShaderData_t;

typedef struct SimpleGradientShaderData_t {
    GLuint id;
    GLint top_color_loc;
    GLint bottom_color_loc;
    GLint projection_loc;
} SimpleGradientShaderData_t;

typedef struct BlurShaderData_t {
    GLuint id;
    GLint texture_loc;
    GLint direction_loc;
    GLint size_loc;
    GLint projection_loc;
} BlurShaderData_t;

typedef struct RandomGradientShaderData_t {
    GLuint id;
    GLint time_loc;
    GLint resolution_loc;
    GLint projection_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
    GLint border_radius_loc;
    GLint rect_size_loc;
} RandomGradientShaderData_t;

typedef struct DynamicGradientShaderData_t {
    GLuint id;
    GLint time_loc;
    GLint noise_mag_loc;
    GLint colors;
    GLint resolution_loc;
    GLint projection_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
    GLint border_radius_loc;
    GLint rect_size_loc;
} DynamicGradientShaderData_t;

typedef struct AmLikeShaderData_t {
    GLuint id;
    GLint resolution_loc;
    GLint time_loc;
    GLint colors_loc;
    GLint projection_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
    GLint border_radius_loc;
    GLint rect_size_loc;
} AmLikeShaderData_t;

typedef struct CopyShaderData_t {
    GLuint id;
    GLint projection_loc;
} CopyShaderData_t;

typedef struct ShaderData_t {
    GLuint active_shader_program;
    TextureShaderData_t tex;
    RectShaderData_t rect;
    SimpleGradientShaderData_t grad;
    BlurShaderData_t blur;
    RandomGradientShaderData_t rand_grad;
    DynamicGradientShaderData_t dyn_grad;
    AmLikeShaderData_t am_like;
    CopyShaderData_t copy;
} ShaderData_t;

#define FB_GRID_COLS 20
#define FB_GRID_ROWS 20

typedef struct BlurData_t {
    Texture_t *fb_texture;
    GLuint fbo;
    bool fb_grid_invalid[FB_GRID_COLS][FB_GRID_ROWS];
} BlurData_t;

typedef struct Renderer_t {
    GLFWwindow *window;
    Bounds_t viewport;
    double h_dpi, v_dpi;
    RenderTarget_t *render_target;
    float projection_matrix[PROJECTION_MATRIX_SIZE];
    BlendMode_t blend_mode;
    double window_pixel_scale;
    FontData_t fonts;
    ShaderData_t shaders;
    BlurData_t blur_data;
} Renderer_t;

static Renderer_t *g_renderer = NULL;

static float *get_projection_matrix(void);

static GLuint compile_shader(const GLenum type, const char *source, const char *name) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if ( !success ) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        const char *type_str = "unknown";
        switch ( type ) {
        case GL_VERTEX_SHADER:
            type_str = "vert";
            break;
        case GL_FRAGMENT_SHADER:
            type_str = "frag";
            break;
        // ReSharper disable once CppDFAUnreachableCode
        default:
            break;
        }
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
        printf("Shader linking failed for %s:\n%s\n", program_name, log);
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
    if ( g_renderer->shaders.active_shader_program != program ) {
        glUseProgram(program);
        g_renderer->shaders.active_shader_program = program;
    }
}

static float resolve_background_border_radius(const Background_t *background, const Bounds_t *bounds) {
    if ( background->border_radius_em > 0 ) {
        return (float)render_measure_pt_from_em(background->border_radius_em);
    }
    if ( background->border_radius_em < 0 ) {
        return (float)MIN(bounds->w, bounds->h) * 0.5f;
    }
    return 0.f;
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

    render_on_window_changed();

    g_renderer->blend_mode = BLEND_MODE_NONE;
    render_set_blend_mode(BLEND_MODE_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Compile shaders
    char *buffer = begin_shader_compilation();
    g_renderer->shaders.tex.id = create_shader_program(buffer, incbin_default_vert_shader, incbin_texture_frag_shader, "tex");
    g_renderer->shaders.rect.id = create_shader_program(buffer, incbin_default_vert_shader, incbin_rect_frag_shader, "rect");
    g_renderer->shaders.grad.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_gradient_frag_shader, "gradient");
    g_renderer->shaders.dyn_grad.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_dyn_gradient_frag_shader, "dyn_gradient");
    g_renderer->shaders.am_like.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_am_gradient_frag_shader, "am_gradient");
    g_renderer->shaders.rand_grad.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_rand_gradient_frag_shader, "rand_gradient");
    g_renderer->shaders.blur.id = create_shader_program(buffer, incbin_default_vert_shader, incbin_blur_frag_shader, "blur");
    g_renderer->shaders.copy.id = create_shader_program(buffer, incbin_default_vert_shader, incbin_copy_frag_shader, "copy");
    end_shader_compilation(buffer);

    // Get uniform locations for texture shader
    g_renderer->shaders.tex.projection_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_projection");
    g_renderer->shaders.tex.alpha_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_alpha");
    g_renderer->shaders.tex.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_use_bounds");
    g_renderer->shaders.tex.bounds_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_bounds");
    g_renderer->shaders.tex.border_radius_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_borderRadius");
    g_renderer->shaders.tex.rect_size_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_rectSize");
    g_renderer->shaders.tex.color_mod_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_colorModFactor");
    g_renderer->shaders.tex.num_regions_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_num_regions");
    g_renderer->shaders.tex.regions_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_regions");
    g_renderer->shaders.tex.num_erase_regions_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_num_erase_regions");
    g_renderer->shaders.tex.erase_regions_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_erase_regions");

    // Get uniform locations for rect shader
    g_renderer->shaders.rect.projection_loc = glGetUniformLocation(g_renderer->shaders.rect.id, "u_projection");
    g_renderer->shaders.rect.color_loc = glGetUniformLocation(g_renderer->shaders.rect.id, "u_color");
    g_renderer->shaders.rect.pos_loc = glGetUniformLocation(g_renderer->shaders.rect.id, "u_rectPos");
    g_renderer->shaders.rect.size_loc = glGetUniformLocation(g_renderer->shaders.rect.id, "u_rectSize");
    g_renderer->shaders.rect.radius_loc = glGetUniformLocation(g_renderer->shaders.rect.id, "u_cornerRadius");

    // Get uniform locations for the gradient shader
    g_renderer->shaders.grad.top_color_loc = glGetUniformLocation(g_renderer->shaders.grad.id, "u_topColor");
    g_renderer->shaders.grad.bottom_color_loc = glGetUniformLocation(g_renderer->shaders.grad.id, "u_bottomColor");
    g_renderer->shaders.grad.projection_loc = glGetUniformLocation(g_renderer->shaders.grad.id, "u_projection");

    // Get uniform locations for the random gradient shader
    g_renderer->shaders.rand_grad.time_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_time");
    g_renderer->shaders.rand_grad.resolution_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_resolution");
    g_renderer->shaders.rand_grad.projection_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_projection");
    g_renderer->shaders.rand_grad.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_use_bounds");
    g_renderer->shaders.rand_grad.bounds_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_bounds");
    g_renderer->shaders.rand_grad.border_radius_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_borderRadius");
    g_renderer->shaders.rand_grad.rect_size_loc = glGetUniformLocation(g_renderer->shaders.rand_grad.id, "u_rectSize");

    // Get uniform locations for the dynamic gradient shader
    g_renderer->shaders.dyn_grad.time_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_time");
    g_renderer->shaders.dyn_grad.noise_mag_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_noise_magnitude");
    g_renderer->shaders.dyn_grad.colors = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_colors");
    g_renderer->shaders.dyn_grad.resolution_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_resolution");
    g_renderer->shaders.dyn_grad.projection_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_projection");
    g_renderer->shaders.dyn_grad.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_use_bounds");
    g_renderer->shaders.dyn_grad.bounds_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_bounds");
    g_renderer->shaders.dyn_grad.border_radius_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_borderRadius");
    g_renderer->shaders.dyn_grad.rect_size_loc = glGetUniformLocation(g_renderer->shaders.dyn_grad.id, "u_rectSize");

    // Get uniform locations for blur shader
    g_renderer->shaders.blur.texture_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_texture");
    g_renderer->shaders.blur.direction_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_direction");
    g_renderer->shaders.blur.size_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_blur_size");
    g_renderer->shaders.blur.projection_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_projection");

    // Get uniform locations for the AM-like shader
    g_renderer->shaders.am_like.resolution_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "iResolution");
    g_renderer->shaders.am_like.time_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "iTime");
    g_renderer->shaders.am_like.colors_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "iColors");
    g_renderer->shaders.am_like.projection_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "u_projection");
    g_renderer->shaders.am_like.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "u_use_bounds");
    g_renderer->shaders.am_like.bounds_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "u_bounds");
    g_renderer->shaders.am_like.border_radius_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "u_borderRadius");
    g_renderer->shaders.am_like.rect_size_loc = glGetUniformLocation(g_renderer->shaders.am_like.id, "u_rectSize");

    // Get uniform locations for copy shader
    g_renderer->shaders.copy.projection_loc = glGetUniformLocation(g_renderer->shaders.copy.id, "u_projection");
}

void render_finish(void) {
    if ( g_renderer == NULL ) {
        return;
    }

    // Unload fonts
    if ( g_renderer->fonts.ui_font_data != NULL )
        free(g_renderer->fonts.ui_font_data);
    if ( g_renderer->fonts.lyrics_font_data != NULL )
        free(g_renderer->fonts.lyrics_font_data);

    // Delete OpenGL objects
    glDeleteProgram(g_renderer->shaders.tex.id);
    glDeleteProgram(g_renderer->shaders.rect.id);
    glDeleteProgram(g_renderer->shaders.grad.id);
    glDeleteProgram(g_renderer->shaders.dyn_grad.id);
    glDeleteProgram(g_renderer->shaders.rand_grad.id);
    glDeleteProgram(g_renderer->shaders.blur.id);
    glDeleteProgram(g_renderer->shaders.am_like.id);

    // Destroy GL context (GLFW destroys context with window)
    glfwDestroyWindow(g_renderer->window);

    if ( g_renderer->blur_data.fb_texture != NULL )
        render_destroy_texture(g_renderer->blur_data.fb_texture);

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

    if ( g_renderer->blur_data.fb_texture != NULL ) {
        render_destroy_texture(g_renderer->blur_data.fb_texture);
        g_renderer->blur_data.fb_texture = NULL;
    }

    glViewport(0, 0, outW, outH);
    update_projection_matrix();
}

static void deconstruct_colors_opengl(const Color_t *color, float *r, float *g, float *b, float *a) {
    // ReSharper disable once CppDFAConstantConditions
    if ( r )
        *r = (float)color->r / 255.0f;
    // ReSharper disable once CppDFAConstantConditions
    if ( g )
        *g = (float)color->g / 255.0f;
    // ReSharper disable once CppDFAConstantConditions
    if ( b )
        *b = (float)color->b / 255.0f;
    // ReSharper disable once CppDFAConstantConditions
    if ( a )
        *a = (float)color->a / 255.0f;
}

static void draw_random_gradient_bg(const Background_t *background, const Bounds_t *bounds) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    const float border_radius = resolve_background_border_radius(background, bounds);
    render_set_blend_mode(BLEND_MODE_BLEND);

    set_shader_program(g_renderer->shaders.rand_grad.id);
    glBindVertexArray(background->null_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, background->null_tex->vbo);

    if ( texture_needs_reconfigure(background->null_tex, bounds) ) {
        float quad_vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices((float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h, quad_vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), &quad_vertices, GL_STATIC_DRAW);
        mark_texture_configured(background->null_tex, bounds);
    }

    glUniformMatrix4fv(g_renderer->shaders.rand_grad.projection_loc, 1, GL_FALSE, get_projection_matrix());
    glUniform1i(g_renderer->shaders.rand_grad.use_bounds_loc, 1);
    glUniform4f(g_renderer->shaders.rand_grad.bounds_loc, (float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.rand_grad.border_radius_loc, border_radius);
    glUniform2f(g_renderer->shaders.rand_grad.rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.rand_grad.time_loc, (float)events_get_elapsed_time());
    glUniform2f(g_renderer->shaders.rand_grad.resolution_loc, (float)bounds->w, (float)bounds->h);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    render_set_blend_mode(saved_blend);
}

static void draw_dynamic_gradient_bg(const Background_t *background, const Bounds_t *bounds) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    const float border_radius = resolve_background_border_radius(background, bounds);
    render_set_blend_mode(BLEND_MODE_BLEND);

    set_shader_program(g_renderer->shaders.dyn_grad.id);

    glBindVertexArray(background->null_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, background->null_tex->vbo);

    if ( texture_needs_reconfigure(background->null_tex, bounds) ) {
        float quad_vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices((float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h, quad_vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), &quad_vertices, GL_STATIC_DRAW);
        mark_texture_configured(background->null_tex, bounds);
    }

    glUniformMatrix4fv(g_renderer->shaders.dyn_grad.projection_loc, 1, GL_FALSE, get_projection_matrix());
    glUniform1i(g_renderer->shaders.dyn_grad.use_bounds_loc, 1);
    glUniform4f(g_renderer->shaders.dyn_grad.bounds_loc, (float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.dyn_grad.border_radius_loc, border_radius);
    glUniform2f(g_renderer->shaders.dyn_grad.rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.dyn_grad.time_loc, (float)events_get_elapsed_time() / 5.f);
    glUniform1f(g_renderer->shaders.dyn_grad.noise_mag_loc, 0.1f);
    glUniform2f(g_renderer->shaders.dyn_grad.resolution_loc, (float)bounds->w, (float)bounds->h);

    float colors[5][4];
    for ( int i = 0; i < 5; i++ ) {
        const Color_t *color = &background->colors[i];
        float *r = &colors[i][0];
        float *g = &colors[i][1];
        float *b = &colors[i][2];
        float *a = &colors[i][3];
        deconstruct_colors_opengl(color, r, g, b, a);
    }
    glUniform4fv(g_renderer->shaders.dyn_grad.colors, 5, &colors[0][0]);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    render_set_blend_mode(saved_blend);
}

static void draw_am_like_bg(const Background_t *background, const Bounds_t *bounds) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    const float border_radius = resolve_background_border_radius(background, bounds);
    render_set_blend_mode(BLEND_MODE_BLEND);

    set_shader_program(g_renderer->shaders.am_like.id);
    glBindVertexArray(background->null_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, background->null_tex->vbo);

    if ( texture_needs_reconfigure(background->null_tex, bounds) ) {
        float quad_vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices((float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h, quad_vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), &quad_vertices, GL_STATIC_DRAW);
        mark_texture_configured(background->null_tex, bounds);
    }

    glUniformMatrix4fv(g_renderer->shaders.am_like.projection_loc, 1, GL_FALSE, get_projection_matrix());
    glUniform1i(g_renderer->shaders.am_like.use_bounds_loc, 1);
    glUniform4f(g_renderer->shaders.am_like.bounds_loc, (float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.am_like.border_radius_loc, border_radius);
    glUniform2f(g_renderer->shaders.am_like.rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.am_like.time_loc, (float)events_get_elapsed_time());
    glUniform3f(g_renderer->shaders.am_like.resolution_loc, (float)bounds->w, (float)bounds->h, 0.f);

    float colors[5][4];
    for ( int i = 0; i < 5; i++ ) {
        const Color_t *color = &background->colors[i];
        float *r = &colors[i][0];
        float *g = &colors[i][1];
        float *b = &colors[i][2];
        float *a = &colors[i][3];
        deconstruct_colors_opengl(color, r, g, b, a);
    }
    glUniform4fv(g_renderer->shaders.am_like.colors_loc, 5, &colors[0][0]);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    render_set_blend_mode(saved_blend);
}

static Texture_t *internal_create_gradient_background_texture(const Background_t *background, const Bounds_t *bounds) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = (int32_t)bounds->w, height = (int32_t)bounds->h;
    const RenderTarget_t *target = render_make_texture_target(width, height); // TODO: Avoid recreating this

    set_shader_program(g_renderer->shaders.grad.id);

    const float w = (float)width, h = (float)height;

    float r, g, b, a;
    deconstruct_colors_opengl(&background->colors[0], &r, &g, &b, &a);
    glUniform4f(g_renderer->shaders.grad.top_color_loc, r, g, b, a);
    deconstruct_colors_opengl(&background->colors[1], &r, &g, &b, &a);
    glUniform4f(g_renderer->shaders.grad.bottom_color_loc, r, g, b, a);
    glUniformMatrix4fv(g_renderer->shaders.grad.projection_loc, 1, GL_FALSE, target->projection);

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

static void invalidate_cached_fb_portion(const Bounds_t *at) {
    const double vp_w = g_renderer->viewport.w;
    const double vp_h = g_renderer->viewport.h;

    int col_min = (int)(at->x * FB_GRID_COLS / vp_w);
    int col_max = (int)((at->x + at->w) * FB_GRID_COLS / vp_w);
    int row_min = (int)(at->y * FB_GRID_ROWS / vp_h);
    int row_max = (int)((at->y + at->h) * FB_GRID_ROWS / vp_h);

    col_min = col_min < 0 ? 0 : col_min >= FB_GRID_COLS ? FB_GRID_COLS - 1 : col_min;
    col_max = col_max < 0 ? 0 : col_max >= FB_GRID_COLS ? FB_GRID_COLS - 1 : col_max;
    row_min = row_min < 0 ? 0 : row_min >= FB_GRID_ROWS ? FB_GRID_ROWS - 1 : row_min;
    row_max = row_max < 0 ? 0 : row_max >= FB_GRID_ROWS ? FB_GRID_ROWS - 1 : row_max;

    for ( int c = col_min; c <= col_max; c++ ) {
        for ( int r = row_min; r <= row_max; r++ ) {
            g_renderer->blur_data.fb_grid_invalid[c][r] = true;
        }
    }
}

static bool is_cached_fb_invalid_for_portion(const Bounds_t *at) {
    const double vp_w = g_renderer->viewport.w;
    const double vp_h = g_renderer->viewport.h;

    int col_min = (int)(at->x * FB_GRID_COLS / vp_w);
    int col_max = (int)((at->x + at->w) * FB_GRID_COLS / vp_w);
    int row_min = (int)(at->y * FB_GRID_ROWS / vp_h);
    int row_max = (int)((at->y + at->h) * FB_GRID_ROWS / vp_h);

    col_min = col_min < 0 ? 0 : col_min >= FB_GRID_COLS ? FB_GRID_COLS - 1 : col_min;
    col_max = col_max < 0 ? 0 : col_max >= FB_GRID_COLS ? FB_GRID_COLS - 1 : col_max;
    row_min = row_min < 0 ? 0 : row_min >= FB_GRID_ROWS ? FB_GRID_ROWS - 1 : row_min;
    row_max = row_max < 0 ? 0 : row_max >= FB_GRID_ROWS ? FB_GRID_ROWS - 1 : row_max;

    for ( int c = col_min; c <= col_max; c++ ) {
        for ( int r = row_min; r <= row_max; r++ ) {
            if ( g_renderer->blur_data.fb_grid_invalid[c][r] ) {
                return true;
            }
        }
    }
    return false;
}

static bool clear_cached_fb_grid(void) {
    memset(g_renderer->blur_data.fb_grid_invalid, 0, sizeof(g_renderer->blur_data.fb_grid_invalid));
    return true;
}

static void draw_portion_of_fb(const Bounds_t *at) {
    const float vp_w = (float)g_renderer->viewport.w;
    const float vp_h = (float)g_renderer->viewport.h;
    const int32_t ivp_w = (int32_t)vp_w;
    const int32_t ivp_h = (int32_t)vp_h;

    if ( g_renderer->blur_data.fb_texture == NULL ) {
        g_renderer->blur_data.fb_texture = render_make_empty(ivp_w, ivp_h);
        memset(g_renderer->blur_data.fb_grid_invalid, 1, sizeof(g_renderer->blur_data.fb_grid_invalid));
    }

    const Texture_t *fb_tex = g_renderer->blur_data.fb_texture;
    if ( is_cached_fb_invalid_for_portion(at) ) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, fb_tex->id);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ivp_w, ivp_h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_renderer->blur_data.fbo);
        clear_cached_fb_grid();
    }

    const float u0 = (float)at->x / vp_w;
    const float u1 = (float)(at->x + at->w) / vp_w;
    const float v_screen_top = (vp_h - (float)at->y) / vp_h;
    const float v_screen_bot = (vp_h - (float)(at->y + at->h)) / vp_h;

    const float dst_w = (float)at->w, dst_h = (float)at->h;
    const float vertices[QUAD_VERTICES_SIZE] = {
        0.f, dst_h, u0, v_screen_bot, 0.f,   0.f, u0, v_screen_top, dst_w, 0.f,   u1, v_screen_top,
        0.f, dst_h, u0, v_screen_bot, dst_w, 0.f, u1, v_screen_top, dst_w, dst_h, u1, v_screen_bot,
    };

    set_shader_program(g_renderer->shaders.copy.id);
    glUniformMatrix4fv(g_renderer->shaders.copy.projection_loc, 1, GL_FALSE, g_renderer->render_target->projection);
    glBindTexture(GL_TEXTURE_2D, fb_tex->id);
    glBindVertexArray(fb_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, fb_tex->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static const RenderTarget_t *set_render_target(GLuint fbo, Texture_t *target_texture) {
    RenderTarget_t *target = calloc(1, sizeof(*target));
    if ( target == NULL ) {
        error_abort("Failed to allocate render target");
    }

    target->fbo = fbo;
    target->texture = target_texture;

    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    target->prev_target = g_renderer->render_target;
    g_renderer->render_target = target;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture->id, 0);

    const int32_t width = target_texture->width, height = target_texture->height;
    glViewport(0, 0, width, height);
    create_orthographic_matrix(0.0f, (float)width, 0.0f, (float)height, target->projection);
    render_set_blend_mode(saved_blend);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    return target;
}

const RenderTarget_t *render_make_texture_target(const int32_t width, const int32_t height) {
    Texture_t *texture = render_make_empty(width, height);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);

    const RenderTarget_t *target = set_render_target(fbo, texture);

    return target;
}

Texture_t *render_blur_texture(Texture_t *target, const Texture_t *source, const Bounds_t *bounds, const float blur_radius) {
    if ( !source || blur_radius <= 0 || source->width <= 0 || source->height <= 0 ) {
        error_abort("render_blur_texture_radius: Invalid params");
    }

    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = source->width, height = source->height;
    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices(0, 0, (float)width, (float)height, vertices);

    const Texture_t *blur_source = source;
    Texture_t *composite_texture = NULL;

    if ( bounds != NULL ) {
        const RenderTarget_t *composite_target = render_make_texture_target(width, height);
        draw_portion_of_fb(bounds);

        render_set_blend_mode(BLEND_MODE_BLEND);
        set_shader_program(g_renderer->shaders.tex.id);
        glUniformMatrix4fv(g_renderer->shaders.tex.projection_loc, 1, GL_FALSE, composite_target->projection);
        glUniform1f(g_renderer->shaders.tex.alpha_loc, 1.0f);
        glUniform2f(g_renderer->shaders.tex.rect_size_loc, (float)width, (float)height);
        glUniform1i(g_renderer->shaders.tex.use_bounds_loc, 0);
        glUniform1f(g_renderer->shaders.tex.border_radius_loc, 0.0f);
        glUniform1f(g_renderer->shaders.tex.color_mod_loc, 1.0f);
        glUniform1i(g_renderer->shaders.tex.num_regions_loc, 0);
        glUniform1i(g_renderer->shaders.tex.num_erase_regions_loc, 0);
        glBindTexture(GL_TEXTURE_2D, source->id);
        glBindVertexArray(composite_target->texture->vao);
        glBindBuffer(GL_ARRAY_BUFFER, composite_target->texture->vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        composite_texture = render_restore_texture_target();
        blur_source = composite_texture;
    }

    // TODO: Avoid creating a new texture every time here
    render_set_blend_mode(BLEND_MODE_NONE);
    const RenderTarget_t *h_blur_target = render_make_texture_target(width, height);

    set_shader_program(g_renderer->shaders.blur.id);
    glUniformMatrix4fv(g_renderer->shaders.blur.projection_loc, 1, GL_FALSE, h_blur_target->projection);
    glUniform1f(g_renderer->shaders.blur.size_loc, blur_radius);
    glUniform2f(g_renderer->shaders.blur.direction_loc, 1.0f, 0.0f);

    glBindTexture(GL_TEXTURE_2D, blur_source->id);
    glBindVertexArray(h_blur_target->texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, h_blur_target->texture->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    Texture_t *h_blurred_texture = render_restore_texture_target();
    if ( composite_texture != NULL )
        render_destroy_texture(composite_texture);

    GLuint temp_fbo;
    glGenFramebuffers(1, &temp_fbo);
    set_render_target(temp_fbo, target);

    glUniform2f(g_renderer->shaders.blur.direction_loc, 0.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, h_blurred_texture->id);
    glBindVertexArray(h_blurred_texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, h_blurred_texture->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    Texture_t *result = render_restore_texture_target();
    result->border_radius = source->border_radius;
    render_destroy_texture(h_blurred_texture);

    render_set_blend_mode(saved_blend);
    return result;
}

void render_clear(void) {
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

Background_t *render_make_background(const BackgroundType_t type) {
    Background_t *background = calloc(1, sizeof(*background));
    background->null_tex = render_make_null();
    background->type = type;
    background->border_radius_em = 0;

    return background;
}

void render_destroy_background(Background_t *background) {
    if ( background != NULL )
        free(background);
}

static void draw_gradient_bg(Background_t *background, const Bounds_t *bounds) {
    const bool is_null = background->null_tex == NULL || background->null_tex->id == 0;
    const bool needs_configure = !is_null && texture_needs_reconfigure(background->null_tex, bounds);
    if ( is_null || needs_configure ) {
        if ( background->null_tex != NULL )
            render_destroy_texture(background->null_tex);
        background->null_tex = internal_create_gradient_background_texture(background, bounds);
    }
    static DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 1.f};
    background->null_tex->border_radius = background->border_radius_em > 0 ? resolve_background_border_radius(background, bounds)
                                                                           : (float)background->border_radius_em;
    const BlendMode_t blend_mode = render_get_blend_mode();
    render_set_blend_mode(BLEND_MODE_BLEND);
    render_draw_texture(background->null_tex, bounds, &opts);
    render_set_blend_mode(blend_mode);
}

Texture_t *render_make_empty(int32_t width, int32_t height) {
    Texture_t *tex = render_make_null();
    glGenTextures(1, &tex->id);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    tex->width = width;
    tex->height = height;
    return tex;
}

void render_draw_background(Background_t *background, const Bounds_t *at) {
    if ( background->blur ) {
        if ( background->empty_tex == NULL )
            background->empty_tex = render_make_empty((int32_t)at->w, (int32_t)at->h);
        if ( background->blur_tex == NULL ) {
            background->blur_tex = render_make_empty((int32_t)at->w, (int32_t)at->h);
        }
        const float blur_radius = (float)(at->w + at->h) / 2 * 0.0025f;
        render_blur_texture(background->blur_tex, background->empty_tex, at, blur_radius);

        const DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 1.f};
        const BlendMode_t blend_mode = render_get_blend_mode();
        render_set_blend_mode(BLEND_MODE_NONE);
        render_draw_texture(background->blur_tex, at, &opts);
        render_set_blend_mode(blend_mode);
    }

    switch ( background->type ) {
    case BACKGROUND_GRADIENT:
        draw_gradient_bg(background, at);
        break;
    case BACKGROUND_SANDS_GRADIENT:
        draw_dynamic_gradient_bg(background, at);
        break;
    case BACKGROUND_RANDOM_GRADIENT:
        draw_random_gradient_bg(background, at);
        break;
    case BACKGROUND_AM_LIKE_GRADIENT:
        draw_am_like_bg(background, at);
        break;
    default:
        printf("Warning: Unrecognized background type\n");
        break;
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
        if ( g_renderer->fonts.ui_font_data ) {
            free(g_renderer->fonts.ui_font_data);
        }
        info = &g_renderer->fonts.ui_font_info;
        g_renderer->fonts.ui_font_data = data_copy;
    } else if ( type == FONT_LYRICS ) {
        if ( g_renderer->fonts.lyrics_font_data ) {
            free(g_renderer->fonts.lyrics_font_data);
        }
        info = &g_renderer->fonts.lyrics_font_info;
        g_renderer->fonts.lyrics_font_data = data_copy;
    } else {
        error_abort("Invalid font kind");
    }

    if ( !stbtt_InitFont(info, data_copy, 0) ) {
        error_abort("Could not load font");
    }
}

void render_set_window_title(const char *title) { glfwSetWindowTitle(g_renderer->window, title); }

void render_measure_text_size(const char *text, const int32_t pixels, int32_t *w, int32_t *h, const FontType_t kind) {
    const stbtt_fontinfo *font = kind == FONT_UI ? &g_renderer->fonts.ui_font_info : &g_renderer->fonts.lyrics_font_info;

    const float pixel_height = (float)pixels;
    const float scale = stbtt_ScaleForMappingEmToPixels(font, pixel_height);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);

    if ( h )
        *h = (int32_t)((ascent - descent + lineGap) * (double)scale);

    int width = 0;
    int32_t i = 0;
    const int32_t len = (int32_t)strlen(text);
    int32_t c = 0;
    int32_t prev_c = -1;

    while ( i < len ) {
        c = str_u8_next(text, len, &i);
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

    if ( w )
        *w = (int32_t)(width * (double)scale);
}

int32_t render_measure_pixels_from_em(const double em) {
    const double scale_w = g_renderer->viewport.w / DEFAULT_WIDTH;
    const double scale_h = g_renderer->viewport.h / DEFAULT_HEIGHT;
    const double scale = MIN(scale_w, scale_h);
    const double rem = DEFAULT_PT * scale;
    const double pixels = em * rem;
    return (int32_t)pixels;
}

int32_t render_measure_pt_from_em(const double em) {
    const double pixels = render_measure_pixels_from_em(em);
    const double dpi_scale = BASE_DPI / MIN(g_renderer->h_dpi, g_renderer->v_dpi);
    const int32_t pt_size = (int32_t)lround(pixels * dpi_scale);
    return pt_size;
}

void render_measure_char_bounds(const int32_t c, const int32_t prev_c, const int32_t pixels, CharBounds_t *out_bounds,
                                const FontType_t font) {
    const stbtt_fontinfo *font_info = font == FONT_UI ? &g_renderer->fonts.ui_font_info : &g_renderer->fonts.lyrics_font_info;

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

Color_t render_color_darken(Color_t color, const double amount) {
    color.r = (uint8_t)fmax(0, color.r * (1.0 - amount));
    color.g = (uint8_t)fmax(0, color.g * (1.0 - amount));
    color.b = (uint8_t)fmax(0, color.b * (1.0 - amount));
    return color;
}

static float calculate_color_luminance(const Color_t *color) {
    return 0.299f * (float)color->r + 0.587f * (float)color->g + 0.114f * (float)color->b;
}

void render_sample_bg_colors_from_image(const unsigned char *bytes, const int length, Color_t colors[5]) {
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
                centroids[k].a = 0xFF;
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
        colors[i] = sorted[i].color;
    }

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
    const stbtt_fontinfo *font = font_type == FONT_UI ? &g_renderer->fonts.ui_font_info : &g_renderer->fonts.lyrics_font_info;

    if ( strlen(text) == 0 ) {
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
    int32_t c = 0;
    int32_t prev_c = -1;

    while ( i < len ) {
        c = str_u8_next(text, len, &i);
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
        c = str_u8_next(text, len, &i);
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
    } else if ( border_radius_em < 0 ) {
        texture->border_radius = BORDER_RADIUS_AUTO;
    }

    return texture;
}

Texture_t *render_make_dummy_image(const double border_radius_em) {
    Texture_t *texture = create_test_texture();

    if ( border_radius_em > 0 ) {
        texture->border_radius = (float)render_measure_pt_from_em(border_radius_em);
    } else if ( border_radius_em < 0 ) {
        texture->border_radius = BORDER_RADIUS_AUTO;
    }

    return texture;
}

void render_draw_rounded_rect(const Texture_t *null_tex, const Bounds_t *bounds, const Color_t *color, float border_radius) {
    if ( bounds->w <= 0 ) {
        return;
    }

    set_shader_program(g_renderer->shaders.rect.id);

    if ( border_radius < 0.f )
        border_radius = (float)MIN(bounds->w, bounds->h) * 0.5f;

    float r, g, b, a;
    deconstruct_colors_opengl(color, &r, &g, &b, &a);
    glUniform4f(g_renderer->shaders.rect.color_loc, r, g, b, a);
    glUniform2f(g_renderer->shaders.rect.pos_loc, (float)bounds->x, (float)bounds->y);
    glUniform2f(g_renderer->shaders.rect.size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(g_renderer->shaders.rect.radius_loc, border_radius);
    glUniformMatrix4fv(g_renderer->shaders.rect.projection_loc, 1, GL_FALSE, get_projection_matrix());

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

    const float original_w = at->w == 0 ? (float)texture->width : (float)at->w;
    const float original_h = at->w == 0 ? (float)texture->height : (float)at->h;
    const float scale = MAX(0.f, 1.f + (float)at->scale_mod);
    const float w = (float)(at->w == 0 ? (float)texture->width : at->w) * scale;
    const float h = (float)(at->w == 0 ? (float)texture->height : at->h) * scale;

    double x = at->x, y = at->y;
    if ( scale != 1.f && opts->center_on_scale ) {
        x -= (w - original_w) / 2.f;
        y -= (h - original_h) / 2.f;
    }

    if ( x + w < 0 || x > g_renderer->viewport.w || y + h < 0 || y > g_renderer->viewport.h ) {
        return;
    }

    const float *projection = get_projection_matrix();

    set_shader_program(g_renderer->shaders.tex.id);

    int num_draw_regions = 0;
    if ( opts->draw_regions != NULL ) {
        num_draw_regions = MIN(MAX_DRAW_SUB_REGIONS, opts->draw_regions->num_regions);
    }

    float regions[MAX_DRAW_SUB_REGIONS][4] = {0};
    for ( int i = 0; i < num_draw_regions; i++ ) {
        const DrawRegionOpt_t *region = &opts->draw_regions->regions[i];
        regions[i][0] = region->x0_perc;
        regions[i][1] = region->y0_perc;
        regions[i][2] = region->x1_perc;
        regions[i][3] = region->y1_perc;
    }

    int num_erase_regions = 0;
    if ( opts->scale_regions != NULL ) {
        num_erase_regions = MIN(MAX_SCALE_SUB_REGIONS, opts->scale_regions->num_regions);
    }

    // Erase the portions of the texture that are to be scaled so we redraw them scaled later
    float erase_regions[MAX_SCALE_SUB_REGIONS][4] = {0};
    for ( int i = 0; i < num_erase_regions; i++ ) {
        const ScaleRegionOpt_t *region = &opts->scale_regions->regions[i];
        erase_regions[i][0] = region->x0_perc;
        erase_regions[i][1] = region->y0_perc;
        erase_regions[i][2] = region->x1_perc;
        erase_regions[i][3] = region->y1_perc;
    }

    // auto border radius using half of the smaller axis
    float border_radius = texture->border_radius;
    if ( border_radius < 0.f ) {
        border_radius = MIN(w, h) * 0.5f;
    }
    glUniform1f(g_renderer->shaders.tex.border_radius_loc, border_radius);
    glUniform1f(g_renderer->shaders.tex.alpha_loc, (float)opts->alpha_mod / 255.0f);
    glUniform2f(g_renderer->shaders.tex.rect_size_loc, w, h);
    glUniform1i(g_renderer->shaders.tex.use_bounds_loc, 1);
    glUniform4f(g_renderer->shaders.tex.bounds_loc, (float)x, (float)y, w, h);
    glUniformMatrix4fv(g_renderer->shaders.tex.projection_loc, 1, GL_FALSE, projection);
    glUniform1f(g_renderer->shaders.tex.color_mod_loc, opts->color_mod);
    glUniform1i(g_renderer->shaders.tex.num_regions_loc, num_draw_regions);
    if ( num_draw_regions > 0 ) {
        glUniform4fv(g_renderer->shaders.tex.regions_loc, MAX_DRAW_SUB_REGIONS, &regions[0][0]);
    }
    glUniform1i(g_renderer->shaders.tex.num_erase_regions_loc, num_erase_regions);
    if ( num_erase_regions > 0 ) {
        glUniform4fv(g_renderer->shaders.tex.erase_regions_loc, MAX_SCALE_SUB_REGIONS, &erase_regions[0][0]);
    }

    glBindTexture(GL_TEXTURE_2D, texture->id);

    glBindVertexArray(texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, texture->vbo);

    const Bounds_t final_bounds = {.x = x, .y = y, .w = (int32_t)w, .h = (int32_t)h};
    if ( texture_needs_reconfigure(texture, &final_bounds) ) {
        float vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices((float)x, (float)y, w, h, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        mark_texture_configured(texture, &final_bounds);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    const bool is_default_framebuffer = g_renderer->render_target == NULL;
    if ( is_default_framebuffer ) {
        invalidate_cached_fb_portion(&final_bounds);
    }

    // Re-draw the scaled portions of the texture in separate draw calls
    // this will of course induce additional rebuilding of the VBO and the draw calls themselves but...
    // well, right now it's a lot better than making a separate texture for every text segment
    for ( int i = 0; i < num_erase_regions; i++ ) {
        const ScaleRegionOpt_t *region = &opts->scale_regions->regions[i];
        Bounds_t bounds = *at;
        bounds.scale_mod += region->relative_scale;
        // Also compensate for the scale by centering the texture by the amount scaled
        // relative to the center of the region
        const float center_x = (region->x0_perc + region->x1_perc) / 2.f;
        const float center_y = (region->y0_perc + region->y1_perc) / 2.f;
        bounds.x -= bounds.w * region->relative_scale * center_x;
        bounds.y -= bounds.h * region->relative_scale * center_y;
        // Options will be the same with the exception that the draw region(s) will be just one with the actual bound
        // to be drawn, considering the actual draw regions set
        DrawTextureOpts_t new_opts = *opts;
        new_opts.scale_regions = NULL;
        new_opts.draw_regions = NULL;

        DrawRegionOptSet_t scaled_region = {0};
        scaled_region.num_regions = 1;
        DrawRegionOpt_t *scaled_region_opt = &scaled_region.regions[0];
        scaled_region_opt->x0_perc = region->x0_perc;
        scaled_region_opt->x1_perc = region->x1_perc;
        scaled_region_opt->y0_perc = region->y0_perc;
        scaled_region_opt->y1_perc = region->y1_perc;

        // Now iterate through the set draw regions and set it accordingly
        for ( int dr = 0; dr < num_draw_regions; dr++ ) {
            const DrawRegionOpt_t *draw_region = &opts->draw_regions->regions[dr];
            if ( draw_region->y0_perc <= scaled_region_opt->y0_perc && draw_region->y1_perc >= scaled_region_opt->y1_perc ) {
                scaled_region_opt->x1_perc = MIN(scaled_region_opt->x1_perc, draw_region->x1_perc);
            }
        }

        new_opts.draw_regions = &scaled_region;
        render_draw_texture(texture, &bounds, &new_opts);
    }
}

void render_destroy_shadow(Shadow_t *shadow) {
    if ( shadow->texture )
        render_destroy_texture(shadow->texture);

    if ( shadow->blur_tex )
        render_destroy_texture(shadow->blur_tex);
    free(shadow);
}

Shadow_t *render_make_shadow(Texture_t *texture, const Bounds_t *src_bounds, const int32_t offset) {
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

    Shadow_t *shadow = calloc(1, sizeof(*shadow));
    shadow->offset = offset;
    shadow->texture = result;
    shadow->bounds = (Bounds_t){.w = width, .h = height};
    shadow->blur_tex = render_make_empty(width, height);

    return shadow;
}
