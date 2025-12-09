uniform float u_time;
uniform float u_noiseScale;
uniform vec2 u_resolution;

out vec4 FragColor;
in vec2 fragCoord;

vec3 hash( vec3 p ) {
    p = vec3( dot(p, vec3(127.1,37.1,23.4)),
    dot(p, vec3(269.5,183.3,46.2)),
    dot(p, vec3(317.3,81.1,99.9)) );
    return fract(sin(p) * 43758.5453);
}

float noise( in vec3 p ) {
    vec3 i = floor( p );
    vec3 f = fract( p );
    vec3 u = f*f*(3.0-2.0*f);

    return mix( mix( mix( dot( hash( i + vec3(0.,0.,0.) ), f - vec3(0.,0.,0.) ),
    dot( hash( i + vec3(1.,0.,0.) ), f - vec3(1.,0.,0.) ), u.x),
    mix( dot( hash( i + vec3(0.,1.,0.) ), f - vec3(0.,1.,0.) ),
    dot( hash( i + vec3(1.,1.,0.) ), f - vec3(1.,1.,0.) ), u.x), u.y),
    mix( mix( dot( hash( i + vec3(0.,0.,1.) ), f - vec3(0.,0.,1.) ),
    dot( hash( i + vec3(1.,0.,1.) ), f - vec3(1.,0.,1.) ), u.x),
    mix( dot( hash( i + vec3(0.,1.,1.) ), f - vec3(0.,1.,1.) ),
    dot( hash( i + vec3(1.,1.,1.) ), f - vec3(1.,1.,1.) ), u.x), u.y), u.z );
}

vec3 hsl2rgb( in vec3 c ) {
    vec3 rgb = clamp( abs( mod( c.x*6.0+vec3(0.0,4.0,2.0), 6.0 ) - 3.0 ) - 1.0, 0.0, 1.0 );
    return c.z + c.y * ( rgb-0.5 ) * ( 1.0 - abs( 2.0 * c.z - 1.0 ) );
}

void main() {
    vec2 pos = fragCoord * 2.5;
    float n = noise( vec3( pos.x, pos.y, u_time * 0.05 ) );
    float noiseValue = n * 0.5 + 0.5;
    float hueRangeStart = 0.05;
    float hueRangeEnd = 0.65;
    float hue = mix(hueRangeStart, hueRangeEnd, noiseValue);
    hue = mod(hue + u_time * 0.02, 1.0);
    float saturation = 0.4;
    float lightness = 0.8;
    vec3 finalColor = hsl2rgb(vec3(hue, saturation, lightness));
    FragColor = vec4( finalColor, 1.0 );
}
