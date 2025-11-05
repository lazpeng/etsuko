in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex;
uniform float alpha;
in vec2 FragPos;
uniform float borderRadius;
uniform vec2 rectSize;// width, height

void main() {
    vec4 texColor = texture(tex, TexCoord);
    float finalAlpha = alpha;
    if (borderRadius > 0.0) {
        // Distance from edges
        vec2 halfSize = rectSize * 0.5;
        vec2 pos = FragPos - halfSize;
        // Distance to nearest corner (only outside the inner rectangle)
        vec2 cornerDist = max(vec2(0.0), abs(pos) - (halfSize - borderRadius));
        float dist = length(cornerDist);
        if (dist > borderRadius) {
            discard;
        }
        // Optional: smooth edges with anti-aliasing
        finalAlpha = 1.0 - smoothstep(borderRadius - 1.0, borderRadius, dist);
    }
    FragColor = vec4(texColor.rgb, texColor.a * finalAlpha);
}