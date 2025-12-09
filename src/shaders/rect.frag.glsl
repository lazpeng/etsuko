in vec2 FragPos;
in vec2 Position;
out vec4 FragColor;
uniform vec4 u_color;
uniform vec2 u_rectPos;
uniform vec2 u_rectSize;
uniform float u_cornerRadius;

float roundedBoxSDF(vec2 centerPos, vec2 size, float radius) {
    return length(max(abs(centerPos) - size + radius, 0.0)) - radius;
}

void main() {
    vec2 center = u_rectPos + u_rectSize * 0.5;
    vec2 pos = Position - center;
    float dist = roundedBoxSDF(pos, u_rectSize * 0.5, u_cornerRadius);
    float alpha = 1.0 - smoothstep(-1.0, 1.0, dist);
    FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
