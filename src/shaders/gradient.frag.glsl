in vec2 TexCoord;

out vec4 FragColor;

uniform vec4 u_topColor;
uniform vec4 u_bottomColor;

// Simple dithering to reduce color banding
float dither(vec2 coord) {
    return fract(sin(dot(coord, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float t = TexCoord.y;
    vec4 color = vec4(mix(u_topColor.rgb, u_bottomColor.rgb, t), 1.0);

    float noise = (dither(gl_FragCoord.xy) - 0.5) / 255.0;
    color.rgb += noise;

    FragColor = color;
}
