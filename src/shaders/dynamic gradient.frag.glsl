out vec4 FragColor;

in vec2 fragCoord;

uniform float u_time;
uniform vec3 u_colors[5];
uniform float u_noise_magnitude;

// 2D Simplex Noise function
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

void main() {
    vec2 uv = fragCoord;

    // Fixed movement direction and speed
    float noise1 = snoise(uv + u_time * 0.1);
    float noise2 = snoise(uv * 2.0 + u_time * 0.2 + noise1);

    // Distortion uses randomized magnitude uniform
    vec2 distorted_uv = uv + vec2(noise1, noise2) * u_noise_magnitude;

    float mix_val = smoothstep(0.2, 0.8, distorted_uv.x + distorted_uv.y * 0.5 + noise2 * 0.3);

    vec3 color = mix(u_colors[0], u_colors[1], mix_val);
    color = mix(color, u_colors[2], smoothstep(0.4, 1.0, distorted_uv.y + noise1 * 0.2));
    color = mix(color, u_colors[3], smoothstep(0.6, 0.9, distorted_uv.x + noise2 * 0.5));
    color = mix(color, u_colors[4], smoothstep(0.8, 1.0, length(distorted_uv - 0.5) + noise1 * 0.1));

    FragColor = vec4(color, 1.0);
}
