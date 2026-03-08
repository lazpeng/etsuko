out vec4 fragColor;
in vec2 FragPos;

uniform vec3 iResolution;
uniform float iTime;
uniform vec4 iColors[5];
uniform float u_borderRadius;
uniform vec2 u_rectSize;

#define S(a, b, t) smoothstep(a, b, t)

mat2 Rot(float a)
{
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}


// Created by inigo quilez - iq/2014
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
vec2 hash(vec2 p)
{
    p = vec2(dot(p, vec2(2127.1, 81.17)), dot(p, vec2(1269.5, 283.37)));
    return fract(sin(p) * 43758.5453);
}

float noise(in vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    vec2 u = f * f * (3.0 - 2.0 * f);

    float n = mix(mix(dot(-1.0 + 2.0 * hash(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                      dot(-1.0 + 2.0 * hash(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
                  mix(dot(-1.0 + 2.0 * hash(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                      dot(-1.0 + 2.0 * hash(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
    return 0.5 + 0.5 * n;
}


void main()
{
    vec2 uv = FragPos / iResolution.xy;
    float ratio = iResolution.x / iResolution.y;

    vec2 tuv = uv;
    tuv -= .5;

    // rotate with Noise
    float degree = noise(vec2(iTime * .1, tuv.x * tuv.y));

    tuv.y *= 1. / ratio;
    tuv *= Rot(radians((degree - .5) * 720. + 180.));
    tuv.y *= ratio;


    // Wave warp with sin
    float frequency = 5.;
    float amplitude = 30.;
    float speed = iTime * 2.;
    tuv.x += sin(tuv.y * frequency + speed) / amplitude;
    tuv.y += sin(tuv.x * frequency * 1.5 + speed) / (amplitude * .5);


    // draw the image
    //vec3 colorYellow = vec3(.957, .804, .623);
    //vec3 colorDeepBlue = vec3(.192, .384, .933);
    vec4 layer1 = mix(iColors[0], iColors[1], S(- .3, .2, (tuv * Rot(radians(- 5.))).x));

    //vec3 colorRed = vec3(.910, .510, .8);
    //vec3 colorBlue = vec3(0.350, .71, .953);
    vec4 layer2 = mix(iColors[2], iColors[3], S(- .3, .2, (tuv * Rot(radians(- 5.))).x));

    vec4 finalComp = mix(layer1, layer2, S(.5, - .3, tuv.y));

    vec4 col = finalComp;

    float alpha = col.a;
    if (u_borderRadius > 0.0) {
        vec2 halfSize = u_rectSize * 0.5;
        vec2 localPos = FragPos - halfSize;
        vec2 cornerDist = max(vec2(0.0), abs(localPos) - (halfSize - u_borderRadius));
        float dist = length(cornerDist);
        if (dist > u_borderRadius) {
            discard;
        }
        alpha -= smoothstep(u_borderRadius - 1.0, u_borderRadius, dist);
    }

    fragColor = vec4(col.rgb * 0.5, alpha);
}
