// One axis of a separable gaussian blur, run twice: horizontally into a scratch buffer, then
// vertically back out. Both passes work on the reduced buffer the prepare pass produced, which is
// already premultiplied, so all this has to do is weight and sum.
// Taps are bilinear pairs, so a kernel covering 2n+1 texels costs n+1 fetches.
precision highp float;

in vec2 fragCoord;
out vec4 FragColor;

uniform sampler2D u_tex;
// Centres of the first and last texel the pass before this one actually wrote. The buffers are
// grown to fit the largest blur seen and never shrunk, so a smaller blur leaves the previous
// one's contents sitting just past its own edge, and the ramp would otherwise reach into them.
// Clamping here also gives the edge the behaviour each caller wants: what lies outside a padded
// texture's buffer is empty, so repeating it repeats nothing, and what lies outside a captured
// region is more of the same image, so repeating it is the right guess.
uniform vec4 u_uvBounds;
// One tap unit along the axis of this pass, in u_tex coordinates. Zero on the other axis.
uniform vec2 u_step;
uniform int u_numTaps;
// In tap units. Every offset but the first lands between two texels so one fetch picks up both.
uniform float u_offsets[16];
uniform float u_weights[16];
// Set on the last pass, to hand back a texture the ordinary drawing path can use
uniform bool u_unpremultiply;

vec4 sample_at(vec2 uv) { return texture(u_tex, clamp(uv, u_uvBounds.xy, u_uvBounds.zw)); }

void main() {
    vec4 sum = sample_at(fragCoord) * u_weights[0];
    for (int i = 1; i < u_numTaps; i++) {
        vec2 d = u_step * u_offsets[i];
        sum += (sample_at(fragCoord + d) + sample_at(fragCoord - d)) * u_weights[i];
    }

    if (u_unpremultiply) {
        sum.rgb = sum.a > 0.0 ? sum.rgb / sum.a : vec3(0.0);
    }
    FragColor = sum;
}
