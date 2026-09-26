#include "shaders.h"

// The shader bodies below are shared verbatim by both targets: GLSL 330 and
// GLSL ES 3.00 agree on everything they use (in/out, texture(), texelFetch,
// dFdx). Only the version line and ES's mandatory precision declarations
// differ, so that prologue — and nothing else — is per-platform.
//
// ES gives samplers a default precision of lowp, which is not enough for the
// occupancy grid: texelFetch there returns a cell code scaled by 255 and
// rounded to an int, and lowp carries about 8 distinct values, so every wall
// code collapses into its neighbours and the shadows come out of the wrong
// cells. Declare the sampler precision rather than inheriting it.
//
// The native string keeps the leading newline that the raw literal used to
// contribute, so the GLSL the desktop build compiles is unchanged to the byte
// (prove it with the assembly diff in AGENTS.md). The web string must NOT have
// it: #version has to be the first token, and ANGLE is entitled to reject a
// shader whose directive is preceded by anything at all.
#ifdef PLATFORM_WEB
#define GLSL_VERSION_HEADER "#version 300 es\n" \
                            "precision highp float;\n" \
                            "precision highp int;\n" \
                            "precision highp sampler2D;"
#else
#define GLSL_VERSION_HEADER "\n#version 330"
#endif

// ---------------------------------------------------------------- shaders
const char *WORLD_VS = GLSL_VERSION_HEADER R"GLSL(
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec3 fragPos; out vec2 fragUV; out vec3 fragN; out vec4 fragC;
void main(){
    fragPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fragUV = vertexTexCoord; fragN = (matNormal * vec4(vertexNormal, 0.0)).xyz; fragC = vertexColor;
    gl_Position = mvp*vec4(vertexPosition,1.0);
}
)GLSL";

const char *WORLD_FS = GLSL_VERSION_HEADER R"GLSL(
in vec3 fragPos; in vec2 fragUV; in vec3 fragN; in vec4 fragC;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime; uniform float uBlackout; uniform vec3 uViewPos;
uniform float uFlash; uniform vec3 uFlashDir;
uniform vec3 uFlarePos; uniform float uFlareInt;
uniform vec3 uEntPos; uniform float uEntDark;      // the hunter kills the lights around it
uniform vec3 uAmb; uniform vec3 uFogCol; uniform float uFogDen;
uniform vec3 uLightCol; uniform float uLS; uniform float uLY; uniform float uDead; uniform float uLightMul;
uniform float uGloss;
uniform float uVary;             // how uneven the working tubes are (Level 0)
uniform float uFaulty;           // share of tubes that stutter
uniform float uWet;              // Level 0's sodden carpet: 1 = damp patches on the floor
uniform float uWetFrom;          // where on the patch field they start: 0.60 L0 carpet, 0.78 L1 puddles
uniform vec4 uRoomMask;          // x0,z0,x1,z1: ceiling panels centred in here are dark (the Manila Room)
uniform vec4 uLamp;              // xyz: the one spare point light — the Manila Room's chandelier, or
                                 // the nearest stairwell's landing batten — w: its output (0 = none near)
uniform vec3 uLampCol;           // ...and its colour: filament warm, or fluorescent
uniform sampler2D texture1; // packed material slopes / gloss mask
float gGloss;
uniform sampler2D texture2;      // occupancy grid (material normal-map slot)
uniform vec2 uOccOrigin;         // world cell coords of texel (0,0)
uniform float uOccN;             // grid side in cells; 0 = no grid, everything lit
uniform float uEntBlock;         // 1 while the thing is out — it occludes light too
uniform float uStoreyH;          // floor-to-floor pitch; 0 on a level that is one floorplan
uniform float uStorey;           // the storey you are on, for its tubes' own failures
out vec4 finalColor;

// Storeys (World::storeyH). The storey you stand on is always the one at y = 0,
// and the ones above and below are drawn a pitch up or down, seen through the
// openings between them. Which one a fragment belongs to decides whose tubes
// light it, and which byte of the occupancy grid its shadows march: r is yours,
// g the storey below, b the one above. Two storeys off there is no grid; it is
// taken as open. gChan picks the byte by a dot product, not a branch.
int gRel = 0;
vec4 gChan = vec4(1.0, 0.0, 0.0, 0.0);
vec4 chanFor(int rel){
    return vec4(rel == 0 ? 1.0 : 0.0, rel == -1 ? 1.0 : 0.0, rel == 1 ? 1.0 : 0.0, 0.0);
}
// The per-storey offset into the tube hash. MUST match storeyHashOffset() in
// levels.cpp. Bounded, because sin() loses the hash at large arguments.
float storeyOffset(float st){ return fract(st * 0.6180339) * 97.0; }

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
float lightState(vec2 g, float so){
    float h = lhash(g + vec2(so, so * 1.7));
    // Not every tube that works works well: some run at part output, a
    // spread uVary wide. levels.cpp's lightAtCPU applies the same factor.
    float s = 1.0 - uVary * fract(h*53.7);
    // Keep this arithmetic-only: desktop ANGLE can corrupt fragments when
    // the reflected panel's light state adds nested divergent control flow.
    float fh = fract(h*97.31);
    float gate = fract(sin(floor(uTime*0.45+fh*37.0)*12.9898)*43758.5453);
    float n = fract(sin(uTime*(7.0+fh*10.0) + fh*211.0)*43758.5453);
    float faulty = 1.0 - step(h, 1.0 - uFaulty);
    float flicker = faulty * (1.0 - step(gate, 0.74));
    s *= mix(1.0, 0.62 + 0.38*step(0.5, n), flicker);
    vec2 pc = g*uLS + uLS*0.5;
    float masked = step(uRoomMask.x, pc.x) * step(pc.x, uRoomMask.z)
                 * step(uRoomMask.y, pc.y) * step(pc.y, uRoomMask.w);
    return s * uBlackout * step(uDead, h) * (1.0 - masked);
}
int occAtC(ivec2 c, vec4 ch){
    ivec2 t = c - ivec2(uOccOrigin);
    int n = int(uOccN);
    if (t.x < 0 || t.y < 0 || t.x >= n || t.y >= n) return 0;   // off-grid: assume open
    return int(dot(texelFetch(texture2, t, 0), ch) * 255.0 + 0.5);
}
int occAt(ivec2 c){ return occAtC(c, gChan); }
// Pillars occupy 1.16 m inside a 2 m cell (World::ensureMesh / collision).
// Test that footprint even in the ray's first and last cells. Skipping those
// cells left a bright square around every pillar, followed by oversized shadows.
bool pillarBlocks(ivec2 c, vec2 a, vec2 invDir, float dist){
    vec2 lo = (vec2(c)*2.0 + vec2(0.42) - a) * invDir;
    vec2 hi = (vec2(c)*2.0 + vec2(1.58) - a) * invDir;
    vec2 entry = min(lo, hi), leave = max(lo, hi);
    return (occAt(c) & 4) != 0 &&
           max(max(entry.x, entry.y), 0.001) < min(min(leave.x, leave.y), dist - 0.001);
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
    // Every panel centre (ls/2 on an 8 or 12 m grid) is a cell corner. A ray
    // starting exactly there "crosses" edges at t = 0, including a wall that
    // only touches the corner, so the single centre tap beyond 6 m was
    // falsely shadowed: a sphere round every light, seen as bright rings on
    // floors and walls. Start inside the cell the ray heads into. The panel
    // overhangs all four cells round its corner, so it lights each of them.
    a += dir * 0.01;
    dist -= 0.01;
    ivec2 c = ivec2(floor(a / 2.0)), ec = ivec2(floor(b / 2.0));
    vec2 invDir = mix(vec2(-1.0), vec2(1.0), greaterThanEqual(dir, vec2(0.0)))
                  / max(abs(dir), vec2(1e-6));
    if (pillarBlocks(c, a, invDir, dist)) return 0.0;
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
        if (pillarBlocks(c, a, invDir, dist)) return 0.0;
        if (c == ec) return 1.0;
    }
    return 1.0;
}
// Light from the panel centred at `lc` reaching `p`, traced from two taps
// `spread` apart across the ray (or one tap at the centre when spread is ~0).
// The centre is a cell corner and corridor walls often run through it, so a
// tap pushed straight across the ray could land beyond the wall: that tap was
// blocked, the light halved inside 6 m and not beyond, and every such panel
// drew a hard dark sphere on floors and walls. Keep both taps inside the cell
// the centre ray heads into, the same cell lightVis starts the far tap in.
float panelVis(vec2 lc, vec2 p, float spread){
    vec2 toP = p - lc;
    float toLen = length(toP);
    // directly overhead there's no meaningful direction to spread along
    vec2 dir = toLen > 0.001 ? toP / toLen : vec2(0.0, 1.0);
    float vis;
    if (spread < 0.01) {
        vis = lightVis(lc, p);
    } else {
        vec2 q = floor((lc + dir * 0.01) / 2.0) * 2.0;
        vec2 perp = vec2(-dir.y, dir.x) * spread;
        vec2 ta = clamp(lc + perp, q + 0.01, q + 1.99);
        vec2 tb = clamp(lc - perp, q + 0.01, q + 1.99);
        vis = 0.5 * (lightVis(ta, p) + lightVis(tb, p));
    }
    return vis;
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
// The nine fittings of one storey's grid around P, lit, shadowed and summed.
// `ly` is that grid's light plane, and bit (dx+1)*3 + (dz+1) of `mask` says
// whether the fitting at base + (dx, dz) is there to light anything (see the
// fitting masks in roomLight). `upper`: these hang a storey up, over the hole.
vec3 panelSum(vec3 P, vec3 N, vec2 base, vec2 shP, float ly, float so, bool upper, int mask){
    vec3 light = vec3(0.0);
    for (int dx=-1; dx<=1; dx++)
    for (int dz=-1; dz<=1; dz++){
        if (((mask >> ((dx + 1) * 3 + (dz + 1))) & 1) == 0) continue;
        vec2 g = base + vec2(float(dx), float(dz));
        vec3 lc = vec3(g.x*uLS + uLS*0.5, ly, g.y*uLS + uLS*0.5);   // panel centre
        float st = lightState(g, so);
        if (st <= 0.001) continue;
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
        vec3 lp = vec3(clamp(P.x, lc.x - PANEL_HALF, lc.x + PANEL_HALF), ly,
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
        // times as much, which was too much. The taps must close up onto the
        // centre before the switch: cutting from two taps to one at a fixed
        // range drew a hard sphere around every panel, and that showed as big
        // bright circles and arcs across the floor and walls.
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
        // Nor is a fitting a storey up, shining down the hole this point is
        // under (`upper`): the line between them stays inside the opening's
        // footprint, where nothing stands but rails, which pass light, and a
        // stairwell's core wall, which runs directly under those fittings
        // and would have each of them light one flight and not the other.
        // Tracing them cost a seventh of the frame in a stair shaft.
        if (upper || sw < 0.002) {
            vis = 1.0;                                   // too far to shadow; it's faint anyway
        } else {
            // two taps for a little penumbra up close, closing onto one by 6 m
            // The range scales with the grid, capped at the original 6 m, so
            // the 8 and 12 m levels trace exactly as before. On Level 0's
            // 4 m grid all nine summed panels sit inside 6 m, and two taps
            // each for all nine cost a quarter of the frame; the next tube
            // along is 4 m away, so the penumbra closes by 3 m there.
            float sr2 = min(36.0, 0.5625*uLS*uLS), sr1 = min(16.0, 0.25*uLS*uLS);   // 36 and 16 at ls >= 8
            float spread = d2 < sr2 ? 0.45 * (1.0 - smoothstep(sr1, sr2, d2)) : 0.0;
            vis = panelVis(lc.xz, shP, spread);
        }
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
    return light;
}
vec3 roomLight(vec3 P, vec3 N){
    vec2 base = floor((P.xz - uLS*0.5)/uLS + 0.5);
    vec3 light = vec3(0.0);
    vec3 V = normalize(uViewPos - P);
    float shin = mix(20.0, 210.0, gGloss);
    // reflection ray, for picking the point on a panel this surface can actually
    // see a highlight from — see the representative-point note below
    vec3 R = reflect(-V, N);
    // Bias along geometry, never tile relief: a perturbed floor normal can
    // move the lookup across an adjoining wall and light a strip behind it.
    vec2 shP = P.xz + normalize(fragN).xz * 0.16;
    // Storeys: your own storey's tubes, less any that would hang in an
    // opening; and where this point has no ceiling (bit 4), the tubes of the
    // storey above that hang over the hole as well — the light that falls
    // down a stairwell, into a double-height hall. Which fittings are there is
    // one fetch of the fitting masks (the occupancy texture's second n rows,
    // World::buildOccupancy): a byte per storey, and the ninth bits in alpha.
    // A fitting a storey up shines down the hole exactly where it exists up
    // there and is missing here, because a hole above is an opening below.
    // Only near an opening, which bit 6 marks: everywhere else — nearly every
    // fragment — is the plain nine-panel sum, for one extra fetch.
    int own = uStoreyH > 0.0 ? occAt(ivec2(floor(shP * 0.5))) : 0;
    int ownMask = 511, upMask = 0;
    if ((own & 64) != 0){
        int n = int(uOccN);
        ivec2 t = ivec2(base * (uLS * 0.5)) - ivec2(uOccOrigin);   // the block's first cell
        if (t.x >= 0 && t.y >= 0 && t.x < n && t.y < n){
            vec4 m = texelFetch(texture2, ivec2(t.x, t.y + n), 0) * 255.0;
            int hi = int(m.a + 0.5);
            int ci = int(dot(vec4(0.0, 1.0, 2.0, 0.0), gChan) + 0.5);   // R, G or B
            ownMask = int(dot(m, gChan) + 0.5) | (((hi >> ci) & 1) << 8);
            if (gRel < 1 && (own & 16) != 0){
                vec4 uc = chanFor(gRel + 1);
                int cu = int(dot(vec4(0.0, 1.0, 2.0, 0.0), uc) + 0.5);
                int upper = int(dot(m, uc) + 0.5) | (((hi >> cu) & 1) << 8);
                upMask = upper & ~ownMask & 511;
            }
        }
    }
    float ownLY = uLY + float(gRel) * uStoreyH;
    light += panelSum(P, N, base, shP, ownLY, storeyOffset(uStorey + float(gRel)), false, ownMask);
    if (upMask != 0)
        light += panelSum(P, N, base, shP, ownLY + uStoreyH, storeyOffset(uStorey + float(gRel + 1)),
                          true, upMask);
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
        float t = (ownLY - P.y) / R.y;
        if (t > 0.0){
            vec3 hit = P + R * t;
            vec2 gs = floor((hit.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(gs, storeyOffset(uStorey + float(gRel)));
            if (st > 0.002){
                vec3 lc = vec3(gs.x*uLS + uLS*0.5, ownLY, gs.y*uLS + uLS*0.5);
                if (uEntDark > 0.01)
                    st *= mix(1.0, smoothstep(2.0, 9.0, distance(lc.xz, uEntPos.xz)), uEntDark);
                vec3 sp = vec3(clamp(hit.x, lc.x - PANEL_HALF, lc.x + PANEL_HALF), ownLY,
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
                if (lobe > 0.002) light += uLightCol*(lobe*lightVis(sp.xz, shP));
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
    // The Manila Room's chandelier: six warm bulbs, near enough one point.
    // Walled in by lightVis like the torch and the flare, so its warmth stays
    // in the room and spills only out of the four doorways.
    if (uLamp.w > 0.01){
        vec3 lv3 = uLamp.xyz - P;
        float d2 = dot(lv3, lv3);
        if (d2 < 90.0){
            float li = uLamp.w * 3.2/(1.0 + 0.26*d2) * (1.0 - smoothstep(55.0, 90.0, d2));
            li *= lightVis(uLamp.xz, shP);
            vec3 Ll = lv3 * inversesqrt(max(d2, 1e-6));
            vec3 lcol = uLampCol * li;
            light += lcol * clamp(dot(N, Ll)*0.75 + 0.25, 0.0, 1.0);
            if (gGloss > 0.10) light += lcol * (sheen(N, V, Ll, shin) * gGloss * 0.6);
        }
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
        float cone = pow(max(dot(rd, uFlashDir), 0.0), 26.0);
        if (cone > 0.001){
            const float k = 0.31622777;              // sqrt(0.10), matching the beam falloff
            // Backscatter toward the flashlight holder should be faint: the old
            // strength washed out the very surfaces the torch was illuminating.
            const float torchScatter = 0.20;
            s += vec3(1.0,0.97,0.86) * (torchScatter * uFlash * cone * 7.5 * atan(d*k) / k);
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
    // Which storey this fragment is on: a floor belongs to its own storey,
    // a ceiling and the plenum over it to the one they hang in, and the last
    // 30 cm of a flight to the floor it arrives at.
    if (uStoreyH > 0.0){
        gRel = int(floor((fragPos.y + 0.3) / uStoreyH));
        gChan = chanFor(gRel);
    }
    vec3 col;
    float aOut = 1.0;
    float dist = distance(fragPos, uViewPos);
    if (fragC.a < 0.62){
        if (fragC.a < 0.1){                          // light panel (emissive, flickers)
            vec2 g = floor((fragPos.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(g, storeyOffset(uStorey + float(gRel)));
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
            float fres = 0.03 + 0.97*pow(1.0 - abs(dot(V, Nw)), 5.0);
            col = fragC.rgb * (light*0.75 + uAmb*1.4) * (0.85 + 0.15*w1);
            col += light * fres * 0.45;
            aOut = 0.30 + 0.55*fres;                 // near-clear looking down, a mirror at a glance
        }
    } else {
        // Level 0's noclip walls (vertex alpha 250, see noclipCol in
        // world.cpp): the paper tears sideways in thin horizontal bands that
        // jump a dozen times a second, most of the time not at all. Grad
        // sampling with the untorn derivatives, or every band edge picks the
        // smallest mip and draws a line.
        bool noclip = fragC.a > 0.965 && fragC.a < 0.990;
        vec2 uvT = fragUV;
        float tear = 0.0;
        if (noclip){
            float tick = floor(uTime*11.0);
            float band = floor(fragPos.y*9.0 + fract(tick*0.37)*4.0);
            float r = fract(sin(band*91.7 + tick*7.31)*43758.5453);
            float burst = step(0.62, fract(sin(floor(uTime*1.3)*3.7)*151.3));   // it comes and goes
            // ragged, not a clean slab: each band tears in 30 cm pieces, some of
            // which hold, so its ends are broken rather than the wall's edges
            float seg = floor((fragPos.x + fragPos.z) * 3.3);
            float keep = step(0.30, fract(sin(seg*47.1 + band*13.7 + tick*3.9)*43758.5453));
            tear = step(0.58, r) * burst * keep;
            uvT.x += (r - 0.5) * 0.09 * tear;
        }
        // everything else keeps the plain sample it always had
        vec4 texel = noclip ? textureGrad(texture0, uvT, duvdx, duvdy) : texture(texture0, fragUV);
        vec4 detail = texture(texture1, fragUV);
        gGloss = detail.a < 0.75 ? detail.b : uGloss * detail.b;
        vec3 Nb = normalize(fragN);
        // Detail is tied to material UVs and mipmaps, so it stays attached to
        // the surface and filters away at distance instead of crawling. Alpha
        // 254 still opts out: held objects and flat decals remain smooth.
        if (fragC.a > 0.998) {
            vec2 slope = (detail.rg * 255.0 - 128.0) / 127.0;
            Nb = detailNormal(Nb, slope, dpdx, dpdy, duvdx, duvdy);

        }
        // Level 0's carpet is "old" and "moist" in every telling, and whatever
        // soaks it "is not water". Damp patches: darker, browner pile that
        // has the gloss to mirror the tubes overhead, and flattened relief,
        // because liquid fills the loops. Floors only (up-facing, at or below
        // deck level), and in world space so the patches never tile.
        if (uWet > 0.0 && fragN.y > 0.7 && fragPos.y - float(gRel) * uStoreyH < 0.3){
            float wn = vnoise(fragPos.xz*0.42)*0.62 + vnoise(fragPos.xz*1.35 + 17.0)*0.38;
            float wet = smoothstep(uWetFrom, uWetFrom + 0.12, wn) * uWet;
            texel.rgb *= mix(vec3(1.0), vec3(0.60, 0.56, 0.48), wet);
            gGloss = max(gGloss, 0.62*wet);
            Nb = normalize(mix(Nb, normalize(fragN), wet));
        }
        col = texel.rgb * fragC.rgb * roomLight(fragPos, Nb);
        // Underwater tile catches moving ribbons of refracted light. Pool
        // floors are the only glossy world surfaces this far below the deck.
        if (uGloss > 0.5 && fragPos.y < -0.13) {
            float depth=-0.12-fragPos.y;
            float c1=sin(fragPos.x*4.1+sin(fragPos.z*2.7+uTime)*1.4+uTime*0.8);
            float c2=sin(fragPos.z*4.6+sin(fragPos.x*3.2-uTime*0.7)-uTime*0.6);
            float caustic=pow(max(0.0,1.0-abs(c1+c2)*0.7),12.0);
            col *= mix(vec3(1),vec3(0.32,0.77,0.68),1.0-exp(-depth*0.32));
            col += vec3(0.12,0.24,0.19)*caustic*exp(-depth*0.22)*clamp(gLightLum,0.0,1.0);
        }
        aOut = fragC.a * texel.a;                    // translucent contact shadows + scrawl decals
        if (noclip){
            // Reality is thin here, and the torn bands show what is behind
            // it: a flat, over-bright nothing on an ordinary exit, and on a
            // cursed one (vertex alpha 247) the crimson of
            // the Red Rooms. A faint shimmer runs all the time, torn or not,
            // so a wanderer who stops and looks can find it.
            float shimmer = 0.035 * sin(uTime*23.0 + fragPos.y*41.0 + fragPos.x*7.0 + fragPos.z*7.0);
            vec3 behind = fragC.a < 0.975 ? vec3(0.55, 0.03, 0.02) : vec3(1.25, 1.18, 0.95);   // 247 cursed, 250 not
            col = mix(col * (1.0 + shimmer), behind * (0.6 + 0.4*gLightLum), tear * 0.55);
            aOut = 1.0;
        }
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
    // flashlight has its own weaker scattering weight above; reducing this
    // shared factor would also dim the flare halo.
    col += inScatter(uViewPos, rd, dist) * (uFogDen * 0.10);
    col = tonemap(col);
    finalColor = vec4(col, aOut) * colDiffuse;
}
)GLSL";

const char *POST_FS = GLSL_VERSION_HEADER R"GLSL(
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime; uniform float uFear; uniform float uWater;
uniform float uMigraine;   // Level 0's hum headache, 0..1 (Game::migraine)
out vec4 finalColor;
float hh(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
void main(){
    vec2 uv = fragTexCoord;
    if (uWater > 0.0) {
        uv += uWater*0.0018*vec2(sin(uv.y*24.0+uTime*1.3),sin(uv.x*21.0-uTime));
        uv = clamp(uv,vec2(0.002),vec2(0.998));
    }
    // The migraine throbs: a slow double pulse, like a heartbeat behind the
    // eyes, that pinches the image in a little and splits its colour.
    float throb = 0.0;
    if (uMigraine > 0.0) {
        float ph = fract(uTime * 1.05);
        throb = uMigraine * (exp(-ph*ph*90.0) + 0.6*exp(-(ph-0.22)*(ph-0.22)*120.0));
        uv = 0.5 + (uv - 0.5) * (1.0 - 0.006*throb);
    }
    vec2 dir = uv - 0.5;
    float ca = 0.00015 + uFear*0.0025 + throb*0.0035;   // chromatic aberration
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
    c *= 1.0 - smoothstep(0.34, 0.95, d)*(0.42 + 0.34*uFear + 0.30*throb); // vignette
    c *= 1.0 - 0.05*throb;                               // and the whole frame dims on the beat
    c *= 0.994 + 0.006*sin(uTime*377.0);             // mains-hum luma shimmer
    c = mix(c,c*vec3(0.48,0.86,0.80)+vec3(0.015,0.07,0.065),uWater*0.75);
    finalColor = vec4(c, 1.0);
}
)GLSL";
