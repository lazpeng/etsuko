// First step of a blur: lift the source into the padded, reduced buffer the blur passes work on.
// Everything that has to happen per source texel happens here, because it is the only pass that
// still sees them one at a time.
//
// Fetches are unfiltered on purpose. Bilinear would average colour and alpha independently, so a
// source whose transparent texels carry no useful colour (a render target's cleared gaps, a cut
// out image) would have dark colour mixed in before this pass could weight it by coverage, and
// nothing downstream can take that back out.
precision highp float;

out vec4 FragColor;

uniform sampler2D u_tex;
// Source texels per destination texel, on each axis
uniform int u_scale;
// Source texel the destination's first texel starts at, and which way each axis runs from there.
// A negative origin is what leaves room around the source for the blur to spread into, and a
// negative direction is for a source that was copied out of the framebuffer, which stores its
// rows the other way up.
uniform ivec2 u_srcOrigin;
uniform ivec2 u_srcDir;
// Corner the source is drawn with, in source pixels. A texture that is only ever shown rounded
// has to be blurred rounded too, or the blur spreads corners that nothing draws.
uniform float u_srcRadius;
// Whether what lies outside the source counts as nothing, which is the case for a texture being
// blurred inside its own padding, or as simply not sampled, which is the case for a region cut
// out of a larger image
uniform bool u_padTransparent;

void main() {
    ivec2 size = textureSize(u_tex, 0);
    ivec2 base = u_srcOrigin + ivec2(gl_FragCoord.xy) * u_scale * u_srcDir;
    vec2 halfSize = vec2(size) * 0.5;

    vec4 sum = vec4(0.0);
    int taken = 0;
    for (int y = 0; y < u_scale; y++) {
        for (int x = 0; x < u_scale; x++) {
            ivec2 p = base + ivec2(x, y) * u_srcDir;
            if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y) {
                continue;
            }
            vec4 c = texelFetch(u_tex, p, 0);
            // Premultiply before anything averages these together, so colour is carried by
            // coverage rather than by texels that are barely there
            c.rgb *= c.a;
            if (u_srcRadius > 0.0) {
                vec2 cornerDist = max(vec2(0.0), abs(vec2(p) + 0.5 - halfSize) - (halfSize - u_srcRadius));
                c *= 1.0 - smoothstep(u_srcRadius - 1.0, u_srcRadius, length(cornerDist));
            }
            sum += c;
            taken++;
        }
    }

    float divisor = u_padTransparent ? float(u_scale * u_scale) : float(max(taken, 1));
    FragColor = sum / divisor;
}
