in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex;
uniform float alpha;
in vec2 FragPos;
uniform float borderRadius;
uniform vec2 rectSize;
uniform float colorModFactor;
uniform bool fadeEdges;
uniform float fadeDistance;

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
        finalAlpha = finalAlpha - smoothstep(borderRadius - 1.0, borderRadius, dist);
    }
    if (fadeEdges) {
        // Calculate distance from edges (0.0 at edge, 0.5 at center)
        vec2 edge_dist = min(TexCoord, 1.0 - TexCoord);
        float min_edge_dist = min(edge_dist.x, edge_dist.y);

        // Create a smooth falloff
        // smoothstep creates a smooth transition from 0 to 1
        float edge_alpha = smoothstep(0.0, fadeDistance, min_edge_dist);

        // Multiply the alpha by the edge factor
        finalAlpha *= edge_alpha;
    }
    FragColor = vec4(texColor.rgb * colorModFactor, texColor.a * finalAlpha);
}