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

// Sigma in source pixels per unit of blur_radius, tuned to look about like the tap grid it replaced
#define BLUR_SIGMA_PER_RADIUS (1.75f)
#define BLUR_RADIUS_QUANTUM (0.25f)
#define BLUR_MIN_RADIUS (0.2f)
// Padding is sized for the top of the bucket a sigma lands in, so a radius moving around inside one bucket never resizes the
// cache
#define BLUR_PAD_BUCKET_SIGMA (2.f)
// Past three sigma the weights round away
#define BLUR_SUPPORT_SIGMAS (3.f)
// Smallest sigma, in reduced texels, that still upscales without the reduced grid showing through
#define BLUR_MIN_REDUCED_SIGMA (1.5f)
// A bilinear fetch covers a 2x2 block and no more, so going further would need a real downsample
#define BLUR_MAX_SCALE (2)
#define BLUR_MAX_TAPS (16)
#define BLUR_CACHE_TTL_FRAMES (120)
#define BLUR_SCRATCH_COUNT (2)

// How long a captured backdrop stands in for the live one. What it samples drifts slowly and the
// blur keeps nothing but that drift, so it holds up for a good few frames
#define BACKDROP_REFRESH_SECONDS (0.08)
#define BACKDROP_SCALE (4)
// Sigma as a fraction of the shorter side of the area being frosted
#define BACKDROP_SIGMA_FRACTION (0.04)
// Captured around it too, so the edges mix with what is really outside instead of a stretched copy
#define BACKDROP_EDGE_SIGMAS (2.f)

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

typedef struct BlurShaderData_t {
    GLuint id;
    GLint tex_loc;
    GLint uv_bounds_loc;
    GLint step_loc;
    GLint num_taps_loc;
    GLint offsets_loc;
    GLint weights_loc;
    GLint unpremultiply_loc;
} BlurShaderData_t;

typedef struct BlurPrepareShaderData_t {
    GLuint id;
    GLint tex_loc;
    GLint scale_loc;
    GLint src_origin_loc;
    GLint src_dir_loc;
    GLint src_radius_loc;
    GLint pad_transparent_loc;
} BlurPrepareShaderData_t;

typedef struct ShaderData_t {
    GLuint active_shader_program;
    TextureShaderData_t tex;
    RectShaderData_t rect;
    SimpleGradientShaderData_t grad;
    ImageBlurShaderData_t image_blur;
    NoiseFieldShaderData_t noise_field;
    BackgroundUpscaleShaderData_t bg_upscale;
    BlurShaderData_t blur;
    BlurPrepareShaderData_t blur_prepare;
} ShaderData_t;

typedef struct BlurCache_t {
    OWNING MAYBE_NULL Texture_t *texture;
    // Quantized radius the contents were built for. Negative means not built yet
    float radius;
    // Room left around the source, in source pixels, and source pixels per texel of the copy
    int32_t pad, scale;
    int32_t src_w, src_h;
    uint64_t last_used_frame;
    WEAK Texture_t *owner;
    WEAK MAYBE_NULL struct BlurCache_t *next;
} BlurCache_t;

typedef struct BlurEngineData_t {
    // Whichever texture a pass writes into is attached to this in turn
    GLuint fbo;
    // Only there for the vertex objects the passes draw their quad with
    OWNING MAYBE_NULL Texture_t *pass_quad;
    // Grown to fit the largest blur seen so far and then left alone
    OWNING MAYBE_NULL Texture_t *scratch[BLUR_SCRATCH_COUNT];
    WEAK MAYBE_NULL BlurCache_t *caches;
    uint64_t frame;
} BlurEngineData_t;

typedef struct Renderer_t {
    GLFWwindow *window;
    Bounds_t viewport;
    WEAK MAYBE_NULL RenderTarget_t *render_target;
    float projection_matrix[PROJECTION_MATRIX_SIZE];
    BlendMode_t blend_mode;
    double window_pixel_scale;
    FontData_t fonts;
    ShaderData_t shaders;
    BlurEngineData_t blur_engine;
} Renderer_t;

static Renderer_t *g_renderer = NULL;

static float *get_projection_matrix(void);
static void restore_render_target_binding(void);

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

typedef struct BlurKernel_t {
    int32_t num_taps;
    float offsets[BLUR_MAX_TAPS];
    float weights[BLUR_MAX_TAPS];
} BlurKernel_t;

typedef struct BlurLayout_t {
    int32_t pad, scale;
    int32_t width, height;
    float sigma;
} BlurLayout_t;

static void build_blur_kernel(const float sigma, BlurKernel_t *kernel) {
    float discrete[(BLUR_MAX_TAPS - 1) * 2 + 1];
    int32_t support = (int32_t)ceilf(BLUR_SUPPORT_SIGMAS * MAX(sigma, 0.01f));
    support = MAX(1, MIN(support, (BLUR_MAX_TAPS - 1) * 2));

    const float denom = 2.f * sigma * sigma;
    float total = 0.f;
    for ( int32_t k = 0; k <= support; k++ ) {
        discrete[k] = expf(-(float)(k * k) / denom);
        total += k == 0 ? discrete[k] : 2.f * discrete[k];
    }

    kernel->num_taps = 1;
    kernel->offsets[0] = 0.f;
    kernel->weights[0] = discrete[0] / total;
    for ( int32_t k = 1; k <= support; k += 2 ) {
        const float w0 = discrete[k];
        const float w1 = k + 1 <= support ? discrete[k + 1] : 0.f;
        const float pair = w0 + w1;

        kernel->offsets[kernel->num_taps] = ((float)k * w0 + (float)(k + 1) * w1) / pair;
        kernel->weights[kernel->num_taps] = pair / total;
        kernel->num_taps++;
    }
}

static BlurLayout_t blur_layout_for(const int32_t src_w, const int32_t src_h, const float radius) {
    BlurLayout_t layout = {0};
    layout.sigma = radius * BLUR_SIGMA_PER_RADIUS;
    layout.scale = MAX(1, MIN(BLUR_MAX_SCALE, (int32_t)(layout.sigma / BLUR_MIN_REDUCED_SIGMA)));

    const float bucket = ceilf(layout.sigma / BLUR_PAD_BUCKET_SIGMA) * BLUR_PAD_BUCKET_SIGMA;
    layout.pad = (int32_t)ceilf(BLUR_SUPPORT_SIGMAS * bucket);

    layout.width = (src_w + 2 * layout.pad + layout.scale - 1) / layout.scale;
    layout.height = (src_h + 2 * layout.pad + layout.scale - 1) / layout.scale;
    return layout;
}

static Texture_t *ensure_blur_scratch(const int32_t index, const int32_t width, const int32_t height) {
    Texture_t *scratch = g_renderer->blur_engine.scratch[index];
    if ( scratch != NULL && scratch->width >= width && scratch->height >= height )
        return scratch;

    const int32_t w = scratch != NULL ? MAX(width, scratch->width) : width;
    const int32_t h = scratch != NULL ? MAX(height, scratch->height) : height;
    if ( scratch != NULL )
        render_destroy_texture(scratch);

    g_renderer->blur_engine.scratch[index] = render_make_empty(w, h);
    return g_renderer->blur_engine.scratch[index];
}

static void draw_blur_pass_quad(const float u0, const float v0, const float u1, const float v1) {
    const Texture_t *quad = g_renderer->blur_engine.pass_quad;

    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices_with_uv(-1.f, -1.f, 2.f, 2.f, u0, v0, u1, v1, vertices);

    glBindVertexArray(quad->vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void bind_blur_target(const Texture_t *target, const int32_t width, const int32_t height) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer->blur_engine.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->id, 0);
    glViewport(0, 0, width, height);
}

typedef struct BlurPassOpts_t {
    // Part of the source that lands on the destination, in source coordinates
    float u0, v0, u1, v1;
    // How much of the source the previous pass wrote
    int32_t src_used_w, src_used_h;
    // One tap unit along the axis of this pass
    float step_x, step_y;
    bool unpremultiply;
} BlurPassOpts_t;

static void run_blur_pass(const Texture_t *src, const Texture_t *dst, const int32_t width, const int32_t height,
                          const BlurKernel_t *kernel, const BlurPassOpts_t *opts) {
    const BlurShaderData_t *shader = &g_renderer->shaders.blur;
    set_shader_program(shader->id);

    bind_blur_target(dst, width, height);

    glUniform1i(shader->tex_loc, 0);
    glUniform4f(shader->uv_bounds_loc, 0.5f / (float)src->width, 0.5f / (float)src->height,
                ((float)opts->src_used_w - 0.5f) / (float)src->width, ((float)opts->src_used_h - 0.5f) / (float)src->height);
    glUniform2f(shader->step_loc, opts->step_x, opts->step_y);
    glUniform1i(shader->num_taps_loc, kernel->num_taps);
    glUniform1fv(shader->offsets_loc, BLUR_MAX_TAPS, kernel->offsets);
    glUniform1fv(shader->weights_loc, BLUR_MAX_TAPS, kernel->weights);
    glUniform1i(shader->unpremultiply_loc, opts->unpremultiply);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src->id);

    draw_blur_pass_quad(opts->u0, opts->v0, opts->u1, opts->v1);
}

typedef struct BlurPrepareOpts_t {
    int32_t scale;
    // Source texel the destination starts at, and which way each axis runs from there
    int32_t origin_x, origin_y, dir_x, dir_y;
    // Corner to cut out of the source, in source pixels
    float src_radius;
    bool pad_transparent;
} BlurPrepareOpts_t;

static void run_blur_prepare_pass(const Texture_t *src, const Texture_t *dst, const int32_t width, const int32_t height,
                                  const BlurPrepareOpts_t *opts) {
    const BlurPrepareShaderData_t *shader = &g_renderer->shaders.blur_prepare;
    set_shader_program(shader->id);

    bind_blur_target(dst, width, height);

    glUniform1i(shader->tex_loc, 0);
    glUniform1i(shader->scale_loc, opts->scale);
    glUniform2i(shader->src_origin_loc, opts->origin_x, opts->origin_y);
    glUniform2i(shader->src_dir_loc, opts->dir_x, opts->dir_y);
    glUniform1f(shader->src_radius_loc, opts->src_radius);
    glUniform1i(shader->pad_transparent_loc, opts->pad_transparent);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src->id);

    draw_blur_pass_quad(0.f, 0.f, 1.f, 1.f);
}

static void regenerate_blur_cache(const Texture_t *source, const BlurCache_t *cache, const BlurLayout_t *layout) {
    const Texture_t *prepared = ensure_blur_scratch(0, layout->width, layout->height);
    const Texture_t *scratch = ensure_blur_scratch(1, layout->width, layout->height);

    const BlendMode_t saved_blend = g_renderer->blend_mode;
    render_set_blend_mode(BLEND_MODE_NONE);

    const BlurPrepareOpts_t prepare = {
        .scale = layout->scale,
        .origin_x = -layout->pad,
        .origin_y = -layout->pad,
        .dir_x = 1,
        .dir_y = 1,
        .src_radius = source->border_radius < 0.f ? (float)MIN(source->width, source->height) * 0.5f : source->border_radius,
        .pad_transparent = true,
    };
    run_blur_prepare_pass(source, prepared, layout->width, layout->height, &prepare);

    BlurKernel_t kernel;
    build_blur_kernel(layout->sigma / (float)layout->scale, &kernel);

    const BlurPassOpts_t horizontal = {
        .u1 = (float)layout->width / (float)prepared->width,
        .v1 = (float)layout->height / (float)prepared->height,
        .src_used_w = layout->width,
        .src_used_h = layout->height,
        .step_x = 1.f / (float)prepared->width,
    };
    run_blur_pass(prepared, scratch, layout->width, layout->height, &kernel, &horizontal);

    const BlurPassOpts_t vertical = {
        .u1 = (float)layout->width / (float)scratch->width,
        .v1 = (float)layout->height / (float)scratch->height,
        .src_used_w = layout->width,
        .src_used_h = layout->height,
        .step_y = 1.f / (float)scratch->height,
        .unpremultiply = true,
    };
    run_blur_pass(scratch, cache->texture, layout->width, layout->height, &kernel, &vertical);

    glBindTexture(GL_TEXTURE_2D, 0);
    restore_render_target_binding();
    render_set_blend_mode(saved_blend);
}

static void destroy_blur_cache(BlurCache_t *cache) {
    BlurCache_t **link = &g_renderer->blur_engine.caches;
    while ( *link != NULL && *link != cache )
        link = &(*link)->next;
    if ( *link == cache )
        *link = cache->next;

    cache->owner->blur_cache = NULL;
    if ( cache->texture != NULL )
        render_destroy_texture(cache->texture);
    free(cache);
}

static void expire_blur_caches(void) {
    const uint64_t frame = g_renderer->blur_engine.frame;
    BlurCache_t *cache = g_renderer->blur_engine.caches;
    while ( cache != NULL ) {
        BlurCache_t *next = cache->next;
        if ( frame - cache->last_used_frame > BLUR_CACHE_TTL_FRAMES )
            destroy_blur_cache(cache);
        cache = next;
    }
    g_renderer->blur_engine.frame++;
}

static const BlurCache_t *ensure_blur_cache(Texture_t *texture, const float radius) {
    const float quantized = roundf(radius / BLUR_RADIUS_QUANTUM) * BLUR_RADIUS_QUANTUM;
    const BlurLayout_t layout = blur_layout_for(texture->width, texture->height, quantized);

    BlurCache_t *cache = texture->blur_cache;
    if ( cache == NULL ) {
        cache = calloc(1, sizeof(*cache));
        if ( cache == NULL )
            error_abort("Failed to allocate blur cache");
        cache->owner = texture;
        cache->next = g_renderer->blur_engine.caches;
        g_renderer->blur_engine.caches = cache;
        texture->blur_cache = cache;
    }

    const bool layout_changed = cache->texture == NULL || cache->pad != layout.pad || cache->scale != layout.scale ||
                                cache->src_w != texture->width || cache->src_h != texture->height;
    if ( layout_changed ) {
        if ( cache->texture != NULL )
            render_destroy_texture(cache->texture);
        cache->texture = render_make_empty(layout.width, layout.height);
        cache->pad = layout.pad;
        cache->scale = layout.scale;
        cache->src_w = texture->width;
        cache->src_h = texture->height;
        cache->radius = -1.f;
    }
    if ( cache->radius != quantized ) {
        cache->radius = quantized;
        regenerate_blur_cache(texture, cache, &layout);
    }

    cache->last_used_frame = g_renderer->blur_engine.frame;
    return cache;
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

    g_renderer->window = glfwCreateWindow(width, height, "etsuko", NULL, NULL);
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
    g_renderer->shaders.blur.id =
        create_shader_program(buffer, incbin_fullscreen_quad_vert_shader, incbin_blur_frag_shader, "blur");
    g_renderer->shaders.blur_prepare.id =
        create_shader_program(buffer, incbin_fullscreen_quad_vert_shader, incbin_blur_prepare_frag_shader, "blur_prepare");
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

    // Get uniform locations for the separable blur pass
    g_renderer->shaders.blur.tex_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_tex");
    g_renderer->shaders.blur.uv_bounds_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_uvBounds");
    g_renderer->shaders.blur.step_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_step");
    g_renderer->shaders.blur.num_taps_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_numTaps");
    g_renderer->shaders.blur.offsets_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_offsets");
    g_renderer->shaders.blur.weights_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_weights");
    g_renderer->shaders.blur.unpremultiply_loc = glGetUniformLocation(g_renderer->shaders.blur.id, "u_unpremultiply");

    // Get uniform locations for the blur prepare pass
    const BlurPrepareShaderData_t *prepare = &g_renderer->shaders.blur_prepare;
    g_renderer->shaders.blur_prepare.tex_loc = glGetUniformLocation(prepare->id, "u_tex");
    g_renderer->shaders.blur_prepare.scale_loc = glGetUniformLocation(prepare->id, "u_scale");
    g_renderer->shaders.blur_prepare.src_origin_loc = glGetUniformLocation(prepare->id, "u_srcOrigin");
    g_renderer->shaders.blur_prepare.src_dir_loc = glGetUniformLocation(prepare->id, "u_srcDir");
    g_renderer->shaders.blur_prepare.src_radius_loc = glGetUniformLocation(prepare->id, "u_srcRadius");
    g_renderer->shaders.blur_prepare.pad_transparent_loc = glGetUniformLocation(prepare->id, "u_padTransparent");

    glGenFramebuffers(1, &g_renderer->blur_engine.fbo);
    g_renderer->blur_engine.pass_quad = render_make_null();
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
    glDeleteProgram(g_renderer->shaders.blur.id);
    glDeleteProgram(g_renderer->shaders.blur_prepare.id);

    while ( g_renderer->blur_engine.caches != NULL )
        destroy_blur_cache(g_renderer->blur_engine.caches);
    for ( int32_t i = 0; i < BLUR_SCRATCH_COUNT; i++ ) {
        if ( g_renderer->blur_engine.scratch[i] != NULL )
            render_destroy_texture(g_renderer->blur_engine.scratch[i]);
    }
    if ( g_renderer->blur_engine.pass_quad != NULL )
        render_destroy_texture(g_renderer->blur_engine.pass_quad);
    if ( g_renderer->blur_engine.fbo != 0 )
        glDeleteFramebuffers(1, &g_renderer->blur_engine.fbo);

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

    g_renderer->viewport = (Bounds_t){.x = 0, .y = 0, .w = (double)outW, .h = (double)outH};

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

static bool render_target_in_binding_stack(const RenderTarget_t *render_target) {
    for ( const RenderTarget_t *bound = g_renderer->render_target; bound != NULL; bound = bound->previous ) {
        if ( bound == render_target )
            return true;
    }
    return false;
}

void render_target_bind(RenderTarget_t *render_target) {
    if ( render_target->texture == NULL ) {
        error_abort("render_target_bind: Cannot bind a render target with no texture attached");
    }
    if ( render_target_in_binding_stack(render_target) ) {
        error_abort("render_target_bind: Render target is already in the binding stack");
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
    if ( render_target_in_binding_stack(render_target) ) {
        error_abort("render_target_detach_texture: Cannot detach the texture of a render target that is currently bound");
    }

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
        error_abort("render_destroy_render_target: Render target is null");

    if ( render_target_in_binding_stack(render_target) ) {
        error_abort("render_destroy_render_target: Cannot destroy a render target that is currently bound");
    }

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
        if ( background->gradient_target != NULL )
            render_destroy_render_target(background->gradient_target);
        if ( background->low_res_target != NULL )
            render_destroy_render_target(background->low_res_target);
        if ( background->noise_target != NULL )
            render_destroy_render_target(background->noise_target);
        if ( background->backdrop_capture != NULL )
            render_destroy_texture(background->backdrop_capture);
        if ( background->backdrop_result != NULL )
            render_destroy_texture(background->backdrop_result);
        free(background);
    }
}

static void draw_backdrop_blur(Background_t *background, const Bounds_t *bounds, const float border_radius) {
    if ( g_renderer->render_target != NULL )
        return;

    const float sigma = (float)(MIN(bounds->w, bounds->h) * BACKDROP_SIGMA_FRACTION);
    if ( sigma <= 0.f )
        return;

    const int32_t margin = (int32_t)ceilf(BACKDROP_EDGE_SIGMAS * sigma);
    const double vw = g_renderer->viewport.w, vh = g_renderer->viewport.h;
    Bounds_t capture = {.x = MAX(0.0, floor(bounds->x) - margin), .y = MAX(0.0, floor(bounds->y) - margin)};
    capture.w = MIN(vw, ceil(bounds->x + bounds->w) + margin) - capture.x;
    capture.h = MIN(vh, ceil(bounds->y + bounds->h) + margin) - capture.y;
    if ( capture.w <= 0 || capture.h <= 0 )
        return;

    const int32_t cap_w = (int32_t)capture.w, cap_h = (int32_t)capture.h;
    const int32_t low_w = (cap_w + BACKDROP_SCALE - 1) / BACKDROP_SCALE;
    const int32_t low_h = (cap_h + BACKDROP_SCALE - 1) / BACKDROP_SCALE;
    const int32_t out_w = MAX(1, ((int32_t)bounds->w + BACKDROP_SCALE - 1) / BACKDROP_SCALE);
    const int32_t out_h = MAX(1, ((int32_t)bounds->h + BACKDROP_SCALE - 1) / BACKDROP_SCALE);

    bool stale = false;
    if ( background->backdrop_capture == NULL || background->backdrop_capture->width != cap_w ||
         background->backdrop_capture->height != cap_h ) {
        if ( background->backdrop_capture != NULL )
            render_destroy_texture(background->backdrop_capture);
        background->backdrop_capture = render_make_empty(cap_w, cap_h);
        stale = true;
    }
    if ( background->backdrop_result == NULL || background->backdrop_result->width != out_w ||
         background->backdrop_result->height != out_h ) {
        if ( background->backdrop_result != NULL )
            render_destroy_texture(background->backdrop_result);
        background->backdrop_result = render_make_empty(out_w, out_h);
        stale = true;
    }

    const double elapsed = events_get_elapsed_time();
    stale = stale || background->backdrop_bounds.x != bounds->x || background->backdrop_bounds.y != bounds->y ||
            elapsed - background->backdrop_refreshed_at >= BACKDROP_REFRESH_SECONDS ||
            elapsed < background->backdrop_refreshed_at;

    if ( stale ) {
        const BlendMode_t saved_blend = g_renderer->blend_mode;
        render_set_blend_mode(BLEND_MODE_NONE);

        const int32_t fb_y = (int32_t)vh - (int32_t)capture.y - cap_h;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, background->backdrop_capture->id);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (int32_t)capture.x, fb_y, cap_w, cap_h);

        const Texture_t *reduced = ensure_blur_scratch(0, low_w, low_h);
        const BlurPrepareOpts_t prepare = {
            .scale = BACKDROP_SCALE,
            .origin_x = 0,
            .origin_y = cap_h - 1,
            .dir_x = 1,
            .dir_y = -1,
            .pad_transparent = false,
        };
        run_blur_prepare_pass(background->backdrop_capture, reduced, low_w, low_h, &prepare);

        BlurKernel_t kernel;
        build_blur_kernel(sigma / (float)BACKDROP_SCALE, &kernel);

        const Texture_t *scratch = ensure_blur_scratch(1, low_w, low_h);
        const BlurPassOpts_t horizontal = {
            .u1 = (float)low_w / (float)reduced->width,
            .v1 = (float)low_h / (float)reduced->height,
            .src_used_w = low_w,
            .src_used_h = low_h,
            .step_x = 1.f / (float)reduced->width,
        };
        run_blur_pass(reduced, scratch, low_w, low_h, &kernel, &horizontal);

        const float off_u = (float)(bounds->x - capture.x) / BACKDROP_SCALE / (float)scratch->width;
        const float off_v = (float)(bounds->y - capture.y) / BACKDROP_SCALE / (float)scratch->height;
        const BlurPassOpts_t vertical = {
            .u0 = off_u,
            .v0 = off_v,
            .u1 = off_u + (float)out_w / (float)scratch->width,
            .v1 = off_v + (float)out_h / (float)scratch->height,
            .src_used_w = low_w,
            .src_used_h = low_h,
            .step_y = 1.f / (float)scratch->height,
        };
        run_blur_pass(scratch, background->backdrop_result, out_w, out_h, &kernel, &vertical);

        glBindTexture(GL_TEXTURE_2D, 0);
        restore_render_target_binding();
        render_set_blend_mode(saved_blend);

        background->backdrop_bounds = *bounds;
        background->backdrop_refreshed_at = elapsed;
    }

    const BackgroundUpscaleShaderData_t *upscale = &g_renderer->shaders.bg_upscale;
    set_shader_program(upscale->id);
    glUniformMatrix4fv(upscale->projection_loc, 1, GL_FALSE, get_projection_matrix());
    glUniform1i(upscale->use_bounds_loc, 1);
    glUniform4f(upscale->bounds_loc, (float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h);
    glUniform1f(upscale->border_radius_loc, border_radius);
    glUniform2f(upscale->rect_size_loc, (float)bounds->w, (float)bounds->h);
    glUniform1f(upscale->grain_offset_loc, 71.f * (float)(elapsed * 0.37 - floor(elapsed * 0.37)));
    glUniform1i(upscale->texture_loc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, background->backdrop_result->id);

    const Texture_t *quad = g_renderer->blur_engine.pass_quad;
    glBindVertexArray(quad->vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad->vbo);

    float vertices[QUAD_VERTICES_SIZE] = {0};
    create_quad_vertices((float)bounds->x, (float)bounds->y, (float)bounds->w, (float)bounds->h, vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
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
        draw_backdrop_blur(background, bounds, resolve_background_border_radius(background, bounds));
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

void render_present(void) {
    expire_blur_caches();
    glfwSwapBuffers(g_renderer->window);
}

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
    if ( texture->blur_cache != NULL )
        destroy_blur_cache(texture->blur_cache);
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

    Texture_t *draw_tex = texture;
    float quad_x = (float)x, quad_y = (float)y, quad_w = w, quad_h = h;
    float border_radius = texture->border_radius;

    float region_scale_x = 1.f, region_scale_y = 1.f, region_bias_x = 0.f, region_bias_y = 0.f;

    if ( opts->blur_radius > BLUR_MIN_RADIUS && texture->width > 0 && texture->height > 0 ) {
        const BlurCache_t *cache = ensure_blur_cache(texture, opts->blur_radius);
        const float stretch_x = w / (float)texture->width, stretch_y = h / (float)texture->height;
        const float covered_w = (float)(cache->texture->width * cache->scale);
        const float covered_h = (float)(cache->texture->height * cache->scale);

        draw_tex = cache->texture;
        border_radius = 0.f;
        quad_x = (float)x - (float)cache->pad * stretch_x;
        quad_y = (float)y - (float)cache->pad * stretch_y;
        quad_w = covered_w * stretch_x;
        quad_h = covered_h * stretch_y;
        region_scale_x = (float)texture->width / covered_w;
        region_scale_y = (float)texture->height / covered_h;
        region_bias_x = (float)cache->pad / covered_w;
        region_bias_y = (float)cache->pad / covered_h;
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
        regions[i][0] = region->x0_perc * region_scale_x + region_bias_x;
        regions[i][1] = region->y0_perc * region_scale_y + region_bias_y;
        regions[i][2] = region->x1_perc * region_scale_x + region_bias_x;
        regions[i][3] = region->y1_perc * region_scale_y + region_bias_y;

        const float anchor = region->fade_anchor_perc > 0.f ? region->fade_anchor_perc : region->x1_perc;
        const float travelled = (anchor - region->anim_x1_from_perc) * w;
        const float remaining = (region->anim_x1_to_perc - anchor) * w;
        region_fades[i][0] = MAX(0.f, MIN(fade_width, MIN(travelled, remaining)));
        region_fades[i][1] = anchor * region_scale_x + region_bias_x;
    }

    int num_erase_regions = 0;
    if ( opts->scale_regions != NULL ) {
        num_erase_regions = MIN(MAX_SCALE_SUB_REGIONS, opts->scale_regions->num_regions);
    }

    // Erase the portions of the texture that are to be scaled so we redraw them scaled later
    float erase_regions[MAX_SCALE_SUB_REGIONS][4] = {0};
    for ( int i = 0; i < num_erase_regions; i++ ) {
        const ScaleRegionOpt_t *region = &opts->scale_regions->regions[i];
        erase_regions[i][0] = region->x0_perc * region_scale_x + region_bias_x;
        erase_regions[i][1] = region->y0_perc * region_scale_y + region_bias_y;
        erase_regions[i][2] = region->x1_perc * region_scale_x + region_bias_x;
        erase_regions[i][3] = region->y1_perc * region_scale_y + region_bias_y;
    }

    // auto border radius using half of the smaller axis
    if ( border_radius < 0.f ) {
        border_radius = MIN(quad_w, quad_h) * 0.5f;
    }
    glUniform1f(g_renderer->shaders.tex.border_radius_loc, border_radius);
    glUniform1f(g_renderer->shaders.tex.alpha_loc, (float)opts->alpha_mod / 255.0f);
    glUniform2f(g_renderer->shaders.tex.rect_size_loc, quad_w, quad_h);
    glUniform1i(g_renderer->shaders.tex.use_bounds_loc, 1);
    glUniform4f(g_renderer->shaders.tex.bounds_loc, quad_x, quad_y, quad_w, quad_h);
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

    glBindTexture(GL_TEXTURE_2D, draw_tex->id);

    glBindVertexArray(draw_tex->vao);
    glBindBuffer(GL_ARRAY_BUFFER, draw_tex->vbo);

    const Bounds_t final_bounds = {.x = quad_x, .y = quad_y, .w = (int32_t)quad_w, .h = (int32_t)quad_h};
    if ( texture_needs_reconfigure(draw_tex, &final_bounds) ) {
        float vertices[QUAD_VERTICES_SIZE] = {0};
        create_quad_vertices(quad_x, quad_y, quad_w, quad_h, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
        mark_texture_configured(draw_tex, &final_bounds);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

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
    const float blur_radius = (float)offset / 2.f;

    const int32_t pad = blur_radius > BLUR_MIN_RADIUS ? blur_layout_for(0, 0, blur_radius).pad : 0;
    const int32_t width = (int32_t)src_bounds->w + offset + 2 * pad;
    const int32_t height = (int32_t)src_bounds->h + offset + 2 * pad;

    RenderTarget_t *render_target = render_make_render_target(width, height);
    render_target_bind(render_target);

    Bounds_t bounds = {.x = pad + offset, .y = pad + offset, .w = src_bounds->w, .h = src_bounds->h};
    DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 0.f};
    render_draw_texture(texture, &bounds, &opts);
    // Erase the original texture
    const BlendMode_t saved_blend = render_get_blend_mode();
    render_set_blend_mode(BLEND_MODE_ERASE);

    opts.color_mod = 1.f;
    bounds.x = bounds.y = pad;
    render_draw_texture(texture, &bounds, &opts);

    render_set_blend_mode(saved_blend);

    render_target_unbind(render_target);
    Texture_t *result = render_target_detach_texture(render_target);

    if ( blur_radius > BLUR_MIN_RADIUS ) {
        // Recreate texture on the render target
        render_target_ensure_configured(render_target, width, height);
        render_target_bind(render_target);
        render_set_blend_mode(BLEND_MODE_NONE);

        const Bounds_t blur_bounds = {.x = 0, .y = 0, .w = width, .h = height};
        const DrawTextureOpts_t blur_opts = {.alpha_mod = 255, .color_mod = 1.f, .blur_radius = blur_radius};
        render_draw_texture(result, &blur_bounds, &blur_opts);
        render_destroy_texture(result);

        render_set_blend_mode(saved_blend);
        render_target_unbind(render_target);
        result = render_target_detach_texture(render_target);
    }

    render_destroy_render_target(render_target);

    Shadow_t *shadow = calloc(1, sizeof(*shadow));
    shadow->offset = offset;
    shadow->texture = result;
    shadow->bounds = (Bounds_t){.x = -pad, .y = -pad, .w = width, .h = height};

    return shadow;
}
