layout (location = 0) in vec2 aPos;       // Vertex position in pixels (e.g., 0 to width)
layout (location = 1) in vec2 aTexCoords; // Texture coordinate (0 to 1)

uniform mat4 projection;

out vec2 TexCoords;

void main() {
    // Transform pixel coordinates into clip space using the projection matrix
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoords = aTexCoords;
}