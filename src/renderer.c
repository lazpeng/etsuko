/**
 * renderer.c - OpenGL-based rendering backend
 */

#include "renderer.h"

#include "config.h"
#include "constants.h"
#include "error.h"
#include "events.h"

#include "contrib/stb_image.h"
#include "contrib/stb_truetype.h"
#include "str_utils.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <assert.h>
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
#include <glad/gl.h>
#define GLSL_VERSION "#version 330 core\n"
#define GLSL_PRECISION ""
#endif

#define RESOURCE_INCLUDE_SHADERS
#include "resource_includes.h"

#define DEFAULT_WIDTH (1280)
#define DEFAULT_HEIGHT (720)
#define DEFAULT_PT (16)
// 1MB
#define MAX_SHADER_SIZE (1 * 1024 * 1024)
#define QUAD_VERTICES_SIZE (4 /*points*/ * 3 /*vertices per triangle*/ * 2 /*triangles*/)
#define PROJECTION_MATRIX_SIZE (16)
#define MAX_SCISSOR_STACK (16)
#define REVEAL_FADE_EM (1.5)
#define BACKGROUND_RENDER_DIVISOR (3)
#define NOISE_FIELD_SHORT_AXIS (192)
#define IMAGE_BLUR_TIME_PERIOD (2000.0 * M_PI)

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
    GLint region_fade_loc;
    GLint num_erase_regions_loc;
    GLint erase_regions_loc;
    GLint blur_radius_loc;
    GLint fb_tex_loc;
    GLint use_fb_tex_loc;
    GLint fb_tex_origin_loc;
    GLint fb_tex_size_loc;
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

typedef struct ImageBlurShaderData_t {
    GLuint id;
    GLint resolution_loc;
    GLint time_loc;
    GLint noise_loc;
    GLint image_loc;
    GLint image_prev_loc;
    GLint image_fade_loc;
    GLint projection_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
} ImageBlurShaderData_t;

typedef struct NoiseFieldShaderData_t {
    GLuint id;
    GLint resolution_loc;
    GLint extent_loc;
    GLint time_loc;
    GLint projection_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
} NoiseFieldShaderData_t;

typedef struct BackgroundUpscaleShaderData_t {
    GLuint id;
    GLint texture_loc;
    GLint grain_offset_loc;
    GLint border_radius_loc;
    GLint rect_size_loc;
    GLint projection_loc;
    GLint use_bounds_loc;
    GLint bounds_loc;
} BackgroundUpscaleShaderData_t;

typedef struct ShaderData_t {
    GLuint active_shader_program;
    TextureShaderData_t tex;
    RectShaderData_t rect;
    SimpleGradientShaderData_t grad;
    ImageBlurShaderData_t image_blur;
    NoiseFieldShaderData_t noise_field;
    BackgroundUpscaleShaderData_t bg_upscale;
} ShaderData_t;

typedef struct BlurData_t {
    Texture_t *fb_texture;
    GLuint fb_fbo;
    Bounds_t container_bounds;
    bool in_ctx;
} BlurData_t;

typedef struct Renderer_t {
    GLFWwindow *window;
    Bounds_t viewport;
    WEAK MAYBE_NULL RenderTarget_t *render_target;
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

static void create_quad_vertices_with_uv(const float x, const float y, const float w, const float h, const float u0,
                                         const float v0, const float u1, const float v1, float *dest_vertices) {
    const float vertices[] = {x, y + h, u0, v1, x,     y, u0, v0, x + w, y,     u1, v0,
                              x, y + h, u0, v1, x + w, y, u1, v0, x + w, y + h, u1, v1};
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

static void draw_blurred_bg_at(const Bounds_t *bounds, const float blur_radius) {
    const Texture_t *fb_tex = g_renderer->blur_data.fb_texture;
    if ( fb_tex == NULL )
        return;

    const Bounds_t c = g_renderer->blur_data.container_bounds;
    const float cx = (float)c.x, cy = (float)c.y;
    const float cw = (float)c.w, ch = (float)c.h;
    const float x = (float)bounds->x, y = (float)bounds->y;
    const float w = (float)bounds->w, h = (float)bounds->h;

    const float u0 = (x - cx) / cw;
    const float u1 = (x + w - cx) / cw;
    const float v0 = 1.f - (y - cy) / ch;
    const float v1 = 1.f - (y + h - cy) / ch;

    set_shader_program(g_renderer->shaders.tex.id);
    glUniformMatrix4fv(g_renderer->shaders.tex.projection_loc, 1, GL_FALSE, get_projection_matrix());
    glUniform1f(g_renderer->shaders.tex.border_radius_loc, 0.f);
    glUniform1f(g_renderer->shaders.tex.alpha_loc, 1.f);
    glUniform2f(g_renderer->shaders.tex.rect_size_loc, w, h);
    glUniform1i(g_renderer->shaders.tex.use_bounds_loc, 0);
    glUniform1f(g_renderer->shaders.tex.color_mod_loc, 1.f);
    glUniform1i(g_renderer->shaders.tex.num_regions_loc, 0);
    glUniform2f(g_renderer->shaders.tex.region_fade_loc, 0.f, 0.f);
    glUniform1i(g_renderer->shaders.tex.num_erase_regions_loc, 0);
    glUniform1f(g_renderer->shaders.tex.blur_radius_loc, blur_radius);
    glUniform1i(g_renderer->shaders.tex.use_fb_tex_loc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_tex->id);
    glBindVertexArray(fb_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, fb_tex->vbo);

    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices_with_uv(x, y, w, h, u0, v0, u1, v1, vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static float resolve_background_border_radius(const Background_t *background, const Bounds_t *bounds) {
    if ( background->border_radius_em > 0 ) {
        return (float)render_measure_pixels_from_em(background->border_radius_em);
    }
    // AUTO mode
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
    if ( !gladLoadGL((GLADloadfunc)glfwGetProcAddress) ) {
        error_abort("Failed to initialize glad");
    }
    glfwSwapInterval(config_get()->vsync ? 1 : 0);
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
    g_renderer->shaders.image_blur.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_image_blur_frag_shader, "image_blur");
    g_renderer->shaders.noise_field.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_noise_field_frag_shader, "noise_field");
    g_renderer->shaders.bg_upscale.id =
        create_shader_program(buffer, incbin_default_vert_shader, incbin_background_upscale_frag_shader, "bg_upscale");
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
    g_renderer->shaders.tex.region_fade_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_region_fade");
    g_renderer->shaders.tex.num_erase_regions_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_num_erase_regions");
    g_renderer->shaders.tex.erase_regions_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_erase_regions");
    g_renderer->shaders.tex.blur_radius_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_blurRadius");
    g_renderer->shaders.tex.fb_tex_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_fb_tex");
    g_renderer->shaders.tex.use_fb_tex_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_useFbTex");
    g_renderer->shaders.tex.fb_tex_origin_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_fbTexOrigin");
    g_renderer->shaders.tex.fb_tex_size_loc = glGetUniformLocation(g_renderer->shaders.tex.id, "u_fbTexSize");

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

    // Get uniform locations for the image blur shader
    g_renderer->shaders.image_blur.resolution_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_resolution");
    g_renderer->shaders.image_blur.time_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_time");
    g_renderer->shaders.image_blur.image_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_image");
    g_renderer->shaders.image_blur.image_prev_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_image_prev");
    g_renderer->shaders.image_blur.image_fade_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_image_fade");
    g_renderer->shaders.image_blur.noise_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_noise");
    g_renderer->shaders.image_blur.projection_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_projection");
    g_renderer->shaders.image_blur.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_use_bounds");
    g_renderer->shaders.image_blur.bounds_loc = glGetUniformLocation(g_renderer->shaders.image_blur.id, "u_bounds");

    // Get uniform locations for the noise field shader
    g_renderer->shaders.noise_field.resolution_loc = glGetUniformLocation(g_renderer->shaders.noise_field.id, "u_resolution");
    g_renderer->shaders.noise_field.extent_loc = glGetUniformLocation(g_renderer->shaders.noise_field.id, "u_extent");
    g_renderer->shaders.noise_field.time_loc = glGetUniformLocation(g_renderer->shaders.noise_field.id, "u_time");
    g_renderer->shaders.noise_field.projection_loc = glGetUniformLocation(g_renderer->shaders.noise_field.id, "u_projection");
    g_renderer->shaders.noise_field.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.noise_field.id, "u_use_bounds");
    g_renderer->shaders.noise_field.bounds_loc = glGetUniformLocation(g_renderer->shaders.noise_field.id, "u_bounds");

    // Get uniform locations for the background upscale shader
    g_renderer->shaders.bg_upscale.texture_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_texture");
    g_renderer->shaders.bg_upscale.grain_offset_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_grainOffset");
    g_renderer->shaders.bg_upscale.border_radius_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_borderRadius");
    g_renderer->shaders.bg_upscale.rect_size_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_rectSize");
    g_renderer->shaders.bg_upscale.projection_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_projection");
    g_renderer->shaders.bg_upscale.use_bounds_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_use_bounds");
    g_renderer->shaders.bg_upscale.bounds_loc = glGetUniformLocation(g_renderer->shaders.bg_upscale.id, "u_bounds");
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
    glDeleteProgram(g_renderer->shaders.image_blur.id);
    glDeleteProgram(g_renderer->shaders.noise_field.id);
    glDeleteProgram(g_renderer->shaders.bg_upscale.id);

    // Destroy GL context (GLFW destroys context with window)
    glfwDestroyWindow(g_renderer->window);

    if ( g_renderer->blur_data.fb_texture != NULL )
        render_destroy_texture(g_renderer->blur_data.fb_texture);
    if ( g_renderer->blur_data.fb_fbo != 0 )
        glDeleteFramebuffers(1, &g_renderer->blur_data.fb_fbo);

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

    g_renderer->viewport = (Bounds_t){.x = 0, .y = 0, .w = (double)outW, .h = (double)outH};

    if ( g_renderer->blur_data.fb_texture != NULL ) {
        render_destroy_texture(g_renderer->blur_data.fb_texture);
        g_renderer->blur_data.fb_texture = NULL;
    }
    if ( g_renderer->blur_data.fb_fbo != 0 ) {
        glDeleteFramebuffers(1, &g_renderer->blur_data.fb_fbo);
        g_renderer->blur_data.fb_fbo = 0;
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

static void restore_render_target_binding(void) {
    const RenderTarget_t *render_target = g_renderer->render_target;
    if ( render_target == NULL ) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, (int32_t)g_renderer->viewport.w, (int32_t)g_renderer->viewport.h);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, render_target->fbo);
        glViewport(0, 0, render_target->width, render_target->height);
    }
}

static void configure_render_target(RenderTarget_t *render_target, const int32_t width, const int32_t height) {
    assert(width > 0 && height > 0);
    assert(render_target->texture == NULL);

    render_target->width = width;
    render_target->height = height;
    render_target->texture = render_make_empty(width, height);
    create_orthographic_matrix(0.f, (float)width, 0.f, (float)height, render_target->projection);

    glBindFramebuffer(GL_FRAMEBUFFER, render_target->fbo);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_target->texture->id, 0);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    restore_render_target_binding();
}

RenderTarget_t *render_make_render_target(const int32_t width, const int32_t height) {
    RenderTarget_t *render_target = calloc(1, sizeof(*render_target));
    if ( render_target == NULL ) {
        error_abort("Failed to allocate render target");
    }

    glGenFramebuffers(1, &render_target->fbo);
    configure_render_target(render_target, width, height);

    return render_target;
}

bool render_target_ensure_configured(RenderTarget_t *render_target, const int32_t width, const int32_t height) {
    if ( render_target->texture != NULL && render_target->width == width && render_target->height == height )
        return false;

    Texture_t *previous = render_target->texture;
    render_target->texture = NULL;
    configure_render_target(render_target, width, height);

    if ( previous != NULL ) {
        // Copy old contents over to the new texture
        const BlendMode_t saved_blend = g_renderer->blend_mode;
        render_set_blend_mode(BLEND_MODE_NONE);
        render_target_bind(render_target);

        const Bounds_t at = {.x = 0, .y = 0, .w = width, .h = height};
        const DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 1.f};
        render_draw_texture(previous, &at, &opts);

        render_target_unbind(render_target);
        render_set_blend_mode(saved_blend);
        render_destroy_texture(previous);
    }

    return true;
}

void render_target_bind(RenderTarget_t *render_target) {
    assert(render_target->texture != NULL);

    for ( const RenderTarget_t *bound = g_renderer->render_target; bound != NULL; bound = bound->previous ) {
        assert(bound != render_target);
    }

    render_target->previous = g_renderer->render_target;
    g_renderer->render_target = render_target;
    restore_render_target_binding();
}

void render_target_unbind(RenderTarget_t *render_target) {
    if ( g_renderer->render_target != render_target ) {
        error_abort("Unbinding a render target that is not the currently bound one");
    }

    g_renderer->render_target = render_target->previous;
    render_target->previous = NULL;
    restore_render_target_binding();
}

Texture_t *render_target_detach_texture(RenderTarget_t *render_target) {
    assert(g_renderer->render_target != render_target);
    // TODO: Check if it's on the render target stack, not only the current bound target

    Texture_t *texture = render_target->texture;
    if ( texture == NULL ) {
        error_abort("render_target_detach_texture: Texture was already detached from the target");
    }
    render_target->texture = NULL;

    glBindFramebuffer(GL_FRAMEBUFFER, render_target->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    restore_render_target_binding();

    return texture;
}

void render_destroy_render_target(RenderTarget_t *render_target) {
    if ( render_target == NULL )
        return;

    if ( render_target->texture != NULL )
        render_destroy_texture(render_target->texture);
    if ( render_target->fbo != 0 )
        glDeleteFramebuffers(1, &render_target->fbo);
    free(render_target);
}

static void draw_background_buffer_quad(Texture_t *texture, const int32_t width, const int32_t height) {
    const Bounds_t at = {.x = 0, .y = 0, .w = width, .h = height};

    glBindVertexArray(texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, texture->vbo);
    if ( texture_needs_reconfigure(texture, &at) ) {
        float vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices(0.f, 0.f, (float)width, (float)height, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        mark_texture_configured(texture, &at);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void draw_image_blur_bg(Background_t *background, const Bounds_t *bounds) {
    // Nothing to draw until render_background_set_image has been called
    if ( background->image_tex == NULL )
        return;

    const int32_t width = (int32_t)bounds->w, height = (int32_t)bounds->h;
    if ( width <= 0 || height <= 0 )
        return;

    const int32_t low_w = MAX(1, width / BACKGROUND_RENDER_DIVISOR);
    const int32_t low_h = MAX(1, height / BACKGROUND_RENDER_DIVISOR);

    // Matched to the background's aspect so the baked fields resolve the same on both axes
    int32_t noise_w = NOISE_FIELD_SHORT_AXIS, noise_h = NOISE_FIELD_SHORT_AXIS;
    if ( width >= height ) {
        noise_w = (int32_t)((double)NOISE_FIELD_SHORT_AXIS * width / height + 0.5);
    } else {
        noise_h = (int32_t)((double)NOISE_FIELD_SHORT_AXIS * height / width + 0.5);
    }
    noise_w = MAX(1, MIN(noise_w, low_w));
    noise_h = MAX(1, MIN(noise_h, low_h));

    if ( background->image_prev_tex != NULL && background->image_fade >= 1.f ) {
        render_destroy_texture(background->image_prev_tex);
        background->image_prev_tex = NULL;
    }
    const float fade = background->image_prev_tex != NULL ? background->image_fade : 1.f;
    const Texture_t *prev = background->image_prev_tex != NULL ? background->image_prev_tex : background->image_tex;

    const double elapsed = events_get_elapsed_time();
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    // Both offscreen passes write every pixel of their own target, so there is nothing to blend with
    render_set_blend_mode(BLEND_MODE_NONE);

    // Pass 1: bake the flow, swirl and smoke fields
    if ( background->noise_target == NULL ) {
        background->noise_target = render_make_render_target(noise_w, noise_h);
    } else {
        render_target_ensure_configured(background->noise_target, noise_w, noise_h);
    }
    render_target_bind(background->noise_target);

    const NoiseFieldShaderData_t *noise = &g_renderer->shaders.noise_field;
    set_shader_program(noise->id);
    glUniformMatrix4fv(noise->projection_loc, 1, GL_FALSE, background->noise_target->projection);
    glUniform1i(noise->use_bounds_loc, 1);
    glUniform4f(noise->bounds_loc, 0.f, 0.f, (float)noise_w, (float)noise_h);
    glUniform2f(noise->resolution_loc, (float)noise_w, (float)noise_h);
    // The field domain belongs to the background, not to this buffer: short axis spanning one unit
    const float short_axis = (float)MIN(width, height);
    glUniform2f(noise->extent_loc, (float)width / short_axis, (float)height / short_axis);
    glUniform1f(noise->time_loc, (float)elapsed);
    draw_background_buffer_quad(background->noise_target->texture, noise_w, noise_h);
    render_target_unbind(background->noise_target);

    // Pass 2: the effect itself, at a fraction of the final size
    if ( background->low_res_target == NULL ) {
        background->low_res_target = render_make_render_target(low_w, low_h);
    } else {
        render_target_ensure_configured(background->low_res_target, low_w, low_h);
    }
    render_target_bind(background->low_res_target);

    const ImageBlurShaderData_t *shader = &g_renderer->shaders.image_blur;
    set_shader_program(shader->id);
    glUniformMatrix4fv(shader->projection_loc, 1, GL_FALSE, background->low_res_target->projection);
    glUniform1i(shader->use_bounds_loc, 1);
    glUniform4f(shader->bounds_loc, 0.f, 0.f, (float)low_w, (float)low_h);
    glUniform2f(shader->resolution_loc, (float)low_w, (float)low_h);
    glUniform1f(shader->time_loc, (float)fmod(elapsed, IMAGE_BLUR_TIME_PERIOD));
    glUniform1f(shader->image_fade_loc, fade);
    glUniform1i(shader->image_loc, 0);
    glUniform1i(shader->image_prev_loc, 1);
    glUniform1i(shader->noise_loc, 2);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, background->noise_target->texture->id);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, prev->id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, background->image_tex->id);

    draw_background_buffer_quad(background->low_res_target->texture, low_w, low_h);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    render_target_unbind(background->low_res_target);

    // Pass 3: scale the result up onto whatever was being drawn into
    render_set_blend_mode(BLEND_MODE_BLEND);

    const BackgroundUpscaleShaderData_t *upscale = &g_renderer->shaders.bg_upscale;
    set_shader_program(upscale->id);
    glUniformMatrix4fv(upscale->projection_loc, 1, GL_FALSE, get_projection_matrix());
    glUniform1i(upscale->use_bounds_loc, 1);
    glUniform4f(upscale->bounds_loc, (float)bounds->x, (float)bounds->y, (float)width, (float)height);
    glUniform1f(upscale->border_radius_loc, resolve_background_border_radius(background, bounds));
    glUniform2f(upscale->rect_size_loc, (float)width, (float)height);
    // Only shifts the dither from frame to frame, so any bounded value that keeps moving will do
    glUniform1f(upscale->grain_offset_loc, 71.f * (float)(elapsed * 0.37 - floor(elapsed * 0.37)));
    glUniform1i(upscale->texture_loc, 0);

    glBindTexture(GL_TEXTURE_2D, background->low_res_target->texture->id);

    glBindVertexArray(background->null_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, background->null_tex->vbo);
    if ( texture_needs_reconfigure(background->null_tex, bounds) ) {
        float vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices((float)bounds->x, (float)bounds->y, (float)width, (float)height, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        mark_texture_configured(background->null_tex, bounds);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    render_set_blend_mode(saved_blend);
}

static Texture_t *internal_create_gradient_background_texture(Background_t *background, const Bounds_t *bounds) {
    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const int32_t width = (int32_t)bounds->w, height = (int32_t)bounds->h;
    if ( background->gradient_target == NULL ) {
        background->gradient_target = render_make_render_target(width, height);
    } else {
        render_target_ensure_configured(background->gradient_target, width, height);
    }
    render_target_bind(background->gradient_target);

    set_shader_program(g_renderer->shaders.grad.id);

    const float w = (float)width, h = (float)height;

    float r, g, b, a;
    deconstruct_colors_opengl(&background->primary_color, &r, &g, &b, &a);
    glUniform4f(g_renderer->shaders.grad.top_color_loc, r, g, b, a);
    deconstruct_colors_opengl(&background->secondary_color, &r, &g, &b, &a);
    glUniform4f(g_renderer->shaders.grad.bottom_color_loc, r, g, b, a);
    glUniformMatrix4fv(g_renderer->shaders.grad.projection_loc, 1, GL_FALSE, background->gradient_target->projection);

    float quadVertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices(0, 0, w, h, quadVertices);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(background->gradient_target->texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, background->gradient_target->texture->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    render_target_unbind(background->gradient_target);
    Texture_t *texture = render_target_detach_texture(background->gradient_target);

    render_set_blend_mode(saved_blend);
    return texture;
}

#define BLUR_CTX_STACK_MAX 16
static BlurData_t g_blur_ctx_stack[BLUR_CTX_STACK_MAX];
static int g_blur_ctx_depth = 0;

void render_push_blur_ctx(const Bounds_t *container_bounds) {
    assert(g_blur_ctx_depth < BLUR_CTX_STACK_MAX);
    g_blur_ctx_stack[g_blur_ctx_depth++] = g_renderer->blur_data;
    g_renderer->blur_data = (BlurData_t){.container_bounds = *container_bounds, .in_ctx = true};
}

void render_pop_blur_ctx(void) {
    if ( g_renderer->blur_data.fb_texture != NULL ) {
        render_destroy_texture(g_renderer->blur_data.fb_texture);
    }
    if ( g_renderer->blur_data.fb_fbo != 0 ) {
        glDeleteFramebuffers(1, &g_renderer->blur_data.fb_fbo);
    }
    g_renderer->blur_data = g_blur_ctx_stack[--g_blur_ctx_depth];
}

static void ensure_fb_snapshot(void) {
    if ( !g_renderer->blur_data.in_ctx )
        return;
    if ( g_renderer->blur_data.fb_texture != NULL )
        return;

    const Bounds_t b = g_renderer->blur_data.container_bounds;
    const int32_t cw = (int32_t)b.w;
    const int32_t ch = (int32_t)b.h;
    const int32_t cx = (int32_t)b.x;
    const int32_t fb_y = (int32_t)g_renderer->viewport.h - (int32_t)b.y - ch;

    g_renderer->blur_data.fb_texture = render_make_empty(cw, ch);

    glBindTexture(GL_TEXTURE_2D, g_renderer->blur_data.fb_texture->id);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cx, fb_y, cw, ch);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create an FBO backed by the snapshot texture so we can stamp blurred drawables into it
    // as they are drawn. This keeps the snapshot current, so each subsequent drawable that
    // samples from it sees the already-composited result of all previous drawables rather than
    // just the raw pre-container background.
    glGenFramebuffers(1, &g_renderer->blur_data.fb_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer->blur_data.fb_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_renderer->blur_data.fb_texture->id, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    if ( background != NULL ) {
        if ( background->null_tex != NULL )
            render_destroy_texture(background->null_tex);
        if ( background->image_tex != NULL )
            render_destroy_texture(background->image_tex);
        if ( background->image_prev_tex != NULL )
            render_destroy_texture(background->image_prev_tex);
        render_destroy_render_target(background->gradient_target);
        render_destroy_render_target(background->low_res_target);
        render_destroy_render_target(background->noise_target);
        free(background);
    }
}

static void draw_gradient_bg(Background_t *background, const Bounds_t *bounds) {
    const bool is_null = background->null_tex == NULL || background->null_tex->id == 0;
    const bool needs_configure = !is_null && texture_needs_reconfigure(background->null_tex, bounds);
    if ( is_null || needs_configure ) {
        if ( background->null_tex != NULL )
            render_destroy_texture(background->null_tex);
        background->null_tex = internal_create_gradient_background_texture(background, bounds);
    }
    background->null_tex->border_radius = background->border_radius_em > 0 ? resolve_background_border_radius(background, bounds)
                                                                           : (float)background->border_radius_em;
    const BlendMode_t blend_mode = render_get_blend_mode();
    render_set_blend_mode(BLEND_MODE_BLEND);
    if ( background->blur ) {
        ensure_fb_snapshot();
        const float blur_radius = (float)(bounds->w + bounds->h) / 2 * 0.0025f;
        draw_blurred_bg_at(bounds, blur_radius);
    }
    static const DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 1.f};
    render_draw_texture(background->null_tex, bounds, &opts);
    render_set_blend_mode(blend_mode);
}

Texture_t *render_make_empty(const int32_t width, const int32_t height) {
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
    switch ( background->type ) {
    case BACKGROUND_GRADIENT:
        draw_gradient_bg(background, at);
        break;
    case BACKGROUND_IMAGE_BLUR:
        draw_image_blur_bg(background, at);
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

// Size of the blurred copy.
#define BLURRED_IMAGE_SIZE (128)
// Half-width of the OKLab a/b range the blurred image is encoded over. Must match AB_RANGE in the image blur shader.
#define OKLAB_AB_RANGE (0.32f)
// Gaussian sigma of the blur as a fraction of the side
#define BLURRED_IMAGE_SIGMA (0.09)

// Index folded back into [0, n) the same way GL_MIRRORED_REPEAT will show the texture past its edges
static int32_t mirror_index(int32_t i, const int32_t n) {
    if ( i < 0 )
        i = -i - 1;
    if ( i >= n )
        i = 2 * n - i - 1;
    return i;
}

// Offset of element i along a row (horizontal) or column of a square RGB float image
static size_t axis_offset(const int32_t line, const int32_t i, const int32_t size, const bool horizontal) {
    return (size_t)(horizontal ? line * size + i : i * size + line) * 3;
}

// One box blur pass along one axis. Three passes per axis are close enough to a Gaussian of the same sigma
static void box_blur_pass(const float *src, float *dst, const int32_t size, const int32_t radius, const bool horizontal) {
    const float inv = 1.f / (float)(2 * radius + 1);
    for ( int32_t line = 0; line < size; line++ ) {
        for ( int32_t c = 0; c < 3; c++ ) {
            float sum = 0.f;
            for ( int32_t i = -radius; i <= radius; i++ )
                sum += src[axis_offset(line, mirror_index(i, size), size, horizontal) + c];
            for ( int32_t i = 0; i < size; i++ ) {
                dst[axis_offset(line, i, size, horizontal) + c] = sum * inv;
                sum += src[axis_offset(line, mirror_index(i + radius + 1, size), size, horizontal) + c] -
                       src[axis_offset(line, mirror_index(i - radius, size), size, horizontal) + c];
            }
        }
    }
}

// Area-averaging downsample of an RGB byte image into a square float image. Non-square art gets stretched, which keeps
// all of it in the texture and which the blur hides anyway
static void downsample_box(const unsigned char *src, const int32_t w, const int32_t h, float *dst, const int32_t size) {
    for ( int32_t oy = 0; oy < size; oy++ ) {
        const int32_t y0 = oy * h / size;
        const int32_t y1 = MAX(y0 + 1, (oy + 1) * h / size);
        for ( int32_t ox = 0; ox < size; ox++ ) {
            const int32_t x0 = ox * w / size;
            const int32_t x1 = MAX(x0 + 1, (ox + 1) * w / size);
            float sum[3] = {0.f, 0.f, 0.f};
            for ( int32_t y = y0; y < y1; y++ ) {
                for ( int32_t x = x0; x < x1; x++ ) {
                    const unsigned char *p = src + ((size_t)y * w + x) * 3;
                    sum[0] += (float)p[0];
                    sum[1] += (float)p[1];
                    sum[2] += (float)p[2];
                }
            }
            const float inv = 1.f / (float)((y1 - y0) * (x1 - x0));
            float *out = dst + ((size_t)oy * size + ox) * 3;
            out[0] = sum[0] * inv;
            out[1] = sum[1] * inv;
            out[2] = sum[2] * inv;
        }
    }
}

// Encodes one 0..255 sRGB pixel as the OKLab the image blur shader samples
static void encode_oklab_pixel(const float *rgb255, unsigned char *out) {
    float linear[3];
    for ( int c = 0; c < 3; c++ ) {
        const float v = rgb255[c] / 255.f;
        linear[c] = powf(v < 0.f ? 0.f : (v > 1.f ? 1.f : v), 2.2f);
    }

    // OKLab conversion by Bjorn Ottosson, https://bottosson.github.io/posts/oklab/ (public domain, MIT as an alternative)
    const float r = linear[0], g = linear[1], b = linear[2];
    const float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

    const float encoded[3] = {
        0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
        (1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s) / (2.f * OKLAB_AB_RANGE) + 0.5f,
        (0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s) / (2.f * OKLAB_AB_RANGE) + 0.5f,
    };
    for ( int c = 0; c < 3; c++ ) {
        const float v = encoded[c] * 255.f + 0.5f;
        out[c] = (unsigned char)(v < 0.f ? 0.f : (v > 255.f ? 255.f : v));
    }
}

BlurredBackgroundImage_t *render_make_blurred_image(const unsigned char *bytes, const int length) {
    int w, h, channels;
    unsigned char *decoded = stbi_load_from_memory(bytes, length, &w, &h, &channels, 3);
    if ( decoded == NULL ) {
        fprintf(stderr, "Failed to decode image for the blurred background\n");
        return NULL;
    }

    const int32_t size = BLURRED_IMAGE_SIZE;
    float *img = malloc(sizeof(float) * (size_t)size * size * 3);
    float *tmp = malloc(sizeof(float) * (size_t)size * size * 3);
    if ( img == NULL || tmp == NULL )
        error_abort("Failed to allocate blurred image buffers");

    downsample_box(decoded, w, h, img, size);
    stbi_image_free(decoded);

    const int32_t radius = (int32_t)(BLURRED_IMAGE_SIGMA * size + 0.5);
    for ( int pass = 0; pass < 3; pass++ ) {
        box_blur_pass(img, tmp, size, radius, true);
        box_blur_pass(tmp, img, size, radius, false);
    }
    free(tmp);

    BlurredBackgroundImage_t *image = calloc(1, sizeof(*image));
    if ( image == NULL )
        error_abort("Failed to allocate blurred image");
    image->width = size;
    image->height = size;
    image->pixels = malloc((size_t)size * size * 4);
    if ( image->pixels == NULL )
        error_abort("Failed to allocate blurred image pixels");
    for ( size_t i = 0; i < (size_t)size * size; i++ ) {
        encode_oklab_pixel(&img[i * 3], &image->pixels[i * 4]);
        image->pixels[i * 4 + 3] = 0xFF;
    }
    free(img);

    return image;
}

void render_destroy_blurred_image(BlurredBackgroundImage_t *image) {
    if ( image == NULL )
        return;
    free(image->pixels);
    free(image);
}

void render_background_set_image(Background_t *background, const BlurredBackgroundImage_t *image) {
    Texture_t *texture = calloc(1, sizeof(*texture));
    if ( texture == NULL )
        error_abort("Failed to allocate background image texture");
    texture->width = image->width;
    texture->height = image->height;

    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    if ( background->image_prev_tex != NULL )
        render_destroy_texture(background->image_prev_tex);
    background->image_prev_tex = background->image_tex;
    background->image_tex = texture;
    background->image_fade = 1.f;
}

void render_set_blend_mode(const BlendMode_t mode) {
    if ( mode == g_renderer->blend_mode )
        return;
    g_renderer->blend_mode = mode;

    switch ( mode ) {
    case BLEND_MODE_BLEND:
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

    // Add some padding on characters that have a left bearing (e.g. J) so it doesn't cut off too abruptly
    int left_pad = 0;
    {
        int32_t first_i = 0;
        const int32_t first_c = str_u8_next(text, len, &first_i);
        if ( first_c >= 0 ) {
            int bx0;
            stbtt_GetCodepointBitmapBoxSubpixel(font, first_c, scale, scale, 0, 0, &bx0, NULL, NULL, NULL);
            if ( bx0 < 0 )
                left_pad = -bx0;
        }
    }
    // Add 1 pixel for good measure
    if ( left_pad > 0 )
        left_pad++;
    width += left_pad;

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
            const int target_x = (int)x + c_xoff + left_pad;
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
    texture->left_bearing_offset = left_pad;

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
        texture->border_radius = (float)render_measure_pixels_from_em(border_radius_em);
    } else if ( border_radius_em < 0 ) {
        texture->border_radius = BORDER_RADIUS_AUTO;
    }

    return texture;
}

Texture_t *render_make_dummy_image(const double border_radius_em) {
    Texture_t *texture = create_test_texture();

    if ( border_radius_em > 0 ) {
        texture->border_radius = (float)render_measure_pixels_from_em(border_radius_em);
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

static void stamp_into_fb_texture(void) {
    const Bounds_t c = g_renderer->blur_data.container_bounds;

    float fb_proj[PROJECTION_MATRIX_SIZE];
    create_orthographic_matrix((float)c.x, (float)(c.x + c.w), (float)(c.y + c.h), (float)c.y, fb_proj);

    // Unbind texture 1 in case it's bound to something because writing to and reading from
    // the same texture in webgl is undefined behavior.
    // and in the cases I tested specifically, even though the texture isn't being read from in this case,
    // causes visual artifacts
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer->blur_data.fb_fbo);
    glViewport(0, 0, (GLsizei)c.w, (GLsizei)c.h);
    glUniformMatrix4fv(g_renderer->shaders.tex.projection_loc, 1, GL_FALSE, fb_proj);
    glUniform1i(g_renderer->shaders.tex.use_fb_tex_loc, 0);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei)g_renderer->viewport.w, (GLsizei)g_renderer->viewport.h);
    glUniformMatrix4fv(g_renderer->shaders.tex.projection_loc, 1, GL_FALSE, get_projection_matrix());
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

    double x = at->x - (double)texture->left_bearing_offset, y = at->y;
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
    float region_fades[MAX_DRAW_SUB_REGIONS][2] = {0};
    const float fade_width = (float)render_measure_pixels_from_em(REVEAL_FADE_EM);
    for ( int i = 0; i < num_draw_regions; i++ ) {
        const DrawRegionOpt_t *region = &opts->draw_regions->regions[i];
        regions[i][0] = region->x0_perc;
        regions[i][1] = region->y0_perc;
        regions[i][2] = region->x1_perc;
        regions[i][3] = region->y1_perc;
        // The anchor may lie beyond this region's own x1 for scaled segment redraws that sit
        // just behind the edge, keeping the ramp continuous across draw calls. The ramp is
        // truncated so it never reaches behind the animation start nor past its target,
        // growing from and shrinking back to zero width at the segment boundaries.
        const float anchor = region->fade_anchor_perc > 0.f ? region->fade_anchor_perc : region->x1_perc;
        const float travelled = (anchor - region->anim_x1_from_perc) * w;
        const float remaining = (region->anim_x1_to_perc - anchor) * w;
        region_fades[i][0] = MAX(0.f, MIN(fade_width, MIN(travelled, remaining)));
        region_fades[i][1] = anchor;
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
    glUniform2fv(g_renderer->shaders.tex.region_fade_loc, MAX_DRAW_SUB_REGIONS, &region_fades[0][0]);
    glUniform1i(g_renderer->shaders.tex.num_erase_regions_loc, num_erase_regions);
    if ( num_erase_regions > 0 ) {
        glUniform4fv(g_renderer->shaders.tex.erase_regions_loc, MAX_SCALE_SUB_REGIONS, &erase_regions[0][0]);
    }

    const float blur_radius = opts->blur_radius;
    glUniform1f(g_renderer->shaders.tex.blur_radius_loc, blur_radius);
    const bool use_fb_tex = blur_radius > 0.f && opts->blur_with_bg;
    glUniform1i(g_renderer->shaders.tex.use_fb_tex_loc, use_fb_tex);
    if ( use_fb_tex ) {
        ensure_fb_snapshot();
        if ( g_renderer->blur_data.fb_texture != NULL ) {
            const Bounds_t b = g_renderer->blur_data.container_bounds;
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, g_renderer->blur_data.fb_texture->id);
            glUniform1i(g_renderer->shaders.tex.fb_tex_loc, 1);
            glUniform2f(g_renderer->shaders.tex.fb_tex_origin_loc, (float)b.x,
                        (float)g_renderer->viewport.h - (float)b.y - (float)b.h);
            glUniform2f(g_renderer->shaders.tex.fb_tex_size_loc, (float)b.w, (float)b.h);
            glActiveTexture(GL_TEXTURE0);
        }
    }

    glBindTexture(GL_TEXTURE_2D, texture->id);

    glBindVertexArray(texture->vao);
    glBindBuffer(GL_ARRAY_BUFFER, texture->vbo);

    // Pad the texture when blur is active so we get a smoother fade at the edges
    const float blur_pad = (blur_radius > 0.f && border_radius == 0.f) ? blur_radius * 4.0f : 0.f;
    const Bounds_t final_bounds = {
        .x = x - blur_pad, .y = y - blur_pad, .w = (int32_t)(w + 2.f * blur_pad), .h = (int32_t)(h + 2.f * blur_pad)};
    if ( texture_needs_reconfigure(texture, &final_bounds) ) {
        float vertices[QUAD_VERTICES_SIZE] = {0};
        if ( blur_pad > 0.f ) {
            create_quad_vertices_with_uv((float)(x - blur_pad), (float)(y - blur_pad), w + 2.f * blur_pad, h + 2.f * blur_pad,
                                         -blur_pad / w, -blur_pad / h, 1.f + blur_pad / w, 1.f + blur_pad / h, vertices);
        } else {
            create_quad_vertices((float)x, (float)y, w, h, vertices);
        }
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        mark_texture_configured(texture, &final_bounds);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Stamp into the blur context's fb_texture so the snapshot stays current
    if ( g_renderer->render_target == NULL && g_renderer->blur_data.fb_fbo != 0 && (blur_radius > 0.f || opts->blur_with_bg) ) {
        stamp_into_fb_texture();
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

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
        bounds.x -= bounds.w * region->relative_scale * center_x - region->pos_x_offset;
        bounds.y -= bounds.h * region->relative_scale * center_y - region->pos_y_offset;
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

        // Clamp the scaled segment to the revealed portion of its draw region. The fade
        // state is inherited from the owning row regardless of clamping: the ramp is
        // anchored at the row's reveal edge, so a segment that finished just behind the
        // still moving edge keeps its share of the ramp instead of snapping to opaque.
        for ( int dr = 0; dr < num_draw_regions; dr++ ) {
            const DrawRegionOpt_t *draw_region = &opts->draw_regions->regions[dr];
            if ( draw_region->y0_perc <= scaled_region_opt->y0_perc && draw_region->y1_perc >= scaled_region_opt->y1_perc ) {
                scaled_region_opt->anim_x1_from_perc = draw_region->anim_x1_from_perc;
                scaled_region_opt->anim_x1_to_perc = draw_region->anim_x1_to_perc;
                scaled_region_opt->fade_anchor_perc = draw_region->x1_perc;
                if ( scaled_region_opt->x1_perc > draw_region->x1_perc ) {
                    scaled_region_opt->x1_perc = draw_region->x1_perc;
                }
            }
        }

        new_opts.draw_regions = &scaled_region;
        render_draw_texture(texture, &bounds, &new_opts);
    }
}

void render_destroy_shadow(Shadow_t *shadow) {
    if ( shadow->texture )
        render_destroy_texture(shadow->texture);
    free(shadow);
}

Shadow_t *render_make_shadow(Texture_t *texture, const Bounds_t *src_bounds, const int32_t offset) {
    const int32_t padding = offset / 2; // Leave some pixels for the blur
    const int32_t width = (int32_t)src_bounds->w + offset + padding, height = (int32_t)src_bounds->h + offset + padding;

    RenderTarget_t *render_target = render_make_render_target(width, height);
    render_target_bind(render_target);

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

    render_target_unbind(render_target);
    Texture_t *result = render_target_detach_texture(render_target);

    result->border_radius = texture->border_radius;

    const float blur_r = (float)offset / 2.f;
    if ( blur_r > 0.f ) {
        // Recreate texture on the render target
        render_target_ensure_configured(render_target, width, height);
        render_target_bind(render_target);

        const Bounds_t blur_bounds = {.x = 0, .y = 0, .w = width, .h = height};
        const DrawTextureOpts_t blur_opts = {.alpha_mod = 255, .color_mod = 1.f, .blur_radius = blur_r};
        render_draw_texture(result, &blur_bounds, &blur_opts);
        render_destroy_texture(result);

        render_target_unbind(render_target);
        result = render_target_detach_texture(render_target);
        result->border_radius = texture->border_radius;
    }

    render_destroy_render_target(render_target);

    Shadow_t *shadow = calloc(1, sizeof(*shadow));
    shadow->offset = offset;
    shadow->texture = result;
    shadow->bounds = (Bounds_t){.w = width, .h = height};

    return shadow;
}
