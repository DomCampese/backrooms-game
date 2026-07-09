#include "shaders.h"

// ---------------------------------------------------------------- shaders
const char *WORLD_VS = R"GLSL(
#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPos; out vec2 fragUV; out vec3 fragN; out vec4 fragC;
void main(){
    fragPos = vertexPosition; fragUV = vertexTexCoord; fragN = vertexNormal; fragC = vertexColor;
    gl_Position = mvp*vec4(vertexPosition,1.0);
}
)GLSL";

const char *WORLD_FS = R"GLSL(
#version 330
in vec3 fragPos; in vec2 fragUV; in vec3 fragN; in vec4 fragC;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime; uniform float uBlackout; uniform vec3 uViewPos;
uniform float uFlash; uniform vec3 uFlashDir;
uniform vec3 uFlarePos; uniform float uFlareInt;
uniform vec3 uAmb; uniform vec3 uFogCol; uniform float uFogDen;
uniform vec3 uLightCol; uniform float uLS; uniform float uLY; uniform float uDead; uniform float uLightMul;
uniform float uGloss;
out vec4 finalColor;
float lhash(vec2 g){ return fract(sin(dot(g, vec2(127.1,311.7)))*43758.5453123); }
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    float a = lhash(i), b = lhash(i+vec2(1,0)), c = lhash(i+vec2(0,1)), d = lhash(i+vec2(1,1));
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
// perturb an axis-aligned face normal with a little procedural surface relief,
// so flat walls/floors catch the room light instead of reading as dead planes
vec3 bumpNormal(vec3 P, vec3 N, float scale, float strength){
    vec3 up = abs(N.y) < 0.9 ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    vec2 uvp = vec2(dot(P,T), dot(P,B)) * scale;
    float e = 0.4;
    float h0 = vnoise(uvp)        + 0.5*vnoise(uvp*2.7);
    float hx = vnoise(uvp+vec2(e,0.0)) + 0.5*vnoise(uvp*2.7+vec2(e,0.0));
    float hy = vnoise(uvp+vec2(0.0,e)) + 0.5*vnoise(uvp*2.7+vec2(0.0,e));
    return normalize(N - (T*(hx-h0) + B*(hy-h0)) * strength);
}
float lightState(vec2 g){
    float h = lhash(g);
    if (h < uDead) return 0.0;                      // dead tube
    float s = 1.0;
    if (h > 0.93){                                  // faulty tube: occasional gentle stutter
        float fh = fract(h*97.31);
        float gate = fract(sin(floor(uTime*0.45+fh*37.0)*12.9898)*43758.5453);
        if (gate > 0.74){
            float n = fract(sin(uTime*(7.0+fh*10.0) + fh*211.0)*43758.5453);
            s = 0.62 + 0.38*step(0.5, n);
        }
    }
    return s * uBlackout;
}
vec3 roomLight(vec3 P, vec3 N){
    vec2 base = floor((P.xz - uLS*0.5)/uLS + 0.5);
    vec3 light = vec3(0.0);
    vec3 V = normalize(uViewPos - P);
    for (int dx=-1; dx<=1; dx++)
    for (int dz=-1; dz<=1; dz++){
        vec2 g = base + vec2(float(dx), float(dz));
        float st = lightState(g);
        if (st <= 0.001) continue;
        vec3 lp = vec3(g.x*uLS + uLS*0.5, uLY, g.y*uLS + uLS*0.5);
        vec3 ld = lp - P;
        float d2 = dot(ld,ld);
        float atten = 1.0/(1.0 + 0.075*d2);
        vec3 Ln = normalize(ld);
        float ndl = clamp(dot(N, Ln)*0.55 + 0.45, 0.0, 1.0);
        light += uLightCol*(st*atten*ndl*2.0*uLightMul);
        if (uGloss > 0.005){                        // glossy sheen: tile shines, concrete barely
            float sp = pow(max(dot(normalize(Ln + V), N), 0.0), 64.0);
            light += uLightCol*(sp*uGloss*st*atten*3.0);
        }
    }
    // handheld flashlight: cone from the camera along the view direction
    if (uFlash > 0.01){
        vec3 fv = P - uViewPos;
        float fd2 = dot(fv,fv);
        vec3 fn = normalize(fv);
        float cone = pow(max(dot(fn, uFlashDir), 0.0), 26.0);
        float sput = 0.975 + 0.025*fract(sin(floor(uTime*24.0)*12.9898)*43758.5453);
        float fl = uFlash * cone * sput * 7.5/(1.0 + 0.10*fd2);
        light += vec3(1.0,0.97,0.86) * fl * clamp(dot(N, -fn)*0.6 + 0.4, 0.0, 1.0);
    }
    if (uFlareInt > 0.01){                            // burning flare: orange point light
        vec3 lv2 = uFlarePos - P;
        float d2 = dot(lv2,lv2);
        float ndl = clamp(dot(N, normalize(lv2))*0.6 + 0.4, 0.0, 1.0);
        light += vec3(1.0,0.42,0.15) * (uFlareInt * ndl * 5.0/(1.0 + 0.30*d2));
    }
    light += uAmb*(0.35+0.65*uBlackout);
    return light;
}
void main(){
    vec3 col;
    float aOut = 1.0;
    if (fragC.a < 0.62){
        if (fragC.a < 0.1){                          // light panel (emissive, flickers)
            vec2 g = floor((fragPos.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(g);
            col = vec3(0.50,0.48,0.44) * roomLight(fragPos, vec3(0.0,-1.0,0.0)) + uLightCol*1.9*st;
            col *= 0.93 + 0.07*sin(fragUV.x*33.0)*sin(fragUV.y*33.0);   // prismatic lens ribs
        } else if (fragC.a < 0.3){                   // raw emissive (exit glow)
            col = fragC.rgb * 2.4;
        } else if (fragC.a < 0.45){                  // window glass: clear in the middle, sheen at grazing angles
            vec3 N = normalize(fragN);
            vec3 V = normalize(uViewPos - fragPos);
            float fres = pow(1.0 - abs(dot(V, N)), 3.0);
            col = fragC.rgb * 3.0 + roomLight(fragPos, N) * 0.15;   // faint cool tint + a little room light
            col += vec3(0.55,0.62,0.72) * fres;                    // edge highlight where light skims the pane
            aOut = 0.10 + fres * 0.55;                             // see-through face, denser at the edges
        } else {                                     // water surface
            vec3 light = roomLight(fragPos, vec3(0.0,1.0,0.0));
            float sh = 0.75 + 0.25*sin(fragPos.x*2.3 + uTime*1.4)*sin(fragPos.z*1.9 - uTime*1.1)
                            + 0.10*sin(fragPos.x*5.1 - uTime*2.2);
            col = fragC.rgb * (light*0.75 + uAmb*1.4) * sh;
            aOut = 0.62;
        }
    } else {
        vec4 texel = texture(texture0, fragUV);
        // relief on matte surfaces (grimy walls, carpet, concrete); glossy tile
        // stays smooth, flat decals / contact-shadows don't bump at all
        float relief = (fragC.a > 0.98 ? 0.5 : 0.0) * clamp(1.0 - uGloss*1.6, 0.0, 1.0);
        vec3 Nb = bumpNormal(fragPos, normalize(fragN), 3.3, relief);
        col = texel.rgb * fragC.rgb * roomLight(fragPos, Nb);
        aOut = fragC.a * texel.a;                    // translucent contact shadows + scrawl decals
    }
    float dist = distance(fragPos, uViewPos);
    float f = clamp(exp(-dist*uFogDen), 0.0, 1.0);
    vec3 fogc = uFogCol * mix(0.10, 1.0, uBlackout);
    col = mix(fogc, col, f);
    col = 1.0 - exp(-col*1.25);                      // filmic-ish rolloff, no clipping
    finalColor = vec4(col, aOut) * colDiffuse;
}
)GLSL";

const char *POST_FS = R"GLSL(
#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime; uniform float uFear;
out vec4 finalColor;
float hh(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
void main(){
    vec2 uv = fragTexCoord;
    vec2 dir = uv - 0.5;
    float ca = 0.0012 + uFear*0.0035;               // chromatic aberration
    vec3 c;
    c.r = texture(texture0, uv + dir*ca).r;
    c.g = texture(texture0, uv).g;
    c.b = texture(texture0, uv - dir*ca).b;
    // two-ring threshold bloom off the fluorescents and bright surfaces, warm-tinted
    vec2 pxs = 1.0/vec2(textureSize(texture0, 0));
    vec3 bl = vec3(0.0);
    for (int i = 0; i < 8; i++){
        float a = float(i)*0.7853982;
        vec2 o = vec2(cos(a), sin(a));
        bl += texture(texture0, uv + o*pxs*3.5).rgb;
        bl += texture(texture0, uv + o*pxs*8.5).rgb * 0.55;
    }
    bl /= 12.4;
    vec3 bloom = max(bl - 0.68, 0.0) * vec3(1.08, 1.02, 0.9);   // only the true highlights, faint warm glow
    c += bloom * 0.7;
    // gentle filmic contrast + a touch of saturation, so it's less flat
    vec3 s = c*c*(3.0 - 2.0*c);
    c = mix(c, s, 0.18);
    float lum0 = dot(c, vec3(0.299,0.587,0.114));
    c = mix(vec3(lum0), c, 1.06);
    // dust motes drifting through the light, brighter where the scene is lit
    vec2 gp = uv*vec2(48.0, 27.0) + vec2(uTime*0.5, uTime*0.22);
    vec2 ci = floor(gp), cf = fract(gp) - vec2(hh(ci+0.13), hh(ci+0.27));
    float mote = smoothstep(0.08, 0.0, length(cf)) * step(0.972, hh(ci));
    c += vec3(0.95,0.92,0.82) * mote * (0.08 + 0.3*lum0) * (0.7 + 0.3*sin(uTime*3.0 + hh(ci)*40.0));
    float g = hh(uv*vec2(1287.0,721.0) + vec2(fract(uTime*13.71)*61.0, fract(uTime*7.31)*83.0)) - 0.5;
    c += g * (0.032 + 0.08*uFear);                   // film grain
    float d = length(dir);
    c *= 1.0 - smoothstep(0.34, 0.95, d)*(0.42 + 0.34*uFear); // vignette
    c *= 0.994 + 0.006*sin(uTime*377.0);             // mains-hum luma shimmer
    finalColor = vec4(c, 1.0);
}
)GLSL";
