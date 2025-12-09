layout (location = 0) in vec2 position;
layout (location = 1) in vec2 texCoord;
out vec2 TexCoord;
out vec2 FragPos;
out vec2 Position;

uniform mat4 u_projection;
uniform vec4 u_bounds;
uniform bool u_use_bounds;

void main() {
    gl_Position = u_projection * vec4(position, 0.0, 1.0);
    TexCoord = texCoord;
    Position = position;
    if (u_use_bounds) {
        FragPos = texCoord * u_bounds.zw;
    } else {
        FragPos = texCoord;
    }
}
