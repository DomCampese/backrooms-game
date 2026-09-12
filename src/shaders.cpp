#include "shaders.h"

// ---------------------------------------------------------------- shaders
const char *WORLD_VS = R"GLSL(
#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec3 fragPos; out vec2 fragUV; out vec3 fragN; out vec4 fragC;
void main(){
    fragPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fragUV = vertexTexCoord; fragN = (matNormal * vec4(vertexNormal, 0.0)).xyz; fragC = vertexColor;
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
uniform vec3 uEntPos; uniform float uEntDark;      // the hunter kills the lights around it
uniform vec3 uAmb; uniform vec3 uFogCol; uniform float uFogDen;
uniform vec3 uLightCol; uniform float uLS; uniform float uLY; uniform float uDead; uniform float uLightMul;
uniform float uGloss;
uniform sampler2D texture1; // packed material slopes / gloss mask
float gGloss;
uniform sampler2D texture2;      // occupancy grid (material normal-map slot)
uniform vec2 uOccOrigin;         // world cell coords of texel (0,0)
uniform float uOccN;             // grid side in cells; 0 = no grid, everything lit
uniform float uEntBlock;         // 1 while the thing is out — it occludes light too
out vec4 finalColor;

// Half-width of one ceiling diffuser. MUST match `hp` in world.cpp's panel
// mesher: the lighting treats the panel as the rectangle it actually draws, so
// if the quad changes size and this doesn't, the light stops matching the fitting
// it is supposed to be coming out of.
const float PANEL_HALF = 0.62;

// How lit the shaded point came out. roomLight() fills this in so the fog can be
// lit by the same room the surface is — set once per fragment, read at the end.
float gLightLum = 0.0;

float lhash(vec2 g){ return fract(sin(dot(g, vec2(127.1,311.7)))*43758.5453123); }
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    float a = lhash(i), b = lhash(i+vec2(1,0)), c = lhash(i+vec2(0,1)), d = lhash(i+vec2(1,1));
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
// Reconstruct a tangent frame from the actual UV mapping. This supports both
// wall orientations, mirrored UVs and steps without adding mesh tangents.
// Derivatives are evaluated before material branches (GLSL quad coherence).
vec3 detailNormal(vec3 N, vec2 slope, vec3 dpdx, vec3 dpdy, vec2 duvdx, vec2 duvdy){
    vec3 q1 = cross(dpdy, N), q2 = cross(N, dpdx);
    vec3 T = q1 * duvdx.x + q2 * duvdy.x;
    vec3 B = q1 * duvdx.y + q2 * duvdy.y;
    float inv = inversesqrt(max(max(dot(T,T), dot(B,B)), 1e-12));
    return normalize(N - (T * slope.x + B * slope.y) * inv);
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
int occAt(ivec2 c){
    ivec2 t = c - ivec2(uOccOrigin);
    int n = int(uOccN);
    if (t.x < 0 || t.y < 0 || t.x >= n || t.y >= n) return 0;   // off-grid: assume open
    return int(texelFetch(texture2, t, 0).r * 255.0 + 0.5);
}
// Does light from `a` reach `b`? Walls are a floorplan extruded to full height,
// so this is a 2D grid march: step cell to cell and test the edge we cross.
// Doorways, window glass and open gaps aren't in the grid, so light pours
// through them — which is where the shafts across dark corridors come from.
float lightVis(vec2 a, vec2 b){
    if (uOccN < 1.0) return 1.0;
    vec2 d = b - a;
    float dist = length(d);
    if (dist < 0.05) return 1.0;
    vec2 dir = d / dist;
    ivec2 c = ivec2(floor(a / 2.0)), ec = ivec2(floor(b / 2.0));
    if (c == ec) return 1.0;
    ivec2 stp = ivec2(sign(dir.x), sign(dir.y));
    vec2 inv = 1.0 / max(abs(dir), vec2(1e-6));
    vec2 tDelta = 2.0 * inv;
    vec2 tMax = abs((vec2(c) + max(vec2(stp), 0.0)) * 2.0 - a) * inv;
    if (stp.x == 0) tMax.x = 1e9;
    if (stp.y == 0) tMax.y = 1e9;
    for (int i = 0; i < 8; i++){   // each step crosses one 2 m cell, so this reaches 16 m
        if (tMax.x < tMax.y){
            if (tMax.x > dist) return 1.0;
            c.x += stp.x;
            // crossing between cells shares one west edge: the right-hand cell owns it
            if ((occAt(ivec2(stp.x > 0 ? c.x : c.x + 1, c.y)) & 2) != 0) return 0.0;
            tMax.x += tDelta.x;
        } else {
            if (tMax.y > dist) return 1.0;
            c.y += stp.y;
            if ((occAt(ivec2(c.x, stp.y > 0 ? c.y : c.y + 1)) & 1) != 0) return 0.0;
            tMax.y += tDelta.y;
        }
        if (c == ec) return 1.0;
        if ((occAt(c) & 4) != 0) return 0.0;   // a pillar fills its whole cell
    }
    return 1.0;
}
// the hunter is solid too: catch it in a beam and it throws a shadow down the
// hall. It's a body, not a column — a ray that passes over its head still gets
// through, so the beam clears it onto the ceiling behind.
float entVis(vec3 a, vec3 b){
    if (uEntBlock < 0.5) return 1.0;
    vec2 d = b.xz - a.xz;
    float L = length(d);
    if (L < 1e-4) return 1.0;
    vec2 n = d / L;
    float t = clamp(dot(uEntPos.xz - a.xz, n), 0.0, L);
    float side = smoothstep(0.26, 0.52, distance(a.xz + n * t, uEntPos.xz));
    // uEntPos.y is chest height; the head is a bit under a metre above that
    float rayY = mix(a.y, b.y, t / L);
    float over = smoothstep(0.0, 0.45, rayY - (uEntPos.y + 0.95));
    return max(side, over);
}
// One normalized Blinn lobe with a Schlick edge term. Every surface down here is
// a dielectric, so the sheen is weak head-on and strong at a grazing angle —
// that Fresnel ramp is what makes wet tile read as ceramic instead of as paper,
// and it is the entire reason the poolrooms have a floor you can see the lights in.
float sheen(vec3 N, vec3 V, vec3 L, float shin){
    float nh = max(dot(N, normalize(L + V)), 0.0);
    float u = 1.0 - max(dot(V, N), 0.0), u2 = u*u;
    float f = 0.045 + 0.955*(u2*u2*u);                      // Schlick, by multiplies not pow()
    return pow(nh, shin) * (shin + 8.0) * 0.03978874 * f;   // (n+8)/8pi normalization
}
vec3 roomLight(vec3 P, vec3 N){
    vec2 base = floor((P.xz - uLS*0.5)/uLS + 0.5);
    vec3 light = vec3(0.0);
    vec3 V = normalize(uViewPos - P);
    float shin = mix(20.0, 210.0, gGloss);
    // reflection ray, for picking the point on a panel this surface can actually
    // see a highlight from — see the representative-point note below
    vec3 R = reflect(-V, N);
    // march from just off the surface, so a wall face isn't shadowed by its own wall
    vec2 shP = P.xz + N.xz * 0.16;
    for (int dx=-1; dx<=1; dx++)
    for (int dz=-1; dz<=1; dz++){
        vec2 g = base + vec2(float(dx), float(dz));
        float st = lightState(g);
        if (st <= 0.001) continue;
        vec3 lc = vec3(g.x*uLS + uLS*0.5, uLY, g.y*uLS + uLS*0.5);   // panel centre
        if (uEntDark > 0.01){                        // fluorescents die in a pool around the hunter
            float ed = distance(lc.xz, uEntPos.xz);
            st *= mix(1.0, smoothstep(2.0, 9.0, ed), uEntDark);
        }
        // A ceiling diffuser is a 1.24 m square of glowing plastic, not a point.
        // Treating it as a point put a hard little hotspot under every fitting and
        // made the falloff far too even everywhere else. Shade instead from the
        // point on the panel *closest to the surface* — the standard representative
        // point — which costs two clamps and gives the near field the soft, broad
        // pour of light a real fluorescent tray actually throws.
        vec3 lp = vec3(clamp(P.x, lc.x - PANEL_HALF, lc.x + PANEL_HALF), uLY,
                       clamp(P.z, lc.z - PANEL_HALF, lc.z + PANEL_HALF));
        vec3 ld = lp - P;
        float d2 = dot(ld,ld);
        float atten = 1.0/(1.0 + 0.22*d2);
        // Only the 3x3 panels around this point are summed, and that window is a
        // hard cut: at the edge of it a light is still worth ~8% of full, then
        // vanishes the instant P crosses into the next cell. That draws a
        // straight seam along every light-cell boundary — the lines on the
        // floor. Fade each panel out before it leaves the set so nothing pops.
        vec2 wf = 1.0 - smoothstep(vec2(0.80), vec2(1.0), abs(lc.xz - P.xz)/(1.5*uLS));
        atten *= wf.x * wf.y;
        if (atten < 1e-5) continue;
        if (st*atten < 0.004) continue;              // too faint to be worth tracing
        // Two taps across the panel up close for a little penumbra, one beyond.
        // Tracing across it at every range looked better and cost about three
        // times as much, which was too much.
        // The march gives up after a fixed number of cells, and where it gives up
        // the shadow simply stops — and because the DDA spends one step per cell
        // crossed, the contour where it runs out is |dx|+|dz| = const: a diamond,
        // whose sides are straight diagonals across open floor. That is where the
        // lines on the ground came from. Fade each light's shadow out well inside
        // the budget so the march never truncates in view.
        float l1 = abs(lc.x - shP.x) + abs(lc.z - shP.y);   // shP is a vec2: .y is world z
        // Gone by 15 m, which is 7.5 cells — inside the 8-cell march, so it can
        // never truncate mid-view. It also means most of the nine lights skip
        // tracing altogether, which is where the time comes back.
        float sw = 1.0 - smoothstep(10.0, 15.0, l1);
        float vis;
        if (sw < 0.002) {
            vis = 1.0;                                   // too far to shadow; it's faint anyway
        } else if (d2 < 36.0) {
            vec2 toFrag = shP - lc.xz;
            float toLen = length(toFrag);
            // directly overhead there's no meaningful direction to spread along
            vec2 perp = (toLen > 0.001) ? vec2(-toFrag.y, toFrag.x) / toLen * 0.45 : vec2(0.45, 0.0);
            vis = 0.5*(lightVis(lc.xz + perp, shP) + lightVis(lc.xz - perp, shP));
        } else vis = lightVis(lc.xz, shP);
        vis = mix(1.0, vis, sw);                         // ease the shadow off with range
        // a wall kills the direct beam, never the light that bounces around it
        st *= mix(0.18, 1.0, vis);
        if (st <= 0.001) continue;
        vec3 Ln = ld * inversesqrt(max(d2, 1e-6));
        // Soften the terminator by how big the panel looks from here, not by a
        // fixed amount. The old 0.55/0.45 half-lambert handed 45% of full
        // brightness to every surface edge-on to the light, which is most of why
        // the place read as one flat wash — but replacing it with a hard lambert
        // put a different fault in: the ceiling sits *level with* the fittings, so
        // its light arrives edge-on, and a hard terminator there turned the
        // surface relief into black mould-like blotches across every tile. A
        // 1.24 m panel a foot away genuinely has a terminator a foot wide; one
        // across the room genuinely has a crisp one. sin of the half-angle it
        // subtends is that width, and it costs an inversesqrt.
        float w = clamp(PANEL_HALF * inversesqrt(d2 + PANEL_HALF*PANEL_HALF), 0.10, 0.80);
        float ndl = clamp((dot(N, Ln) + w)/(1.0 + w), 0.0, 1.0);
        light += uLightCol*(st*atten*ndl*5.4*uLightMul);
    }
    // Specular, once, for the one panel the surface is actually reflecting.
    // Running a lobe per light inside the loop cost about a fifth of the frame on
    // every level — including the matte ones, whose gloss is far too low for the
    // result to be visible — and it was the wrong answer anyway: a mirror shows
    // you what the reflection ray hits, not a blur of everything overhead. Trace
    // the ray to the ceiling plane, look up whichever fitting is there, and clamp
    // to its rectangle. The highlight that falls out is the panel's own shape
    // stretched across the floor, which is what makes wet tile read as wet tile.
    // 0.10 not 0.005: below about a tenth the lobe is worth a thousandth of the
    // room light and you cannot see it at any exposure, but the levels that are
    // nearly matte were still paying a shadow march per fragment to compute it.
    if (gGloss > 0.10 && R.y > 0.02){
        float t = (uLY - P.y) / R.y;
        if (t > 0.0){
            vec3 hit = P + R * t;
            vec2 gs = floor((hit.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(gs);
            if (st > 0.002){
                vec3 lc = vec3(gs.x*uLS + uLS*0.5, uLY, gs.y*uLS + uLS*0.5);
                if (uEntDark > 0.01)
                    st *= mix(1.0, smoothstep(2.0, 9.0, distance(lc.xz, uEntPos.xz)), uEntDark);
                vec3 sp = vec3(clamp(hit.x, lc.x - PANEL_HALF, lc.x + PANEL_HALF), uLY,
                               clamp(hit.z, lc.z - PANEL_HALF, lc.z + PANEL_HALF));
                vec3 ld = sp - P;
                float d2 = dot(ld, ld);
                float lobe = sheen(N, V, ld*inversesqrt(max(d2,1e-6)), shin)
                             *gGloss*st*2.6/(1.0 + 0.22*d2);
                // Work out the lobe before tracing whether the panel is visible,
                // not after. It is a sharp highlight, so on any given frame it is
                // nonzero over a small band of the screen — and the trace is a
                // whole DDA. Testing the cheap thing first is where the poolrooms
                // got their frame time back.
                if (lobe > 0.002) light += uLightCol*(lobe*mix(0.18, 1.0, lightVis(sp.xz, shP)));
            }
        }
    }
    // Bounce fill, split by which way the surface looks. Flat ambient from every
    // direction is the other half of why this place read as a wash: a real room's
    // fill comes mostly off the lit ceiling, so up-facing surfaces catch far more
    // of it than down-facing ones, and that alone gives an unlit corner some shape.
    float hemi = 0.5 + 0.5*N.y;
    vec3 amb = uAmb * mix(vec3(1.05), uLightCol*1.75, hemi) * (0.35+0.65*uBlackout);
    // The level table's ambients were picked against a tone curve with no toe,
    // which returned about 1.25x its input near black; the filmic curve returns
    // about 0.21x there, so the same figure now arrives roughly six times darker
    // and the Red Halls fell from "almost black" to nothing readable at all.
    // Lift it back — but only where the toe is actually eating it. A flat six-fold
    // multiplier was the first attempt and it blew the poolrooms out to white
    // paper: that level's ambient is four times any other's, high enough that it
    // was never in the toe to begin with, so it got a correction it did not need.
    amb *= 1.0 + 5.5/(1.0 + 40.0*amb);
    light += amb;
    // a pool of shadow drapes the room lighting around the hunter (lamps + ambient)
    if (uEntDark > 0.01){
        float fd = distance(P.xz, uEntPos.xz);
        light *= mix(1.0, smoothstep(0.8, 6.5, fd), uEntDark);
    }
    // the flashlight and the flare cut through that dark — they are how you find it
    if (uFlash > 0.01){
        vec3 fv = P - uViewPos;
        float fd2 = dot(fv,fv);
        vec3 fn = fv * inversesqrt(max(fd2, 1e-6));
        float cone = pow(max(dot(fn, uFlashDir), 0.0), 26.0);
        float sput = 0.975 + 0.025*fract(sin(floor(uTime*24.0)*12.9898)*43758.5453);
        float fl = uFlash * cone * sput * 7.5/(1.0 + 0.10*fd2);
        // a torch is near enough a point source: its shadows stay crisp
        if (fl > 0.002) fl *= mix(0.06, 1.0, lightVis(uViewPos.xz, shP)) * entVis(uViewPos, vec3(shP.x, P.y, shP.y));
        vec3 fcol = vec3(1.0,0.97,0.86) * fl;
        light += fcol * clamp(dot(N, -fn)*0.7 + 0.3, 0.0, 1.0);
        // the beam skims off wet tile and polished floors too, not just walls
        if (gGloss > 0.10) light += fcol * (sheen(N, V, -fn, shin) * gGloss * 0.55);
    }
    if (uFlareInt > 0.01){                            // burning flare: orange point light
        vec3 lv2 = uFlarePos - P;
        float d2 = dot(lv2,lv2);
        float fi = uFlareInt * 5.0/(1.0 + 0.30*d2);
        if (fi > 0.002) fi *= mix(0.08, 1.0, lightVis(uFlarePos.xz, shP)) * entVis(uFlarePos, vec3(shP.x, P.y, shP.y));
        vec3 Lf = lv2 * inversesqrt(max(d2, 1e-6));
        float ndl = clamp(dot(N, Lf)*0.7 + 0.3, 0.0, 1.0);
        vec3 fcol = vec3(1.0,0.42,0.15) * fi;
        light += fcol * ndl;
        if (gGloss > 0.10) light += fcol * (sheen(N, V, Lf, shin) * gGloss * 0.55);
    }
    gLightLum = dot(light, vec3(0.30,0.59,0.11));
    return light;
}

// What the air itself is lit to between the eye and this fragment. The eye and
// the torch sit at the same point, so along any one view ray the cone term is
// constant and the whole in-scatter integral collapses to an arctangent — an
// exact answer for the price of one atan, no ray marching at all.
//
//   I(t) = cone * 7.5/(1 + 0.10 t^2)  ->  int_0^d = cone * 7.5 * atan(d*k)/k, k = sqrt(0.10)
//
// The flare is off-axis, so its integral is the same shape about the ray's point
// of closest approach to it. Both together are what puts a visible beam in the
// air and a halo round a burning flare, which is most of what "atmosphere" means
// in a corridor you cannot see the end of.
vec3 inScatter(vec3 ro, vec3 rd, float d){
    vec3 s = vec3(0.0);
    if (uFlash > 0.01){
        float cone = pow(max(dot(rd, uFlashDir), 0.0), 20.0);
        if (cone > 0.001){
            const float k = 0.31622777;              // sqrt(0.10), matching the beam falloff
            s += vec3(1.0,0.97,0.86) * (uFlash * cone * 7.5 * atan(d*k) / k);
        }
    }
    if (uFlareInt > 0.01){
        vec3 h = uFlarePos - ro;
        float b = dot(h, rd), c = dot(h, h);
        const float kf = 0.30;                       // matches the flare's own 1/(1+0.30 d2)
        float m = sqrt(max(c - b*b + 1.0/kf, 1e-4));
        s += vec3(1.0,0.42,0.15) * (uFlareInt * 5.0 / (kf*m) * (atan((d-b)/m) - atan(-b/m)));
    }
    return s;
}
// Filmic curve. The old 1-exp(-1.25x) reached 71% of white by x=1 and 92% by
// x=2, so every lit surface in the building landed inside the same narrow band
// near the top and the place had no dynamic range left to show a pool of light
// with. This keeps a toe under the shadows and a shoulder over the highlights,
// so a dark corner can be genuinely dark and a fluorescent can genuinely blow out.
vec3 tonemap(vec3 x){
    x *= 0.70;
    return clamp((x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14), 0.0, 1.0);
}
void main(){
    vec3 dpdx = dFdx(fragPos), dpdy = dFdy(fragPos);
    vec2 duvdx = dFdx(fragUV), duvdy = dFdy(fragUV);
    gGloss = uGloss;
    vec3 col;
    float aOut = 1.0;
    float dist = distance(fragPos, uViewPos);
    if (fragC.a < 0.62){
        if (fragC.a < 0.1){                          // light panel (emissive, flickers)
            vec2 g = floor((fragPos.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(g);
            // A dead diffuser is still a sheet of white plastic catching the room,
            // not a hole in the ceiling — that is what the first term is for.
            col = vec3(0.72,0.70,0.65) * roomLight(fragPos, vec3(0.0,-1.0,0.0)) + uLightCol*5.2*st;
            col *= 0.93 + 0.07*sin(fragUV.x*33.0)*sin(fragUV.y*33.0);   // prismatic lens ribs
            gLightLum = max(gLightLum, st);
        } else if (fragC.a < 0.3){                   // raw emissive (exit glow)
            col = fragC.rgb * 3.0;
            gLightLum = 1.0;
        } else if (fragC.a < 0.45){                  // window glass: clear in the middle, sheen at grazing angles
            vec3 N = normalize(fragN);
            vec3 V = normalize(uViewPos - fragPos);
            float fres = pow(1.0 - abs(dot(V, N)), 3.0);
            col = fragC.rgb * 3.0 + roomLight(fragPos, N) * 0.15;   // faint cool tint + a little room light
            col += vec3(0.55,0.62,0.72) * fres;                    // edge highlight where light skims the pane
            aOut = 0.10 + fres * 0.55;                             // see-through face, denser at the edges
        } else {                                     // water surface
            vec3 N = normalize(fragN);
            vec3 V = normalize(uViewPos - fragPos);
            // ripple the surface normal rather than just its brightness: still
            // water that only pulses is a painted floor, water that bends the
            // reflection of the ceiling lights is water
            float w1 = sin(fragPos.x*2.3 + uTime*1.4)*sin(fragPos.z*1.9 - uTime*1.1);
            float w2 = sin(fragPos.x*5.1 - uTime*2.2 + fragPos.z*1.3);
            vec3 Nw = normalize(N + vec3(0.055*w2 + 0.03*w1, 0.0, 0.045*w1 - 0.025*w2));
            vec3 light = roomLight(fragPos, Nw);
            // water is the glossiest thing in the building whatever the level says
            float fres = 0.03 + 0.97*pow(1.0 - max(dot(V, Nw), 0.0), 5.0);
            col = fragC.rgb * (light*0.75 + uAmb*1.4) * (0.85 + 0.15*w1);
            col += light * fres * 0.45;
            aOut = 0.60 + 0.30*fres;                 // near-clear looking down, a mirror at a glance
        }
    } else {
        vec4 texel = texture(texture0, fragUV);
        vec4 detail = texture(texture1, fragUV);
        vec3 Nb = normalize(fragN);
        // Detail is tied to material UVs and mipmaps, so it stays attached to
        // the surface and filters away at distance instead of crawling. Alpha
        // 254 still opts out: held objects and flat decals remain smooth.
        if (fragC.a > 0.998) {
            vec2 slope = (detail.rg * 255.0 - 128.0) / 127.0;
            Nb = detailNormal(Nb, slope, dpdx, dpdy, duvdx, duvdy);
            gGloss *= detail.b;
        }
        col = texel.rgb * fragC.rgb * roomLight(fragPos, Nb);
        aOut = fragC.a * texel.a;                    // translucent contact shadows + scrawl decals
    }
    float f = clamp(exp(-dist*uFogDen), 0.0, 1.0);
    // Fog used to be one flat colour everywhere, which meant the far end of a
    // pitch-dark corridor glowed exactly as much as the far end of a lit one.
    // Tint it by how lit the thing behind it is instead, so distance reads as
    // depth and darkness stays dark.
    vec3 fogc = uFogCol * mix(0.10, 1.0, uBlackout) * (0.45 + 1.15*clamp(gLightLum, 0.0, 1.0));
    col = mix(fogc, col, f);
    // the beam and the flare light the air on the way here, not just the surface
    vec3 rd = (fragPos - uViewPos) / max(dist, 1e-4);
    // Scaled by the level's own fog density, because the air is what does the
    // scattering — a level with clear air should not have a visible beam. The
    // constant is low on purpose: at twice this the torch stopped reading as a
    // beam and started reading as a wall of haze with the room lost behind it.
    col += inScatter(uViewPos, rd, dist) * (uFogDen * 0.10);
    col = tonemap(col);
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
    float ca = 0.00015 + uFear*0.0025;               // chromatic aberration
    vec3 c;
    c.r = texture(texture0, uv + dir*ca).r;
    c.g = texture(texture0, uv).g;
    c.b = texture(texture0, uv - dir*ca).b;
    // Threshold each sample before filtering: averaging the room first erased
    // isolated lights while making whole bright walls glow. Twelve fixed taps,
    // down from twenty, keep the glow restrained and the image readable.
    vec2 pxs = 1.0/vec2(textureSize(texture0, 0));
    const vec2 offsets[4] = vec2[4](vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1));
    vec3 bl = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        vec2 o = offsets[i] * pxs;
        bl += max(texture(texture0, uv + o*2.0).rgb - 0.78, 0.0) * 0.50;
        bl += max(texture(texture0, uv + o*6.0).rgb - 0.78, 0.0) * 0.32;
        bl += max(texture(texture0, uv + o*15.0).rgb - 0.78, 0.0) * 0.18;
    }
    c += bl * vec3(1.10, 1.03, 0.88) * 0.34;
    // gentle filmic contrast + a touch of saturation, so it's less flat
    vec3 s = c*c*(3.0 - 2.0*c);
    c = mix(c, s, 0.16);
    float lum0 = dot(c, vec3(0.299,0.587,0.114));
    c = mix(vec3(lum0), c, 1.08);
    float g = hh(uv*vec2(1287.0,721.0) + vec2(fract(uTime*13.71)*61.0, fract(uTime*7.31)*83.0)) - 0.5;
    c += g * (0.012 + 0.055*uFear);                   // film grain
    float d = length(dir);
    c *= 1.0 - smoothstep(0.34, 0.95, d)*(0.42 + 0.34*uFear); // vignette
    c *= 0.994 + 0.006*sin(uTime*377.0);             // mains-hum luma shimmer
    finalColor = vec4(c, 1.0);
}
)GLSL";
