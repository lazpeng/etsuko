in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex;
uniform float alpha;
in vec2 FragPos;
uniform float borderRadius;
uniform vec2 rectSize;
uniform float colorModFactor;
uniform int num_regions;
uniform vec4 regions[4];

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
    bool in_region = num_regions == 0;

    for (int i = 0; i < num_regions; i++) {
        vec2 region_start = regions[i].xy;
        vec2 region_end = regions[i].xy + regions[i].zw;

        if (TexCoord.x >= region_start.x
                && TexCoord.x <= region_end.x
                && TexCoord.y >= region_start.y
                && TexCoord.y <= region_end.y) {
            in_region = true;
            break;
        }
    }

    if (!in_region) {
        discard;
    }

    FragColor = vec4(texColor.rgb * colorModFactor, texColor.a * finalAlpha);
}