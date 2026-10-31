#include "shaders.h"
#include "core/layout.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static const char *WORLD_FS_TEMPLATE = GLSL_VERSION_HEADER R"GLSL(
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
uniform vec4 uLamp;              // xyz: the spare point light (the Manila Room's chandelier or
                                 // the nearest stairwell's landing batten); w: its output, 0 = none
uniform vec3 uLampCol;           // ...and its colour: filament warm, or fluorescent
uniform float uMacro;            // world-space tone wander on tiled surfaces (SURF_MACRO)
uniform float uBoard;            // suspended-ceiling board pitch, metres; 0 = none (CEIL_BOARD)
uniform float uObjRefl;          // 1 while the held revolver draws: reflect the room off metal
uniform sampler2D texture1; // packed material slopes / gloss mask
float gGloss;
uniform sampler2D texture2;      // occupancy grid (material normal-map slot)
uniform vec2 uOccOrigin;         // world cell coords of texel (0,0)
uniform float uOccN;             // grid side in cells; 0 = no grid, everything lit
uniform float uEntBlock;         // 1 while the hunter is present: it casts a shadow
uniform float uStoreyH;          // floor-to-floor pitch; 0 on a level that is one floorplan
uniform float uStorey;           // the current storey, for its tube hash
uniform float uDrawRel;          // which storey the mesh being drawn belongs to, relative to the current one
out vec4 finalColor;

// Storeys (World::storeyH). The current storey is always the one at y = 0,
// and the ones above and below are drawn a pitch up or down, seen through the
// openings between them. Which one a fragment belongs to decides whose tubes
// light it, and which byte of the occupancy grid its shadows march: r is this storey,
// g the storey below, b the one above. Two storeys off there is no grid; it is
// taken as open. gChan picks the byte by a dot product, not a branch.
int gRel = 0;
vec4 gChan = vec4(1.0, 0.0, 0.0, 0.0);
vec4 chanFor(int rel){
    return vec4(rel == 0 ? 1.0 : 0.0, rel == -1 ? 1.0 : 0.0, rel == 1 ? 1.0 : 0.0, 0.0);
}
// The per-storey offset into the tube hash: storeyHashOffset() in core, whose
// numbers worldFragmentShader() writes in. Bounded, because sin() loses the hash.
float storeyOffset(float st){ return fract(st * @STOREY_HASH_STEP@) * @STOREY_HASH_SPAN@; }

// Half-width of one ceiling diffuser: PANEL_HALF in core/layout.h, which the
// mesher and lightAtCPU read too. worldFragmentShader() writes the number in,
// so the lighting always treats the panel as the rectangle the mesher draws.
// The token below is never compiled as it stands.
const float PANEL_HALF = @PANEL_HALF@;

// How lit the shaded point came out. roomLight() fills this in so the fog can be
// lit like the surface. Set once per fragment, read at the end.
float gLightLum = 0.0;

float lhash(vec2 g){ return fract(sin(dot(g, vec2(@TUBE_HASH_X@,@TUBE_HASH_Z@)))*@TUBE_HASH_SCALE@); }
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
    float h = lhash(g + vec2(so, so * @TUBE_STOREY_SKEW@));
    // Not every tube that works works well: some run at part output, a
    // spread uVary wide. levels.cpp's lightAtCPU applies the same factor.
    float s = 1.0 - uVary * fract(h*@TUBE_OUTPUT_SPREAD@);
    // Keep this arithmetic-only: desktop ANGLE can corrupt fragments when
    // the reflected panel's light state adds nested divergent control flow.
    float fh = fract(h*@TUBE_STUTTER_SPREAD@);
    float gate = fract(sin(floor(uTime*@TUBE_STUTTER_RATE@+fh*@TUBE_STUTTER_PHASE@)*@TUBE_STUTTER_HASH@)*@TUBE_HASH_SCALE@);
    float n = fract(sin(uTime*(@TUBE_BUZZ_BASE@+fh*@TUBE_BUZZ_SPREAD@) + fh*@TUBE_BUZZ_PHASE@)*@TUBE_HASH_SCALE@);
    float faulty = 1.0 - step(h, 1.0 - uFaulty);
    float flicker = faulty * (1.0 - step(gate, @TUBE_STUTTER_OPEN@));
    s *= mix(1.0, mix(@TUBE_STUTTER_DIP@, 1.0, step(@TUBE_BUZZ_DUTY@, n)), flicker);
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
// Pillars occupy 1.16 m inside a 2 m cell (bakeChunk / collision).
// Test that footprint even in the ray's first and last cells. Skipping those
// cells left a bright square around every pillar, followed by oversized shadows.
bool pillarBlocks(ivec2 c, vec2 a, vec2 invDir, float dist){
    vec2 lo = (vec2(c)*2.0 + vec2(@PILLAR_LO@) - a) * invDir;
    vec2 hi = (vec2(c)*2.0 + vec2(@PILLAR_HI@) - a) * invDir;
    vec2 entry = min(lo, hi), leave = max(lo, hi);
    return (occAt(c) & @OCC_PILLAR@) != 0 &&
           max(max(entry.x, entry.y), 0.001) < min(min(leave.x, leave.y), dist - 0.001);
}
// Does light from `a` reach `b`? Walls are a floorplan extruded to full height,
// so this is a 2D grid march: step cell to cell and test the edge we cross.
// Doorways, window glass and open gaps are not in the grid, so light passes
// through them.
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
            if ((occAt(ivec2(stp.x > 0 ? c.x : c.x + 1, c.y)) & @OCC_WALL_W@) != 0) return 0.0;
            tMax.x += tDelta.x;
        } else {
            if (tMax.y > dist) return 1.0;
            c.y += stp.y;
            if ((occAt(ivec2(c.x, stp.y > 0 ? c.y : c.y + 1)) & @OCC_WALL_N@) != 0) return 0.0;
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
// The hunter also casts a shadow. He is a body, not a full-height column: a ray
// passing over his head still gets through.
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
// a dielectric, so the highlight is weak head-on and strong at grazing angles.
// This is what makes the Poolrooms' tile reflect the lights.
float sheen(vec3 N, vec3 V, vec3 L, float shin){
    float nh = max(dot(N, normalize(L + V)), 0.0);
    float u = 1.0 - max(dot(V, N), 0.0), u2 = u*u;
    float f = 0.045 + 0.955*(u2*u2*u);                      // Schlick, by multiplies not pow()
    return pow(nh, shin) * (shin + 8.0) * 0.03978874 * f;   // (n+8)/8pi normalization
}
// The nine fittings of the current storey's grid around P, then, where upMask
// has any bit set, the nine of the storey above: lit, shadowed and summed. Bit
// (dx+1)*3 + (dz+1) of each mask says whether the fitting at base + (dx, dz)
// is there (see the fitting masks in roomLight).
//
// ONE loop, and the masks multiply rather than branch. The first version
// called a nine-fitting function twice, and in a desktop browser the floor
// below an atrium (the only place both calls run) came out black while native
// builds lit it, probably the same ANGLE miscompile as the lightState note in
// AGENTS.md.
vec3 panelSum(vec3 P, vec3 N, vec2 base, vec2 shP, float ownLY, float soOwn, float soUp,
              int ownMask, int upMask){
    vec3 light = vec3(0.0);
    int count = upMask != 0 ? 18 : 9;
    for (int i = 0; i < 18; i++){
        if (i >= count) break;
        bool upper = i >= 9;
        int k = upper ? i - 9 : i;
        int dx = k / 3 - 1, dz = k - (k / 3) * 3 - 1;
        float ly = upper ? ownLY + uStoreyH : ownLY;
        float on = float(((upper ? upMask : ownMask) >> k) & 1);
        vec2 g = base + vec2(float(dx), float(dz));
        vec3 lc = vec3(g.x*uLS + uLS*0.5, ly, g.y*uLS + uLS*0.5);   // panel centre
        float st = lightState(g, upper ? soUp : soOwn) * on;
        if (st <= 0.001) continue;
        if (uEntDark > 0.01){                        // fluorescents die in a pool around the hunter
            float ed = distance(lc.xz, uEntPos.xz);
            st *= mix(1.0, smoothstep(2.0, 9.0, ed), uEntDark);
        }
        // A ceiling panel is a 1.24 m square, not a point. A point light gave a
        // hard hotspot under every fitting and too even a falloff elsewhere.
        // Shade from the point on the panel closest to the surface (the usual
        // representative point), which costs two clamps and gives a soft
        // falloff near the fitting.
        vec3 lp = vec3(clamp(P.x, lc.x - PANEL_HALF, lc.x + PANEL_HALF), ly,
                       clamp(P.z, lc.z - PANEL_HALF, lc.z + PANEL_HALF));
        vec3 ld = lp - P;
        float d2 = dot(ld,ld);
        float atten = 1.0/(1.0 + 0.22*d2);
        // Only the 3x3 panels around this point are summed. Without a fade, a
        // light still at ~8% at the window's edge disappears when P crosses into
        // the next cell, leaving a seam along every light-cell boundary. Fade
        // each panel out before it leaves the window.
        vec2 wf = 1.0 - smoothstep(vec2(0.80), vec2(1.0), abs(lc.xz - P.xz)/(1.5*uLS));
        atten *= wf.x * wf.y;
        if (atten < 1e-5) continue;
        if (st*atten < 0.004) continue;              // too faint to be worth tracing
        // Two taps across the panel up close for a little penumbra, one beyond.
        // Two taps at every range cost about three times as much. The taps must
        // converge on the centre before the switch: cutting from two taps to one
        // at a fixed range drew bright circles and arcs around every panel.
        // The march stops after a fixed number of cells, and the shadow stops with
        // it. The DDA takes one step per cell, so the cutoff is |dx|+|dz| = const,
        // a diamond, which drew diagonal lines on the floor. Fade each light's
        // shadow out well before the cap, so the march never truncates in view.
        float l1 = abs(lc.x - shP.x) + abs(lc.z - shP.y);   // shP is a vec2: .y is world z
        // Gone by 15 m (7.5 cells), inside the 8-cell march, so it never
        // truncates in view. Most of the nine lights then skip tracing, which
        // saves the time.
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
        // Soften the terminator by the panel's apparent size, not a fixed amount.
        // The old 0.55/0.45 half-Lambert gave 45% brightness to surfaces edge-on
        // to the light, which made everything look flat. A hard Lambert failed
        // on the ceiling, which is level with the fittings and lit edge-on: the
        // surface relief turned into dark blotches. A
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
    // reflection ray, for picking the point on a panel that gives this surface's
    // highlight (see the representative-point note below)
    vec3 R = reflect(-V, N);
    // Bias along geometry, never tile relief: a perturbed floor normal can
    // move the lookup across an adjoining wall and light a strip behind it.
    vec2 shP = P.xz + normalize(fragN).xz * 0.16;
    // Storeys: the current storey's tubes, less any that would hang in an
    // opening; and where this point has no ceiling (bit 4), also the tubes of
    // the storey above that hang over the hole (light down a stairwell or into
    // a double-height hall). Which fittings are there is
    // one fetch of the fitting masks (the occupancy texture's second n rows,
    // World::buildOccupancy): a byte per storey, and the ninth bits in alpha.
    // A fitting a storey up shines down the hole exactly where it exists up
    // there and is missing here, because a hole above is an opening below.
    // Only near an opening (bit 6). Everywhere else, nearly every fragment,
    // is the plain nine-panel sum, for one extra fetch.
    int own = uStoreyH > 0.0 ? occAt(ivec2(floor(shP * 0.5))) : 0;
    int ownMask = 511, upMask = 0;
    if ((own & @OCC_NEAR_OPENING@) != 0){
        int n = int(uOccN);
        ivec2 t = ivec2(base * (uLS * 0.5)) - ivec2(uOccOrigin);   // the block's first cell
        if (t.x >= 0 && t.y >= 0 && t.x < n && t.y < n){
            vec4 m = texelFetch(texture2, ivec2(t.x, t.y + n), 0) * 255.0;
            int hi = int(m.a + 0.5);
            int ci = int(dot(vec4(0.0, 1.0, 2.0, 0.0), gChan) + 0.5);   // R, G or B
            ownMask = int(dot(m, gChan) + 0.5) | (((hi >> ci) & 1) << 8);
            if (gRel < 1 && (own & @OCC_OPEN_ABOVE@) != 0){
                vec4 uc = chanFor(gRel + 1);
                int cu = int(dot(vec4(0.0, 1.0, 2.0, 0.0), uc) + 0.5);
                int upper = int(dot(m, uc) + 0.5) | (((hi >> cu) & 1) << 8);
                upMask = upper & ~ownMask & 511;
            }
        }
    }
    float ownLY = uLY + float(gRel) * uStoreyH;
    light += panelSum(P, N, base, shP, ownLY, storeyOffset(uStorey + float(gRel)),
                      storeyOffset(uStorey + float(gRel + 1)), ownMask, upMask);
    // Specular, once, for the panel the reflection ray hits. A lobe per light
    // in the loop cost about a fifth of the frame on every level (even matte
    // ones) and gave a blur instead of a reflection. Trace the ray to the
    // ceiling plane, look up the fitting there, and clamp to its rectangle; the
    // highlight is the panel's shape stretched across the floor.
    // 0.10, not 0.005: below that the lobe is about a thousandth of the room
    // light and not visible, but nearly matte levels still paid a shadow march
    // per fragment for it.
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
                // Compute the lobe before tracing visibility. It is nonzero over a
                // small part of the screen and the trace is a full DDA, so testing
                // the cheap part first saves most of the Poolrooms' cost.
                if (lobe > 0.002) light += uLightCol*(lobe*lightVis(sp.xz, shP));
            }
        }
    }
    // Bounce fill, by surface direction. Flat ambient also made everything look
    // flat; real fill light comes mostly off the lit ceiling, so up-facing
    // surfaces get more than down-facing ones, which gives unlit corners shape.
    float hemi = 0.5 + 0.5*N.y;
    vec3 amb = uAmb * mix(vec3(1.05), uLightCol*1.75, hemi) * (0.35+0.65*uBlackout);
    // The level table's ambients were picked against a tone curve with no toe,
    // which returned about 1.25x its input near black; the filmic curve returns
    // about 0.21x there, so the same value is about six times darker and the Red
    // Halls became unreadable. Lift it back, but only in the toe. A flat six-fold
    // multiplier blew the Poolrooms out to white: its ambient is four times any
    // other level's and was never in the toe.
    amb *= 1.0 + 5.5/(1.0 + 40.0*amb);
    light += amb;
    // dim the room lighting around the hunter (lamps and ambient)
    if (uEntDark > 0.01){
        float fd = distance(P.xz, uEntPos.xz);
        light *= mix(1.0, smoothstep(0.8, 6.5, fd), uEntDark);
    }
    // the flashlight and the flare are not dimmed by it
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
// the flashlight are at the same point, so along a view ray the cone term is
// constant and the in-scatter integral reduces to one atan, with no ray march.
//
//   I(t) = cone * 7.5/(1 + 0.10 t^2)  ->  int_0^d = cone * 7.5 * atan(d*k)/k, k = sqrt(0.10)
//
// The flare is off-axis, so its integral is the same shape about the ray's point
// of closest approach to it. Together they give the visible flashlight beam and
// the halo round a burning flare.
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
    // Which storey this fragment is on: the one whose mesh it is, which the
    // renderer sets per chunk (uDrawRel). It used to be worked out from the
    // height, and a sunken floor more than 30 cm down on the current storey was
    // lit as the storey below, with that storey's tubes and shadow grid, so the
    // bottom of every pit came out black.
    if (uStoreyH > 0.0){
        gRel = int(floor(uDrawRel + 0.5));
        gChan = chanFor(gRel);
    }
    vec3 col;
    float aOut = 1.0;
    float dist = distance(fragPos, uViewPos);
    if (fragC.a < 0.62){
        if (fragC.a < 0.1){                          // light panel (emissive, flickers)
            vec2 g = floor((fragPos.xz - uLS*0.5)/uLS + 0.5);
            float st = lightState(g, storeyOffset(uStorey + float(gRel)));
            // The first term lights a dead panel by the room, so it is not black.
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
            // water is always glossy, whatever the level's gloss
            float fres = 0.03 + 0.97*pow(1.0 - abs(dot(V, Nw)), 5.0);
            col = fragC.rgb * (light*0.75 + uAmb*1.4) * (0.85 + 0.15*w1);
            col += light * fres * 0.45;
            aOut = 0.30 + 0.55*fres;                 // near-clear looking down, a mirror at a glance
        }
    } else {
        // Level 0's noclip walls (vertex alpha 250, see noclipCol in
        // world_mesh.cpp): the paper tears sideways in thin horizontal bands that
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
        // World-space variation over the tiled surfaces. Only world maps carry
        // a detail alpha above 0.99 (objects are 128, the neutral map 250), and
        // only opaque textured geometry (vertex alpha 254-255) is a building
        // surface rather than a decal fading out.
        // A texture repeats every 2-3 m, and so do its stains, in a visible grid.
        // One octave of world-space value noise, a couple of metres across,
        // varies the tone to hide it; a second octave cost too much frame time
        // (tools/bench.sh). Arithmetic only: see
        // lightState.
        vec3 Ng = normalize(fragN);
        float world = step(0.99, fragC.a) * step(0.99, detail.a);
        vec2 mp = mix(vec2(fragPos.x + fragPos.z, fragPos.y), fragPos.xz, step(0.7, abs(Ng.y)));
        float wander = vnoise(mp*0.45 + 11.3);
        texel.rgb *= 1.0 + (wander - 0.5) * 2.0 * uMacro * world;
        // Each ceiling board gets its own shade, some are yellowed, and some are
        // water-stained (a tan blot with a dark ring). Per board in world space,
        // hashed with the storey, so no two floors share a stain and none
        // repeats. The T-bar (within 16 mm of a grid line) is left alone.
        if (uBoard > 0.0){
            vec2 bq = fragPos.xz / uBoard;
            vec2 bc = floor(bq), bf = fract(bq);
            float hb = lhash(bc + vec2(storeyOffset(uStorey + float(gRel)) * 3.7, 17.0));
            float edge = min(min(bf.x, 1.0 - bf.x), min(bf.y, 1.0 - bf.y)) * uBoard;
            float onBoard = smoothstep(0.016, 0.024, edge) * step(Ng.y, -0.7) * world;
            vec3 age = mix(vec3(1.0), vec3(1.015, 0.995, 0.93), fract(hb * 7.13));
            vec3 board = age * (1.0 + (hb - 0.5) * 0.07);
            vec2 sc = (bf - vec2(0.3 + 0.4 * fract(hb * 5.3), 0.3 + 0.4 * fract(hb * 9.1))) * uBoard;
            // an irregular blot, dried back in stages: each leak left its own
            // tide line inside the last, the outer one darkest
            float blot = length(sc * vec2(1.0, 1.25)) + vnoise(fragPos.xz * 8.0) * 0.085
                       - (0.07 + 0.12 * fract(hb * 13.3));
            float inside = 1.0 - smoothstep(-0.004, 0.004, blot);
            float ring = exp(-blot * blot * 12000.0) + 0.45 * exp(-(blot + 0.032) * (blot + 0.032) * 16000.0);
            float stained = step(fract(hb * 31.7), 0.05);
            vec3 stain = mix(vec3(1.0), vec3(0.93, 0.88, 0.76), inside * 0.8)
                       * mix(vec3(1.0), vec3(0.74, 0.65, 0.52), min(ring, 1.0) * 0.7);
            board *= mix(vec3(1.0), stain, stained);
            texel.rgb *= mix(vec3(1.0), board, onBoard);
        }
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
        // The revolver reflects the room. With diffuse light only, the stainless
        // steel looked like grey plastic. There is no environment map, so the
        // room is approximated from this fragment's light level (gLightLum, set
        // by roomLight) and the tube and level colours: a dark floor below the
        // horizon, walls at it, the lit ceiling above, and a bright band for the
        // fittings overhead that sharpens with gloss. Roughness blurs a metal's
        // reflection but does not dim it, so metal reflects at full weight and
        // keeps a fifth of its diffuse; the walnut gets 4% at normal incidence,
        // rising with gloss at a grazing angle. Object maps carry metalness in
        // detail alpha (128 = none, 188 = metal); the importer stored metal
        // albedo at 0.35 of its value, hence the 2.6.
        col = texel.rgb * fragC.rgb * roomLight(fragPos, Nb);
        // Backlit print (vertex alpha 240): a vending machine's header, display,
        // price strips and the cabinet behind its glass, lit from inside by its
        // own tubes as well as by the room, so it glows in a blackout. A band on
        // the alpha rather than a branch: arithmetic only, see lightState.
        float backlit = step(0.93, fragC.a) * step(fragC.a, 0.955);
        col += texel.rgb * fragC.rgb * (0.62 * backlit);
        gLightLum = max(gLightLum, 0.8 * backlit);   // the fog takes its brightness from this
        // Only the held revolver has metal, so only its draw pays for this: a
        // uniform branch, the same for every fragment of a draw — not the
        // divergent kind (inside, it stays arithmetic).
        if (uObjRefl > 0.5){
            float isObj = step(detail.a, 0.75);
            float metal = clamp((detail.a - 0.502) / 0.235, 0.0, 1.0) * isObj;
            col *= 1.0 - 0.8 * metal;
            vec3 Vo = normalize(uViewPos - fragPos);
            vec3 Ro = reflect(-Vo, Nb);
            float room = max(gLightLum, 0.04);
            // floor and walls in the level's own colour (its fog is its air, and
            // its air is the colour of its walls: mustard, concrete, tile, red),
            // the ceiling and its tubes in the light's
            vec3 walls = uFogCol / max(dot(uFogCol, vec3(0.30, 0.59, 0.11)), 1e-3);
            vec3 below = mix(walls * 0.22, walls * 0.95, smoothstep(-0.6, 0.05, Ro.y));
            vec3 env = room * (mix(below, uLightCol * 1.15, smoothstep(0.05, 0.7, Ro.y))
                     + uLightCol * pow(max(Ro.y, 0.0), mix(3.0, 26.0, gGloss)) * (0.6 + 1.6 * gGloss));
            vec3 F0 = mix(vec3(0.04), min(texel.rgb * fragC.rgb * 2.6, vec3(0.95)), metal);
            float fr = pow(1.0 - clamp(dot(Nb, Vo), 0.0, 1.0), 5.0);
            vec3 F = F0 + (1.0 - F0) * fr * gGloss;
            col += env * F * mix(0.25 + 0.75 * gGloss, 1.0, metal) * isObj;
        }
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
        aOut = max(fragC.a * texel.a, backlit);      // translucent contact shadows + scrawl decals
        if (noclip){
            // The torn bands show a flat bright colour on an ordinary exit and
            // dark red on a cursed one (vertex alpha 247). A faint shimmer runs
            // all the time, so a player who stops and looks can spot the exit.
            float shimmer = 0.035 * sin(uTime*23.0 + fragPos.y*41.0 + fragPos.x*7.0 + fragPos.z*7.0);
            vec3 behind = fragC.a < 0.975 ? vec3(0.55, 0.03, 0.02) : vec3(1.25, 1.18, 0.95);   // 247 cursed, 250 not
            col = mix(col * (1.0 + shimmer), behind * (0.6 + 0.4*gLightLum), tear * 0.55);
            aOut = 1.0;
        }
    }
    float f = clamp(exp(-dist*uFogDen), 0.0, 1.0);
    // Fog used to be one flat colour, so the far end of a dark corridor glowed
    // as much as a lit one. Its brightness now follows the light level of the
    // surface behind it.
    vec3 fogc = uFogCol * mix(0.10, 1.0, uBlackout) * (0.45 + 1.15*clamp(gLightLum, 0.0, 1.0));
    col = mix(fogc, col, f);
    // the beam and the flare light the air on the way here, not just the surface
    vec3 rd = (fragPos - uViewPos) / max(dist, 1e-4);
    // Scaled by the level's own fog density, because the air is what does the
    // scattering: a level with clear air has no visible beam. The
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
uniform float uMigraine;   // Level 0's hum headache, 0..1 (Sim::migraine)
out vec4 finalColor;
float hh(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
const mat3 TO_YIQ = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
const mat3 TO_RGB = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
void main(){
    vec2 uv = fragTexCoord;
    // The eye sees as the camcorder records, more lightly: TAPE_FS's line
    // wobble, smeared colour, lifted blacks, scanlines and dropouts at about
    // half strength, always on. Fear pushes it toward the tape. q is the
    // screen before the water or the migraine moves the picture.
    vec2 q = fragTexCoord;
    vec2 res = vec2(textureSize(texture0, 0));
    float down = 1.0 - q.y;                       // 0 at the top of the picture
    float line = floor(down * 240.0);
    // A faint tracking band rolling down the picture.
    float band = fract(down*0.7 - uTime*(0.05 + 0.10*uFear));
    float roll = smoothstep(0.0, 0.04, band) * (1.0 - smoothstep(0.04, 0.12, band));
    // Head switching: the bottom lines of the picture are skewed a little.
    float hs = 1.0 - smoothstep(0.0, 0.012, q.y);
    uv.x += (hh(vec2(line, floor(uTime*30.0))) - 0.5) * (0.0005 + 0.0025*uFear + 0.002*roll)
          + hs * (0.004 + 0.010*hh(vec2(line, floor(uTime*60.0))));
    if (uWater > 0.0) {
        uv += uWater*0.0018*vec2(sin(uv.y*24.0+uTime*1.3),sin(uv.x*21.0-uTime));
        uv = clamp(uv,vec2(0.002),vec2(0.998));
    }
    // The migraine: a slow double pulse that zooms the image in slightly and
    // splits its colour.
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
    // Luma softened a little, chroma smeared to the right, as on tape but
    // over half the width.
    float dx = 1.0 / 640.0;
    float Y = dot(0.15*texture(texture0, uv - vec2(dx, 0.0)).rgb + 0.70*c
                + 0.15*texture(texture0, uv + vec2(dx, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    vec2 iq = vec2(0.0);
    for (int i = 0; i < 4; i++) iq += (TO_YIQ * texture(texture0, uv + vec2(dx*(1.5 + 2.2*float(i)), 0.0)).rgb).yz;
    iq = mix((TO_YIQ * c).yz, iq*0.25, 0.75) * 0.94;
    c = max(TO_RGB * vec3(Y, iq), 0.0);
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
    // Tape noise in two-pixel blocks, heavier in the tracking band.
    float n = hh(floor(q*res/2.0) + fract(uTime*vec2(13.17, 7.31))*311.0);
    c += (n - 0.5) * (0.024 + 0.055*uFear + 0.05*roll);
    float d = length(dir);
    c *= 1.0 - smoothstep(0.34, 0.95, d)*(0.42 + 0.34*uFear + 0.30*throb); // vignette
    c *= 1.0 - 0.05*throb;                               // the frame dims on the pulse
    c *= 0.994 + 0.006*sin(uTime*377.0);             // mains-hum luma shimmer
    c = mix(c,c*vec3(0.48,0.86,0.80)+vec3(0.015,0.07,0.065),uWater*0.75);
    // Blacks never quite black, whites never quite white.
    c = 0.02 + c*0.96;
    // Dropouts, rarer and fainter than the tape's.
    float field = floor(uTime*24.0);
    float drop = step(0.9994 - 0.004*uFear, hh(vec2(line, field + 7.0)));
    float span = 1.0 - smoothstep(0.0, 0.02 + 0.06*hh(vec2(line, field + 9.0)), abs(q.x - hh(vec2(line, field + 3.0))));
    c = mix(c, vec3(0.85), drop*span*0.45);
    c *= 0.965 + 0.035*sin(down*res.y*3.14159*0.5);   // scanlines, one every other pixel row
    finalColor = vec4(c, 1.0);
}
)GLSL";

// The camcorder's tape played back off the reel on the death card.
// Coordinates come from the fragment, not the quad; the next frame is cross-faded in by uMix, which is how 15 frames a second play
// back without stepping.
const char *TAPE_FS = GLSL_VERSION_HEADER R"GLSL(
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform float uTime;
uniform vec2 uRes;               // framebuffer, px
uniform vec4 uCellA, uCellB;     // frames as uv: min.xy, max.zw, v up the picture
uniform float uMix;
uniform float uTrack;            // interference: 0 clean .. 1 torn
uniform float uStill;            // a paused frame, which the heads cannot hold still
uniform float uSnow;             // no picture: only noise
out vec4 finalColor;
float hh(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
vec3 frameAt(vec2 q){
    q = clamp(q, vec2(0.002), vec2(0.998));
    vec3 a = texture(texture0, mix(uCellA.xy, uCellA.zw, q)).rgb;
    vec3 b = texture(texture0, mix(uCellB.xy, uCellB.zw, q)).rgb;
    return mix(a, b, uMix);
}
const mat3 TO_YIQ = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
const mat3 TO_RGB = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
void main(){
    vec2 q = gl_FragCoord.xy / uRes;             // 0..1, bottom-left, as the frames are stored
    float t = uTime;
    float down = 1.0 - q.y;                      // 0 at the top of the picture
    float line = floor(down * 240.0);            // a field has about 240 lines
    q.y += uStill * (hh(vec2(floor(t*18.0), 3.1)) - 0.5) * 0.008;
    // Line timing wanders a little always, and a lot under interference.
    float jit = (hh(vec2(line, floor(t*30.0))) - 0.5) * (0.0012 + 0.010*uTrack*uTrack);
    // A tracking band rolling down the picture.
    float band = fract(down*0.7 - t*(0.12 + 0.30*uTrack));
    float tear = smoothstep(0.0, 0.05, band) * (1.0 - smoothstep(0.05, 0.14, band)) * smoothstep(0.15, 0.6, uTrack);
    jit += tear * (0.02 + 0.06*hh(vec2(line, floor(t*24.0))));
    // Head switching: the bottom few lines of every field are skewed.
    float hs = 1.0 - smoothstep(0.0, 0.022, q.y);
    jit += hs * (0.012 + 0.03*hh(vec2(line, floor(t*60.0))));
    q.x += jit;
    // Luma sharp, chroma smeared to the right: tape keeps colour at a fraction
    // of the bandwidth it keeps brightness.
    float dx = 1.0 / 320.0;
    vec3 c0 = frameAt(q);
    float Y = dot(0.22*frameAt(q - vec2(dx, 0.0)) + 0.56*c0 + 0.22*frameAt(q + vec2(dx, 0.0)), vec3(0.299, 0.587, 0.114));
    vec2 iq = vec2(0.0);
    for (int i = 0; i < 4; i++) iq += (TO_YIQ * frameAt(q + vec2(dx*(1.5 + 2.2*float(i)), 0.0))).yz;
    iq *= 0.25 * 0.88;
    vec3 c = TO_RGB * vec3(Y, iq);
    // A tape's blacks are never black, and its whites are never quite white.
    c = 0.035 + c*0.94;
    float n = hh(floor(gl_FragCoord.xy/2.0) + fract(t*vec2(13.17, 7.31))*311.0);
    c += (n - 0.5) * (0.045 + 0.16*uTrack + 0.10*tear);
    // Dropouts: a scrap of a line where the oxide is gone, flaring white.
    float field = floor(t*24.0);
    float drop = step(0.9982 - 0.010*uTrack, hh(vec2(line, field + 7.0)));
    float span = 1.0 - smoothstep(0.0, 0.03 + 0.09*hh(vec2(line, field + 9.0)), abs(q.x - hh(vec2(line, field + 3.0))));
    c = mix(c, vec3(0.88), drop*span*0.75);
    c = mix(c, vec3(0.15 + 0.75*n), uSnow);
    c *= 0.93 + 0.07*sin(down*uRes.y*3.14159*0.5);   // scanlines, one every other pixel row
    vec2 d = q - 0.5;
    c *= 1.0 - smoothstep(0.30, 0.85, length(d*vec2(1.0, 0.8))) * 0.45;
    finalColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";

// A float as GLSL source: the shortest decimal that reads back as the same
// float, with a decimal point, because GLSL ES does not convert an int
// literal to float. %g at too few digits rounds 10 to 1e+01, which reads back
// as the same float and is legal GLSL but not how anyone would write it, so a
// digit count that needs an exponent does not count as shortest.
static std::string glslFloat(float v) {
    char buf[32];
    for (int digits = 1; digits <= 9; digits++) {
        snprintf(buf, sizeof buf, "%.*g", digits, v);
        if (strtof(buf, nullptr) == v && (!strchr(buf, 'e') || digits == 9)) break;
    }
    std::string out = buf;
    if (out.find_first_of(".e") == std::string::npos) out += ".0";
    return out;
}

std::string worldFragmentShader() {
    std::string src = WORLD_FS_TEMPLATE;
    const struct { const char *token; float value; } numbers[] = {
        { "@PANEL_HALF@", PANEL_HALF },
        { "@STOREY_HASH_STEP@", STOREY_HASH_STEP },
        { "@STOREY_HASH_SPAN@", STOREY_HASH_SPAN },
        { "@TUBE_HASH_X@", TUBE_HASH_X },
        { "@TUBE_HASH_Z@", TUBE_HASH_Z },
        { "@TUBE_HASH_SCALE@", TUBE_HASH_SCALE },
        { "@TUBE_STOREY_SKEW@", TUBE_STOREY_SKEW },
        { "@TUBE_OUTPUT_SPREAD@", TUBE_OUTPUT_SPREAD },
        { "@TUBE_STUTTER_SPREAD@", TUBE_STUTTER_SPREAD },
        { "@TUBE_STUTTER_RATE@", TUBE_STUTTER_RATE },
        { "@TUBE_STUTTER_PHASE@", TUBE_STUTTER_PHASE },
        { "@TUBE_STUTTER_HASH@", TUBE_STUTTER_HASH },
        { "@TUBE_STUTTER_OPEN@", TUBE_STUTTER_OPEN },
        { "@TUBE_STUTTER_DIP@", TUBE_STUTTER_DIP },
        { "@TUBE_BUZZ_BASE@", TUBE_BUZZ_BASE },
        { "@TUBE_BUZZ_SPREAD@", TUBE_BUZZ_SPREAD },
        { "@TUBE_BUZZ_PHASE@", TUBE_BUZZ_PHASE },
        { "@TUBE_BUZZ_DUTY@", TUBE_BUZZ_DUTY },
        { "@PILLAR_LO@", PILLAR_LO },
        { "@PILLAR_HI@", PILLAR_HI },
    };
    // The occupancy grid's bits (World::buildOccupancy), as GLSL ints.
    const struct { const char *token; int value; } bits[] = {
        { "@OCC_WALL_N@", OCC_WALL_N },
        { "@OCC_WALL_W@", OCC_WALL_W },
        { "@OCC_PILLAR@", OCC_PILLAR },
        { "@OCC_OPEN_ABOVE@", OCC_OPEN_ABOVE },
        { "@OCC_NEAR_OPENING@", OCC_NEAR_OPENING },
    };
    // A number can be written in more than once: the stutter's hashes scale by
    // the tube hash's TUBE_HASH_SCALE.
    auto put = [&](const std::string &token, const std::string &value) {
        for (size_t at = src.find(token); at != std::string::npos; at = src.find(token, at + value.size()))
            src.replace(at, token.size(), value);
    };
    for (const auto &n : numbers) put(n.token, glslFloat(n.value));
    for (const auto &b : bits) put(b.token, std::to_string(b.value));
    return src;
}
