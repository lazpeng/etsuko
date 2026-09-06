// AI Generated shader below:
// Bakes the low frequency fields that steer the image blur background into one small texture: a two
// component flow vector, a swirl angle and a smoke density, packed as (flow.x, flow.y, swirl, smoke).
// None of them carries more than a handful of cycles across the screen, so evaluating the fbm once per
// texel here and sampling this bilinearly costs a fraction of evaluating it once per output pixel.
// Each field is remapped from [-1, 1] to [0, 1] for the 8 bit target; the rare tail beyond that range
// is clamped on write, which a flow field does not notice. The precision statements override the
// mediump defaults on GLES; desktop GLSL ignores them. The int one is required here: pcg2d needs
// true 32 bit unsigned integers, and mediump uint is only 16 bits.
precision highp float;
precision highp int;

out vec4 FragColor;
in vec2 FragPos;

// Size of this target, in texels
uniform vec2 u_resolution;
// Screen extent in short axis units, so the field domain matches the one the consumer samples with
uniform vec2 u_extent;
// Elapsed time, unwrapped: every term below is a plain translation of the noise domain, so there is no
// period to wrap to. fp32 keeps the lattice well resolved for far longer than any session lasts
uniform float u_time;

// Global time scale. Must match SPEED in the image blur shader
const float SPEED = 1.0;

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
// Two octaves for the fields that only displace: the third one contributed an eighth of an amplitude
// that is already scaled down by BILLOW and SWIRL at the consumer, which no blurred image shows
float fbm2(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 2; i++) {
        s += a * gnoise(p);
        p = OCT * p + 3.7;
        a *= 0.5;
    }
    return s;
}
// The smoke mask keeps one octave more, since it is read through a smoothstep and its shape is visible
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

void main() {
    // Same domain as the consumer: centered, aspect correct, short axis spanning [-0.5, 0.5]
    vec2 p = (FragPos / u_resolution - 0.5) * u_extent;
    float t = u_time * SPEED;

    // billow: a low-frequency flow field carried through time, so region boundaries bulge and travel
    vec2 flow = vec2(fbm2(p * 1.1 + vec2(0.11 * t, 0.06 * t) + 3.0),
                     fbm2(p * 1.1 + vec2(-0.07 * t, 0.10 * t) + 9.0));
    // swirl: rotation that varies across the screen and in time
    float swirl = fbm2(p * 0.9 + vec2(0.05 * t, -0.04 * t) + 17.0);
    // smoke: broad, slow density field deciding where the upper layers show through
    float smoke = fbm3(p * 1.3 + vec2(0.04 * t, 0.03 * t) + 31.0);

    FragColor = vec4(flow, swirl, smoke) * 0.5 + 0.5;
}
