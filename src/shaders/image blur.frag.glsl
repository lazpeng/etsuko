// AI Generated shader below:
// Image blur background. Draws a small, heavily blurred image (the album art) as three counter-rotating, drifting
// layers, carried by a slow flow field and twisted, with a broad smoke mask deciding where the upper layers show
// through. Colors mix in OKLab, lightness is pulled into a band that keeps white text readable, and the output is
// dithered. The precision statement overrides the mediump default on GLES; desktop GLSL ignores it.
precision highp float;

out vec4 FragColor;
in vec2 FragPos;

uniform vec2 u_resolution;
uniform float u_time;
uniform sampler2D u_image;
uniform sampler2D u_image_prev;
// 1.0 draws u_image, 0.0 draws u_image_prev, in between crossfades the two
uniform float u_image_fade;
uniform float u_borderRadius;
uniform vec2 u_rectSize;

// Tuning
// Global time scale. At 1.0 the base layer turns once every 40 s, the counter layer every 28 s
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
// Dither amplitude in 8-bit steps. 1 to 2 removes banding, higher reads as film grain
const float GRAIN = 1.5;

mat2 Rot(float a) { float s = sin(a); float c = cos(a); return mat2(c, -s, s, c); }

// Integer hash: stable at any coordinate magnitude, no sin(), no precision cliff as u_time grows.
// pcg2d from "Hash Functions for GPU Rendering", Mark Jarzynski and Marc Olano, Journal of Computer Graphics
// Techniques vol. 9 no. 3, 2020
uvec2 pcg2d(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u; v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u; v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return v;
}
vec2 grad(vec2 cell) {
    uvec2 h = pcg2d(uvec2(ivec2(cell) + ivec2(65536)));
    return vec2(h) * (2.0 / 4294967295.0) - 1.0;
}
float gnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(grad(i), f);
    float b = dot(grad(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(grad(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(grad(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 1.4;
}
// Rotate and scale between octaves so nothing lines up with the screen axes
const mat2 OCT = mat2(1.6, 1.2, -1.2, 1.6);
float fbm3(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++) {
        s += a * gnoise(p);
        p = OCT * p + 3.7;
        a *= 0.5;
    }
    return s;
}
float fbm4(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * gnoise(p);
        p = OCT * p + 3.7;
        a *= 0.5;
    }
    return s;
}

vec3 srgb_to_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 linear_to_srgb(vec3 c) { return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)); }
// OKLab conversions by Bjorn Ottosson, https://bottosson.github.io/posts/oklab/ (public domain, MIT as an alternative)
vec3 linear_to_oklab(vec3 c) {
    float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
    l = pow(l, 1.0 / 3.0); m = pow(m, 1.0 / 3.0); s = pow(s, 1.0 / 3.0);
    return vec3(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
}
vec3 oklab_to_linear(vec3 c) {
    float l = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    float m = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    float s = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    l = l * l * l; m = m * m * m; s = s * s * s;
    return vec3( 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}
// Interleaved gradient noise: cheap per-pixel dither that breaks 8-bit banding on slow gradients.
// Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
float ign(vec2 px) { return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y)); }

// The image in OKLab at image uv. Outside [0,1] the texture wraps mirrored, so nothing ends at an edge
vec3 source(vec2 uv) {
    vec3 c = texture(u_image, uv).rgb;
    if (u_image_fade < 1.0) {
        c = mix(texture(u_image_prev, uv).rgb, c, u_image_fade);
    }
    return linear_to_oklab(srgb_to_linear(c));
}

vec2 layer_uv(vec2 pw, float t, float rot, float phase, vec2 driftPhase, float zoom, float breathPhase) {
    vec2 c = DRIFT * vec2(sin(t * 0.13 + driftPhase.x), cos(t * 0.11 + driftPhase.y));
    float s = zoom * (1.0 + 0.06 * sin(t * 0.06 + breathPhase));
    return Rot(t * rot + phase) * (pw - c) / s + 0.5;
}

void main() {
    // centered, aspect-correct: short axis spans [-0.5, 0.5]
    vec2 p = (FragPos - 0.5 * u_resolution) / min(u_resolution.x, u_resolution.y);
    float t = u_time * SPEED;

    // billow: a low-frequency flow field carried through time, so region boundaries bulge and travel
    vec2 flow = vec2(fbm3(p * 1.1 + vec2(0.11 * t, 0.06 * t) + 3.0),
                     fbm3(p * 1.1 + vec2(-0.07 * t, 0.10 * t) + 9.0));
    // swirl: rotation that varies across the screen and in time
    float swirl = SWIRL * fbm3(p * 0.9 + vec2(0.05 * t, -0.04 * t) + 17.0);
    vec2 pw = Rot(swirl) * p + BILLOW * flow;

    // smoke: broad, slow density field deciding where the upper layers show through
    float d = fbm4(p * 1.3 + vec2(0.04 * t, 0.03 * t) + 31.0);

    vec3 lab = source(layer_uv(pw, t, 0.157, 0.0, vec2(1.0, 2.0), ZOOM, 0.0));
    if (LAYERS > 1.5) {
        vec3 l1 = source(layer_uv(pw, t, -0.224, 2.1, vec2(4.0, 0.5), ZOOM * 1.15, 2.0));
        float m1 = mix(0.5, smoothstep(-0.7, 0.7, d), SMOKE);
        lab = mix(lab, l1, 0.6 * m1);
    }
    if (LAYERS > 2.5) {
        vec3 l2 = source(layer_uv(pw, t, 0.114, 4.2, vec2(2.5, 3.5), ZOOM * 1.35, 4.0));
        float m2 = mix(0.5, smoothstep(-0.7, 0.7, 0.2 - d), SMOKE);
        lab = mix(lab, l2, 0.5 * m2);
    }

    // vibrancy and a lightness band that keeps white text legible; smoke density shades a little
    lab.yz *= CHROMA;
    lab.x = mix(lab.x, LIGHTNESS, PULL) + 0.04 * SMOKE * d;

    vec3 rgb = linear_to_srgb(oklab_to_linear(lab));
    rgb += (ign(gl_FragCoord.xy + 71.0 * fract(t * 0.37)) - 0.5) * (GRAIN / 255.0);

    float alpha = 1.0;
    if (u_borderRadius > 0.0) {
        vec2 halfSize = u_rectSize * 0.5;
        vec2 localPos = FragPos - halfSize;
        vec2 cornerDist = max(vec2(0.0), abs(localPos) - (halfSize - u_borderRadius));
        float dist = length(cornerDist);
        if (dist > u_borderRadius) {
            discard;
        }
        alpha -= smoothstep(u_borderRadius - 1.0, u_borderRadius, dist);
    }

    FragColor = vec4(rgb, alpha);
}
