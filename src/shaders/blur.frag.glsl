in vec2 v_texcoord;
out vec4 frag_color;

uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform float u_blur_size;

void main() {
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 tex_offset = u_blur_size / vec2(textureSize(u_texture, 0));
    vec3 result = texture(u_texture, v_texcoord).rgb * weights[0];
    for (int i = 1; i < 5; i++) {
        result += texture(u_texture, v_texcoord + u_direction * tex_offset * float(i)).rgb * weights[i];
        result += texture(u_texture, v_texcoord - u_direction * tex_offset * float(i)).rgb * weights[i];
    }

    frag_color = vec4(result, texture(u_texture, v_texcoord).a);
}
