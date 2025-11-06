in vec2 vTexCoord;

out vec4 FragColor;

uniform vec4 topColor;
uniform vec4 bottomColor;

// Simple dithering to reduce color banding
float dither(vec2 coord) {
    return fract(sin(dot(coord, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float t = vTexCoord.y;
    vec4 color = mix(topColor, bottomColor, t);

    float noise = (dither(gl_FragCoord.xy) - 0.5) / 255.0;
    color.rgb += noise;

    FragColor = color;
}
