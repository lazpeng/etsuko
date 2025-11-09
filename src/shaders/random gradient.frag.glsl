uniform float uTime;
uniform float uNoiseScale; // Mantemos para ajuste fino, mas o modificaremos no JS
uniform vec2 uResolution;

out vec4 FragColor;
in vec2 vUv;

// Funções de Ruído e HSL para RGB (inalteradas, pois são utilitárias)
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
    // Coordenadas para o ruído (usaremos um valor fixo para uNoiseScale aqui para consistência)
    // Ou você pode ajustar a variável uNoiseScale no JS.
    // Para um efeito mais suave e com "bolhas" maiores, podemos diminuir a escala:
    vec2 pos = vUv * 2.5; // Diminui a escala para formas maiores e mais suaves

    // Reduzindo a velocidade do tempo para transições mais lentas e suaves
    float n = noise( vec3( pos.x, pos.y, uTime * 0.05 ) ); // Tempo mais lento

    // Mapeia o valor do ruído para 0-1
    float noiseValue = n * 0.5 + 0.5;

    // --- Gerando a cor pastel ---

    // 1. Matiz (Hue): Mapeia o ruído para uma gama específica de matizes (amarelo, rosa, azul, verde pastel)
    // Os valores são em torno de:
    // Amarelo: ~0.15
    // Rosa: ~0.85 - 0.95
    // Azul: ~0.5 - 0.65
    // Verde: ~0.25 - 0.4
    // Vamos fazer um loop mais controlado usando `mix` para transições mais suaves entre esses
    // Você pode ajustar esses números para "deslocar" as cores predominantes.
    float hueRangeStart = 0.05; // Começa perto do amarelo-esverdeado
    float hueRangeEnd = 0.65;   // Termina no azul-roxo
    float hue = mix(hueRangeStart, hueRangeEnd, noiseValue); // Interpola linearmente
    hue = mod(hue + uTime * 0.02, 1.0); // Adiciona um movimento lento do hue ao longo do tempo

    // 2. Saturação (Saturation): Baixa para tons pastéis
    float saturation = 0.4; // Valor mais baixo para tons pastéis

    // 3. Luminosidade (Lightness): Mais alta para tons pastéis claros
    float lightness = 0.8; // Valor mais alto para cores claras

    // Converte HSL para RGB
    vec3 finalColor = hsl2rgb(vec3(hue, saturation, lightness));

    // Define a cor final do pixel
    FragColor = vec4( finalColor, 1.0 );
}
