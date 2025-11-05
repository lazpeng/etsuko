in vec2 FragPos;
out vec4 FragColor;
uniform vec4 color;
uniform vec2 rectPos;
uniform vec2 rectSize;
uniform float cornerRadius;

float roundedBoxSDF(vec2 centerPos, vec2 size, float radius) {
    return length(max(abs(centerPos) - size + radius, 0.0)) - radius;
}

void main() {
    vec2 center = rectPos + rectSize * 0.5;
    vec2 pos = FragPos - center;
    float dist = roundedBoxSDF(pos, rectSize * 0.5, cornerRadius);
    float alpha = 1.0 - smoothstep(-1.0, 1.0, dist);
    FragColor = vec4(color.rgb, color.a * alpha);
}