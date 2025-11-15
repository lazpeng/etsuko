in vec2 v_texcoord;
out vec4 frag_color;

uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform float u_blur_size;
uniform bool u_fade_edges;  // Flag to enable edge fading
uniform float u_fade_distance;  // How far from the edge to start fading (0.0-0.5)

void main() {
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 tex_offset = u_blur_size / vec2(textureSize(u_texture, 0));

    // Blur RGBA together, not just RGB
    vec4 result = texture(u_texture, v_texcoord) * weights[0];
    for (int i = 1; i < 5; i++) {
        result += texture(u_texture, v_texcoord + u_direction * tex_offset * float(i)) * weights[i];
        result += texture(u_texture, v_texcoord - u_direction * tex_offset * float(i)) * weights[i];
    }

    // Apply edge fading if enabled
    if (u_fade_edges) {
        // Calculate distance from edges (0.0 at edge, 0.5 at center)
        vec2 edge_dist = min(v_texcoord, 1.0 - v_texcoord);
        float min_edge_dist = min(edge_dist.x, edge_dist.y);

        // Create a smooth falloff
        // smoothstep creates a smooth transition from 0 to 1
        float edge_alpha = smoothstep(0.0, u_fade_distance, min_edge_dist);

        // Multiply the alpha by the edge factor
        result.a *= edge_alpha;
    }

    frag_color = result;
}
