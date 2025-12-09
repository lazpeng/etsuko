in vec2 TexCoord;
in vec2 FragPos;
out vec4 FragColor;

uniform sampler2D u_tex;
uniform float u_alpha;
uniform float u_borderRadius;
uniform vec2 u_rectSize;
uniform float u_colorModFactor;
uniform int u_num_regions;
uniform vec4 u_regions[4];

void main() {
    vec4 texColor = texture(u_tex, TexCoord);
    float finalAlpha = u_alpha;
    if (u_borderRadius > 0.0) {
        // Distance from edges
        vec2 halfSize = u_rectSize * 0.5;
        vec2 pos = FragPos - halfSize;
        // Distance to nearest corner (only outside the inner rectangle)
        vec2 cornerDist = max(vec2(0.0), abs(pos) - (halfSize - u_borderRadius));
        float dist = length(cornerDist);
        if (dist > u_borderRadius) {
            discard;
        }
        finalAlpha = finalAlpha - smoothstep(u_borderRadius - 1.0, u_borderRadius, dist);
    }
    bool in_region = u_num_regions == 0;

    for (int i = 0; i < u_num_regions; i++) {
        vec2 region_start = u_regions[i].xy;
        vec2 region_end = u_regions[i].xy + u_regions[i].zw;

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

    FragColor = vec4(texColor.rgb * u_colorModFactor, texColor.a * finalAlpha);
}
