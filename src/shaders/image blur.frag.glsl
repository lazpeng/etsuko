// AI Generated shader below:
// Image blur background. Draws a small, heavily blurred image (the album art) as three counter-rotating,
// drifting layers, carried by a slow flow field and twisted, with a broad smoke mask deciding where the
// upper layers show through. Colors mix in OKLab and lightness is pulled into a band that keeps white
// text readable.
//
// Two things this shader deliberately does not do. The fields it steers itself with are baked once per
// frame by the noise field shader and arrive here as a texture, instead of being evaluated per pixel.
// And the source image already holds encoded OKLab, put there on the CPU when it was blurred, so the
// only color conversion left is the one back out at the end. Both moves exist because this runs on every
// pixel of the screen, every frame, on hardware that has no headroom to spare.
//
// The output is neither dithered nor rounded here: this is rendered into a smaller buffer and scaled
// back up, and both of those belong at the output resolution. See the background upscale shader.
// The precision statement overrides the mediump default on GLES; desktop GLSL ignores it.
precision highp float;

out vec4 FragColor;
in vec2 FragPos;

uniform vec2 u_resolution;
// Elapsed time, wrapped by the C side to a period that every term below is continuous across
uniform float u_time;
// (flow.x, flow.y, swirl, smoke), each mapped from [-1, 1] to [0, 1]. See the noise field shader
uniform sampler2D u_noise;
uniform sampler2D u_image;
uniform sampler2D u_image_prev;
// 1.0 draws u_image, 0.0 draws u_image_prev, in between crossfades the two
uniform float u_image_fade;

// Tuning
// Global time scale. At 1.0 the base layer turns once every 40 s, the counter layer every 28 s.
// The C side wraps u_time to a period that is exact for every term here at SPEED == 1.0; changing
// this value puts a visible jump back at the wrap unless IMAGE_BLUR_TIME_PERIOD is updated with it
const float SPEED = 1.0;
// How far the image is scaled past the screen. Bigger means broader, softer regions
const float ZOOM = 1.5;
// How far layer centers wander, in short-axis units
const float DRIFT = 0.15;
// Amplitude of the flow field that bulges and carries region boundaries. Most of the visible motion
const float BILLOW = 0.18;
// Position-dependent twist in radians
const float SWIRL = 0.6;
// Strength of the broad density mask between layers. 0 is a plain crossfade of the rotating layers
const float SMOKE = 0.6;
// Number of rotating copies, 1 to 3
const float LAYERS = 3.0;
// OKLab lightness the field is pulled toward, and how hard. 1 flattens every color to one lightness
const float LIGHTNESS = 0.32;
const float PULL = 0.6;
// Multiplier on OKLab a/b after mixing. Above 1 compensates for the lightness pull and the blur
const float CHROMA = 1.4;
// Half-width of the OKLab a/b range the source image is encoded over. Must match the C side
const float AB_RANGE = 0.32;

mat2 Rot(float a) { float s = sin(a); float c = cos(a); return mat2(c, -s, s, c); }

vec3 oklab_to_linear(vec3 c) {
    float l = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    float m = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    float s = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    l = l * l * l; m = m * m * m; s = s * s * s;
    return vec3( 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}
vec3 linear_to_srgb(vec3 c) { return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)); }

// The encoded source at image uv. Outside [0,1] the texture wraps mirrored, so nothing ends at an edge.
// The encoding is affine, so the layers below can be mixed in this space and decoded once at the end
vec3 source(vec2 uv) {
    vec3 c = texture(u_image, uv).rgb;
    if (u_image_fade < 1.0) {
        c = mix(texture(u_image_prev, uv).rgb, c, u_image_fade);
    }
    return c;
}
vec3 decode_oklab(vec3 c) { return vec3(c.r, (c.gb - 0.5) * (2.0 * AB_RANGE)); }

vec2 layer_uv(vec2 pw, float t, float rot, float phase, vec2 driftPhase, float zoom, float breathPhase) {
    vec2 c = DRIFT * vec2(sin(t * 0.13 + driftPhase.x), cos(t * 0.11 + driftPhase.y));
    float s = zoom * (1.0 + 0.06 * sin(t * 0.06 + breathPhase));
    return Rot(t * rot + phase) * (pw - c) / s + 0.5;
}

void main() {
    // centered, aspect-correct: short axis spans [-0.5, 0.5]
    vec2 p = (FragPos - 0.5 * u_resolution) / min(u_resolution.x, u_resolution.y);
    float t = u_time * SPEED;

    vec4 fields = texture(u_noise, FragPos / u_resolution) * 2.0 - 1.0;
    vec2 flow = fields.xy;
    float swirl = SWIRL * fields.z;
    float d = fields.w;
    vec2 pw = Rot(swirl) * p + BILLOW * flow;

    vec3 enc = source(layer_uv(pw, t, 0.157, 0.0, vec2(1.0, 2.0), ZOOM, 0.0));
    if (LAYERS > 1.5) {
        vec3 l1 = source(layer_uv(pw, t, -0.224, 2.1, vec2(4.0, 0.5), ZOOM * 1.15, 2.0));
        float m1 = mix(0.5, smoothstep(-0.7, 0.7, d), SMOKE);
        enc = mix(enc, l1, 0.6 * m1);
    }
    if (LAYERS > 2.5) {
        vec3 l2 = source(layer_uv(pw, t, 0.114, 4.2, vec2(2.5, 3.5), ZOOM * 1.35, 4.0));
        float m2 = mix(0.5, smoothstep(-0.7, 0.7, 0.2 - d), SMOKE);
        enc = mix(enc, l2, 0.5 * m2);
    }

    // vibrancy and a lightness band that keeps white text legible; smoke density shades a little
    vec3 lab = decode_oklab(enc);
    lab.yz *= CHROMA;
    lab.x = mix(lab.x, LIGHTNESS, PULL) + 0.04 * SMOKE * d;

    FragColor = vec4(linear_to_srgb(oklab_to_linear(lab)), 1.0);
}
