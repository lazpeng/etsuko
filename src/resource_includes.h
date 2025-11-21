//
// Created by Rafael Nakano on 16/11/25.
//

#ifndef ETSUKO_RESOURCE_INCLUDES_H
#define ETSUKO_RESOURCE_INCLUDES_H

/*
 * Shaders
 */

#ifdef RESOURCE_INCLUDE_SHADERS

static const char incbin_texture_vert_shader[] = {
#embed "shaders/texture.vert.glsl"
    ,'\0'
};
static const char incbin_texture_frag_shader[] = {
#embed "shaders/texture.frag.glsl"
    ,'\0'
};

static const char incbin_rect_vert_shader[] = {
#embed "shaders/rect.vert.glsl"
    ,'\0'
};
static const char incbin_rect_frag_shader[] = {
#embed "shaders/rect.frag.glsl"
    ,'\0'
};

static const char incbin_copy_vert_shader[] = {
#embed "shaders/copy.vert.glsl"
    ,'\0'
};
static const char incbin_copy_frag_shader[] = {
#embed "shaders/copy.frag.glsl"
    ,'\0'
};

static const char incbin_gradient_vert_shader[] = {
#embed "shaders/gradient.vert.glsl"
    ,'\0'
};
static const char incbin_gradient_frag_shader[] = {
#embed "shaders/gradient.frag.glsl"
    ,'\0'
};

static const char incbin_dyn_gradient_vert_shader[] = {
#embed "shaders/dynamic gradient.vert.glsl"
    ,'\0'
};
static const char incbin_dyn_gradient_frag_shader[] = {
#embed "shaders/dynamic gradient.frag.glsl"
    ,'\0'
};

static const char incbin_blur_vert_shader[] = {
#embed "shaders/blur.vert.glsl"
    ,'\0'
};
static const char incbin_blur_frag_shader[] = {
#embed "shaders/blur.frag.glsl"
    ,'\0'
};

static const char incbin_rand_gradient_vert_shader[] = {
#embed "shaders/random gradient.vert.glsl"
    ,'\0'
};
static const char incbin_rand_gradient_frag_shader[] = {
#embed "shaders/random gradient.frag.glsl"
    ,'\0'
};

static const char incbin_am_gradient_vert_shader[] = {
#embed "shaders/am gradient.vert.glsl"
    ,'\0'
};
static const char incbin_am_gradient_frag_shader[] = {
#embed "shaders/am gradient.frag.glsl"
    ,'\0'
};

static const char incbin_cloud_gradient_frag_shader[] = {
#embed "shaders/cloud gradient.frag.glsl"
    ,'\0'
};

#endif

#ifdef RESOURCE_INCLUDE_IMAGES

/*
 * Images
 */
static const unsigned char incbin_play_img[] = {
#embed "res/play.png"
};
static const unsigned char incbin_pause_img[] = {
#embed "res/pause.png"
};

#endif

#endif // ETSUKO_RESOURCE_INCLUDES_H
