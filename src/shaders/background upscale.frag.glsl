// AI Generated shader below:
// Scales the image blur background back up from the small buffer it was rendered into. The bilinear
// filter on the source does the smoothing; what is left for this pass is the work that only makes sense
// at the output resolution: dithering, which has to land on the pixels that actually get quantized to
// 8 bits, and the rounded corner, which has to be a curve rather than a staircase.
precision highp float;

out vec4 FragColor;
in vec2 TexCoord;
in vec2 FragPos;

uniform sampler2D u_texture;
// Animates the dither so it does not read as a fixed screen door
uniform float u_grainOffset;
uniform float u_borderRadius;
uniform vec2 u_rectSize;

// Dither amplitude in 8-bit steps. 1 to 2 removes banding, higher reads as film grain
const float GRAIN = 1.5;

// Interleaved gradient noise: cheap per-pixel dither that breaks 8-bit banding on slow gradients.
// Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014
float ign(vec2 px) { return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y)); }

void main() {
    vec3 rgb = texture(u_texture, TexCoord).rgb;
    rgb += (ign(gl_FragCoord.xy + u_grainOffset) - 0.5) * (GRAIN / 255.0);

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
