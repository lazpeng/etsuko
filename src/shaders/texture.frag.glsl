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
uniform vec2 u_region_fade[4];
uniform int u_num_erase_regions;
uniform vec4 u_erase_regions[64];

uniform float u_blurRadius;
uniform sampler2D u_fb_tex;
uniform bool u_useFbTex;
uniform vec2 u_fbTexOrigin;
uniform vec2 u_fbTexSize;

void main() {
    float finalAlpha = u_alpha;
    if (u_borderRadius > 0.0) {
        vec2 halfSize = u_rectSize * 0.5;
        vec2 pos = FragPos - halfSize;
        vec2 cornerDist = max(vec2(0.0), abs(pos) - (halfSize - u_borderRadius));
        float dist = length(cornerDist);
        if (dist > u_borderRadius) {
            discard;
        }
        finalAlpha = finalAlpha - smoothstep(u_borderRadius - 1.0, u_borderRadius, dist);
    }
    bool in_region = u_num_regions == 0;
    // x: fade width in pixels, y: ramp anchor in texture coords
    vec2 region_fade = vec2(0.0);

    for (int i = 0; i < u_num_regions; i++) {
        vec2 region_start = u_regions[i].xy;
        vec2 region_end = u_regions[i].zw;

        if (TexCoord.x >= region_start.x
                && TexCoord.x <= region_end.x
                && TexCoord.y >= region_start.y
                && TexCoord.y <= region_end.y) {
            in_region = true;
            region_fade = u_region_fade[i];
            break;
        }
    }

    // Soft reveal edge. The anchor can sit beyond this region's right edge (scaled segment
    // redraws behind the moving edge), so the ramp stays continuous across draw calls. The
    // host zeroes the width once a region is fully revealed, so completed lines stay crisp.
    if (in_region && region_fade.x > 0.0) {
        float fade_tex = region_fade.x / max(u_rectSize.x, 1.0);
        finalAlpha *= 1.0 - smoothstep(region_fade.y - fade_tex, region_fade.y, TexCoord.x);
    }

    for (int i = 0; i < u_num_erase_regions; i++) {
        vec2 region_start = u_erase_regions[i].xy;
        vec2 region_end = u_erase_regions[i].zw;

        if (TexCoord.x >= region_start.x
                && TexCoord.x <= region_end.x
                && TexCoord.y >= region_start.y
                && TexCoord.y <= region_end.y) {
            discard;
        }
    }

    if (!in_region) {
        discard;
    }

    vec4 texColor;
    if (u_blurRadius > 0.0) {
        const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
        vec2 tex_offset = u_blurRadius / vec2(textureSize(u_tex, 0));
        vec4 result = texture(u_tex, TexCoord) * weights[0] * weights[0];
        for (int i = -4; i <= 4; i++) {
            for (int j = -4; j <= 4; j++) {
                if (i == 0 && j == 0) continue;
                result += texture(u_tex, TexCoord + vec2(float(i), float(j)) * tex_offset)
                          * weights[abs(i)] * weights[abs(j)];
            }
        }
        vec4 fb_sample;
        if (u_useFbTex) {
            fb_sample = texture(u_fb_tex, (gl_FragCoord.xy - u_fbTexOrigin) / u_fbTexSize);
        }
        texColor = u_useFbTex ? mix(fb_sample, result, result.a) : result;
    } else {
        texColor = texture(u_tex, TexCoord);
    }

    FragColor = vec4(texColor.rgb * u_colorModFactor, texColor.a * finalAlpha);
}
