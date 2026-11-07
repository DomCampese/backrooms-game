#include "../core/fp_strict.h"
#include "draw.h"
#include "../core/hash.h"
#include "../sim/sim_math.h"
#include "levels.h"
#include "palette.h"
#include "scene.h"
#include "sheets.h"
#include "sprites.h"
#include <cmath>

namespace {

// raylib's PI, which its shapes turn by.
constexpr float PI_F = 3.14159265358979323846f;

// One flare's current intensity: a fast ramp at ignition, a fade over the last
// 1.5 s, and flicker. The point light and the halo spheres both read it, so
// they dim together.
float flareGlow(const FlareProj &f, float flick) {
    return clampf((Sim::FLAREBURN - f.burn) * 6.0f, 0, 1) * clampf(f.burn / Sim::FLAREFADE, 0, 1) * flick;
}

float dist(Vec3 a, Vec3 b) {
    Vec3 d = sub(a, b);
    return sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
}

Rgba lit(Rgba c, float f) { return { cl8(c.r * f), cl8(c.g * f), cl8(c.b * f), c.a }; }

// What an auditorium's screen shows at time t: the colour of the current shot
// (a few seconds each, hashed from the auditorium's seed) and how bright it is,
// with the slight flicker of a projector. One shot in nine is black, as
// between scenes. The screen and the light it throws both use this.
struct FilmFrame { Vec3 col; float level; };
FilmFrame filmFrame(uint32_t seed, float t) {
    static const Vec3 SHOTS[6] = { { 0.55f, 0.66f, 0.95f }, { 1.0f, 0.78f, 0.52f }, { 0.86f, 0.88f, 0.90f },
                                   { 0.60f, 0.86f, 0.66f }, { 0.95f, 0.42f, 0.36f }, { 0.78f, 0.74f, 0.96f } };
    const float SHOT = 3.4f;
    float phase = (seed & 1023) / 1023.0f * SHOT;
    int shot = (int)floorf((t + phase) / SHOT);
    uint32_t h = ih(shot, (int)(seed >> 10), seed ^ 0xF117u);
    FilmFrame f;
    f.col = SHOTS[h % 6];
    f.level = (h >> 3) % 9 == 0 ? 0.04f : 0.45f + ((h >> 6) & 255) / 255.0f * 0.55f;
    f.level *= 0.96f + 0.04f * sinf(t * TAU * 24.0f) * sinf(t * 7.1f + (float)(seed & 63));
    return f;
}

// The auditorium whose tubes are masked and whose screen lights the room: the
// one the player stands in, else the one whose floor is nearest.
const Auditorium *nearestAuditorium(const std::vector<Auditorium> &auds, float px, float pz) {
    const Auditorium *best = nullptr;
    float bestD = 1e30f;
    for (const Auditorium &a : auds) {
        float dx = fmaxf(fmaxf(a.rect[0] - px, px - a.rect[2]), 0.0f);
        float dz = fmaxf(fmaxf(a.rect[1] - pz, pz - a.rect[3]), 0.0f);
        float d = dx * dx + dz * dz;
        if (d < bestD) { bestD = d; best = &a; }
    }
    return best;
}

// raylib's MatrixRotateY, MatrixRotateX and MatrixTranslate, and its
// MatrixMultiply(a, b): a first, then b.
Xform rotY(float a) {
    Xform m;
    float c = cosf(a), s = sinf(a);
    m.x = { c, 0, -s };
    m.z = { s, 0, c };
    return m;
}
Xform rotX(float a) {
    Xform m;
    float c = cosf(a), s = sinf(a);
    m.y = { 0, c, s };
    m.z = { 0, -s, c };
    return m;
}
Xform translate(float x, float y, float z) {
    Xform m;
    m.t = { x, y, z };
    return m;
}
Vec3 apply3(const Xform &m, Vec3 v) { return add(add(scale(m.x, v.x), scale(m.y, v.y)), scale(m.z, v.z)); }
Xform then(const Xform &a, const Xform &b) {
    Xform r;
    r.x = apply3(b, a.x);
    r.y = apply3(b, a.y);
    r.z = apply3(b, a.z);
    r.t = add(apply3(b, a.t), b.t);
    return r;
}

Shape cube(Vec3 c, float w, float h, float l, Rgba col) {
    Shape s{ ShapeKind::Cube };
    s.a = c; s.size = { w, h, l }; s.colour = col;
    return s;
}
Shape sphere(Vec3 c, float r, Rgba col) {
    Shape s{ ShapeKind::Sphere };
    s.a = c; s.r0 = r; s.colour = col;
    return s;
}
Shape cylinder(Vec3 base, float rTop, float rBottom, float h, int sides, Rgba col) {
    Shape s{ ShapeKind::Cylinder };
    s.a = base; s.r0 = rTop; s.r1 = rBottom; s.h = h; s.sides = sides; s.colour = col;
    return s;
}
Shape cylinderEx(Vec3 a, Vec3 b, float r0, float r1, int sides, Rgba col) {
    Shape s{ ShapeKind::CylinderEx };
    s.a = a; s.b = b; s.r0 = r0; s.r1 = r1; s.sides = sides; s.colour = col;
    return s;
}
// DrawBillboardRec.
Shape billboard(ShapeTex t, float sx, float sy, float sw, float sh, Vec3 c, float w, float h, Rgba col) {
    Shape s{ ShapeKind::Billboard };
    s.tex = t; s.src[0] = sx; s.src[1] = sy; s.src[2] = sw; s.src[3] = sh;
    s.a = c; s.size = { w, h, 0 }; s.colour = col;
    return s;
}
// DrawBillboard: the whole texture, `size` tall and as wide as its aspect.
Shape billboardWhole(ShapeTex t, Vec3 c, float size, Rgba col) {
    int w, h;
    shapeTexSize(t, w, h);
    return billboard(t, 0, 0, (float)w, (float)h, c, size * fabsf((float)w / (float)h), size, col);
}

void put(std::vector<SceneDraw> &out, const Shape &s) { out.push_back({ false, {}, s }); }
void put(std::vector<SceneDraw> &out, ObjMesh m, const Xform &xf, float tint = 1.0f) {
    SceneDraw d{ true, {}, {} };
    d.mesh.mesh = m; d.mesh.xf = xf; d.mesh.tint = tint;
    out.push_back(d);
}

// Where a sprite's frame sits on its sheet, pixels.
void spriteSrc(const ActorSprite &sp, int frame, float src[4]) {
    src[0] = (float)frame * ENT_FRAME_W; src[1] = (float)sp.row * ENT_FRAME_H;
    src[2] = (float)ENT_FRAME_W; src[3] = (float)ENT_FRAME_H;
}
void spriteDraw(std::vector<SceneDraw> &out, ShapeTex t, const ActorSprite &sp, int frame, Rgba col) {
    float src[4];
    spriteSrc(sp, frame, src);
    put(out, billboard(t, src[0], src[1], src[2], src[3], sp.centre, sp.w, sp.h, col));
}

// One tape player: the body, the two turning reels, and the record lamp while
// the tape runs. The lamp pulses, because a steady red dot is easy to miss.
void deckDraws(const Sim &sim, const Xform &xf, bool lamp, float now, std::vector<SceneDraw> &out) {
    put(out, ObjMesh::Deck, xf);
    for (int i = 0; i < 2; i++) {
        // both hubs turn the same way
        Xform r = then(rotY(sim.deck.reel), translate(i ? 0.024f : -0.024f, 0.0505f, 0.0f));
        put(out, ObjMesh::Reel, then(r, xf));
    }
    if (lamp) {
        float pu = 0.45f + 0.55f * (0.5f + 0.5f * sinf(now * 4.2f));
        put(out, ObjMesh::DeckLamp, xf, cl8(255 * pu) / 255.0f);
    }
}

// A loose revolver hangs at chest height and turns, so it reads from across a
// room as something placed, not dropped.
constexpr float REVOLVER_HOVER = 0.95f;       // m above the floor, the model's middle
constexpr float REVOLVER_SHOW_SCALE = 1.3f;   // larger than life, to read at a distance
// The middle of the model's bind pose (revolver.glb's "gun" bounds), in its frame.
constexpr Vec3 REVOLVER_MIDDLE = { 0.0f, -0.0127f, 0.0648f };

float revolverBob(const SceneItem &it, float now) { return sinf(now * 1.7f + it.gi * 1.3f + it.gk) * 0.05f; }

// The gold halo, a column of light and a pool on the floor under it, and
// motes rising through it: drawn unlit, so the find shows even in a blackout.
// The revolver and the casino's cigarettes both have it.
void findGlowDraws(const SceneItem &it, float now, std::vector<SceneDraw> &out) {
    const Vec3 base = it.pos;
    const Vec3 mid = { base.x, base.y + REVOLVER_HOVER + revolverBob(it, now), base.z };
    const float pulse = 0.8f + 0.2f * sinf(now * 2.6f + it.gi);
    Shape halo = billboardWhole(ShapeTex::Particle, mid, 0.95f * pulse, { 255, 196, 80, cl8(120 * pulse) });
    Shape core = billboardWhole(ShapeTex::Particle, mid, 0.42f, { 255, 236, 170, 150 });
    halo.additive = core.additive = true;
    halo.depthWrite = core.depthWrite = false;
    put(out, halo);
    put(out, core);
    Shape column = cylinderEx({ base.x, base.y + 0.01f, base.z }, { base.x, base.y + 2.3f, base.z },
                              0.36f, 0.05f, 16, { 255, 205, 110, cl8(20 * pulse) });
    column.depthWrite = false;
    put(out, column);
    Shape pool = cylinder({ base.x, base.y + 0.006f, base.z }, 0.55f, 0.55f, 0.004f, 20, { 255, 200, 96, cl8(70 * pulse) });
    pool.depthWrite = false;
    put(out, pool);
    for (int i = 0; i < 6; i++) {
        float phase = fmodf(now * 0.35f + i / 6.0f, 1.0f);   // each rises from the floor and fades out above the gun
        float ang = i * 1.047f + now * 0.6f, r = 0.22f + 0.08f * sinf(i * 2.3f);
        Vec3 p = { base.x + cosf(ang) * r, base.y + 0.1f + phase * 1.9f, base.z + sinf(ang) * r };
        Shape mote = billboardWhole(ShapeTex::Particle, p, 0.07f, { 255, 222, 140, cl8(200 * sinf(phase * PI_F)) });
        mote.additive = true;
        mote.depthWrite = false;
        put(out, mote);
    }
}

// The casino's pack of cigarettes, gold and larger than life, hanging where
// a loose revolver would: unlit like its glow, so it reads across the floor.
// Three cigarettes stand out of the open top, and the pack rocks as it bobs.
void cigarettePackDraws(const SceneItem &it, float now, std::vector<SceneDraw> &out) {
    const float s = 1.5f;   // to read at a distance
    const Vec3 mid = { it.pos.x, it.pos.y + REVOLVER_HOVER + revolverBob(it, now), it.pos.z };
    const float w = 0.055f * s, h = 0.085f * s;   // half width and half height
    const float rock = sinf(now * 1.3f + it.gi) * 0.12f;          // tilt of the pack's long axis
    const Vec3 up = { sinf(rock) * 0.6f, cosf(rock), sinf(rock) * 0.8f };
    const Vec3 lo = sub(mid, scale(up, h)), hi = add(mid, scale(up, h));
    put(out, cylinderEx(lo, hi, w * 1.41f, w * 1.41f, 4, { 232, 186, 70, 255 }));            // the gold card
    put(out, cylinderEx(add(lo, scale(up, h * 1.15f)), add(lo, scale(up, h * 1.35f)),
                        w * 1.43f, w * 1.43f, 4, { 120, 22, 30, 255 }));                       // its red band
    put(out, cylinderEx(sub(hi, scale(up, 0.02f * s)), add(hi, scale(up, 0.002f)),
                        w * 1.44f, w * 1.44f, 4, { 250, 236, 200, 255 }));                     // the foil at the top
    for (int c = 0; c < 3; c++) {
        const Vec3 base = add(hi, { (c - 1) * 0.022f * s * cosf(rock), 0, (c - 1) * 0.005f * s });
        const Vec3 tip = add(base, scale(up, (0.03f + 0.012f * c) * s));
        put(out, cylinderEx(base, tip, 0.0055f * s, 0.0055f * s, 8, { 214, 140, 70, 255 }));   // filter
    }
}

}   // namespace

bool looseRevolver(Sim &sim, float now, Xform &xf) {
    if (sim.hasRevolver) return false;
    const int ci = cellOf(sim.px), ck = cellOf(sim.pz), reach = SCENE_PICKUP_CELLS;
    float nearest = 1e30f;
    SceneItem found{ SceneKind::Revolver };
    for (int dx = -reach; dx <= reach; dx++)
        for (int dz = -reach; dz <= reach; dz++) {
            int a = ci + dx, b = ck + dz;
            if (!sim.revolverAt(a, b) || sim.pickupAt(a, b) != Pickup::Revolver) continue;
            if (sim.taken.count(sim.cellKey(a, b))) continue;
            float d = (float)(dx * dx + dz * dz);
            if (d >= nearest) continue;
            nearest = d;
            Vec2 spot = sim.pickupSpot(a, b);
            found.pos = { spot.x, sim.world.floorY(a, b), spot.y };
            found.gi = a, found.gk = b;
        }
    if (nearest > 1e29f) return false;
    const float spin = now * 1.2f + found.gi;
    const Vec3 forward = { cosf(spin), 0, sinf(spin) }, up = { 0, 1, 0 };
    const Vec3 right = cross(forward, up);
    xf.x = scale(right, REVOLVER_SHOW_SCALE);
    xf.y = scale(up, REVOLVER_SHOW_SCALE);
    xf.z = scale(forward, REVOLVER_SHOW_SCALE);
    const Vec3 mid = { found.pos.x, found.pos.y + REVOLVER_HOVER + revolverBob(found, now), found.pos.z };
    xf.t = sub(mid, apply3(xf, REVOLVER_MIDDLE));
    return true;
}

Xform xformOf(const HeldFrame &f) {
    Xform m;
    m.x = scale(f.right, f.scale);
    m.y = scale(f.up, f.scale);
    m.z = scale(f.forward, f.scale);
    m.t = f.pos;
    return m;
}

ObjMat objMat(ObjMesh m) {
    switch (m) {
    case ObjMesh::Can: return ObjMat::Can;
    case ObjMesh::Deck: case ObjMesh::Reel: case ObjMesh::DeckLamp: return ObjMat::Deck;
    default: return ObjMat::Props;
    }
}

float objGloss(ObjMesh m) { return m == ObjMesh::Flare ? FLARE_GLOSS : -1.0f; }

FrameLight frameLight(Sim &sim, float now) {
    FrameLight l{};
    l.time = now;
    l.blackout = sim.blackoutCur * (sim.whisperT > 0 ? 0.86f : 1.0f);   // lights dim during a whisper
    l.flash = sim.flashCur;
    if (sim.flashOn && sim.battery < 0.15f) {   // low battery: flicker
        float lowFlick = 0.55f + 0.45f * sinf(now * 19.0f) * sinf(now * 31.0f + 1.7f);
        l.flash *= clampf(sim.battery / 0.15f, 0.15f, 1.0f) * (0.7f + 0.3f * lowFlick);
    }
    l.flashDir = sim.fwd;
    l.flick = 0.91f + 0.09f * sinf(now * 31.0f) * sinf(now * 47.3f + 1.3f);
    // One flare point light: the flare with the most presence at the player
    // (Sim::dominantFlare), so a nearly burnt-out flare does not keep the light
    // from a fresh one.
    const FlareProj *lead = sim.dominantFlare(sim.px, sim.pz);
    l.flareInt = lead ? flareGlow(*lead, l.flick) : 0.0f;
    l.flarePos = lead ? Vec3{ lead->x, lead->y + 0.06f, lead->z } : Vec3{ sim.px, sim.eyeY, sim.pz };
    if (!lead && sim.muzzleT > 0) {   // the muzzle flash uses the flare light
        l.flareInt = sim.muzzleT / Sim::MUZZLE_FLASH * 1.3f;
        l.flarePos = { sim.px + sim.f2x * 0.6f, sim.eyeY - 0.05f, sim.pz + sim.f2z * 0.6f };
    }
    // the hunter dims lights around him and casts a shadow
    l.entPos = { sim.ent.x, sim.ent.dispY + 1.0f, sim.ent.z };
    l.entDark = (sim.ent.st == EState::Hidden) ? 0.0f : sim.entDarkCur;
    l.entBlock = (sim.ent.st == EState::Hidden || sim.ent.st == EState::Die) ? 0.0f : 1.0f;
    // The nearest enclosed stairwell's landing light, in this storey's frame:
    // the one rising from here, or the one arriving here, whose landing is a
    // storey down. The ceiling grid has no fitting over an opening, so without
    // this a stair shaft is dark.
    const int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
    Vec3 wellLamp = { 0, 0, 0 };
    float wellD2 = 1e30f;
    for (const LandingLight &well : sim.world.landingLights(pcx, pcz)) {
        const Vec3 &lp = well.lamp;
        float d2 = (lp.x - sim.px) * (lp.x - sim.px) + (lp.y - sim.eyeY) * (lp.y - sim.eyeY) + (lp.z - sim.pz) * (lp.z - sim.pz);
        if (d2 < wellD2) { wellD2 = d2; wellLamp = lp; }
    }
    // The Manila Room, when one is near: its tubes off, its chandelier on. An
    // inverted rectangle masks nothing. The chandelier ignores blackouts and
    // flickers smoothly rather than stuttering like a tube.
    const float noMask[4] = { 1e6f, 1e6f, -1e6f, -1e6f };
    for (int i = 0; i < 4; i++) { l.roomMask[i] = noMask[i]; l.lamp[i] = 0; }
    l.lampCol = { 1.0f, 0.70f, 0.42f };
    float manD2 = sim.manilaNear ? (sim.manilaX - sim.px) * (sim.manilaX - sim.px) + (sim.manilaZ - sim.pz) * (sim.manilaZ - sim.pz) : 1e30f;
    if (sim.manilaNear) {
        l.roomMask[0] = sim.manilaX - 4.0f; l.roomMask[1] = sim.manilaZ - 4.0f;
        l.roomMask[2] = sim.manilaX + 4.0f; l.roomMask[3] = sim.manilaZ + 4.0f;
    }
    if (sim.manilaNear && manD2 <= wellD2) {
        float waver = 0.96f + 0.04f * sinf(now * 7.3f) * sinf(now * 2.9f + 1.1f);
        l.lamp[0] = sim.manilaX; l.lamp[1] = sim.world.ceilY(cellOf(sim.manilaX), cellOf(sim.manilaZ)) - 0.80f;
        l.lamp[2] = sim.manilaZ; l.lamp[3] = 1.0f * waver;
    } else if (wellD2 < 22.0f * 22.0f) {
        // Like any tube: it goes out in a blackout and stutters.
        l.lamp[0] = wellLamp.x; l.lamp[1] = wellLamp.y; l.lamp[2] = wellLamp.z;
        l.lamp[3] = 0.95f * sim.blackoutCur * l.flick;
        l.lampCol = { 0.95f, 0.93f, 0.80f };
    }
    // The theater: the nearest auditorium's tubes are masked, and its screen
    // lights the front of the room in the colour of the film. The shader has one
    // mask, so only that auditorium is dark; the others are seen only through
    // their doors. A blackout cuts the projector too.
    if (sim.level == THEATER) {
        std::vector<Auditorium> auds = sim.world.auditoriums(pcx, pcz);
        if (const Auditorium *a = nearestAuditorium(auds, sim.px, sim.pz)) {
            for (int i = 0; i < 4; i++) l.roomMask[i] = a->rect[i];
            FilmFrame film = filmFrame(a->seed, now);
            l.lamp[0] = a->lamp.x; l.lamp[1] = a->lamp.y; l.lamp[2] = a->lamp.z;
            l.lamp[3] = 2.6f * film.level * sim.blackoutCur;
            l.lampCol = film.col;
        }
    }
    setLightExtrasCPU(l.roomMask, l.lamp);
    return l;
}

void sceneDraws(Sim &sim, const FrameLight &light, float now, std::vector<SceneDraw> &out) {
    const int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
    const int pci = cellOf(sim.px), pck = cellOf(sim.pz);
    const float flick = light.flick, flareInt = light.flareInt;
    const Vec3 flarePos = light.flarePos;
    // Stairwell landing lights: the tube on each nearby batten, at the current
    // tube brightness. Drawn here, not in the chunk mesh, so it can go out.
    for (const LandingLight &well : sim.world.landingLights(pcx, pcz)) {
        unsigned char g8 = cl8(60 + 195 * sim.blackoutCur * flick);
        put(out, cube(well.tube, well.alongX ? 0.96f : 0.035f, 0.035f, well.alongX ? 0.035f : 0.96f, { g8, g8, cl8(g8 * 0.92f), 255 }));
    }
    // The theater's screens, showing a film, and the projector's beam through
    // the haze from the port in the back wall: translucent, writing no depth.
    for (const Auditorium &a : sim.world.auditoriums(pcx, pcz)) {
        FilmFrame film = filmFrame(a.seed, now);
        float lv = film.level * sim.blackoutCur;
        Rgba sc = { cl8(255 * film.col.x * lv), cl8(255 * film.col.y * lv), cl8(255 * film.col.z * lv), 255 };
        Vec3 c = add(a.screen, scale(a.normal, 0.01f));
        float w = a.halfW * 2.0f, h = a.y1 - a.y0;
        put(out, cube(c, a.across.x != 0.0f ? w : 0.02f, h, a.across.z != 0.0f ? w : 0.02f, sc));
        Shape beam = cylinderEx(a.booth, add(a.screen, scale(a.normal, 0.3f)), 0.12f, h * 0.48f, 16,
                                { sc.r, sc.g, sc.b, cl8(11 * lv) });
        beam.depthWrite = false;
        if (lv > 0.02f) put(out, beam);
    }
    // Small objects draw unlit, so the room light at each is estimated on the
    // CPU (plus the flare or muzzle light); otherwise balloons would stay lit
    // in a blackout.
    const LevelCfg &lc = LEVELS[sim.level];
    float ambLumP = (lc.amb.x + lc.amb.y + lc.amb.z) / 3.0f;
    float edSend = (sim.ent.st == EState::Hidden) ? 0.0f : sim.entDarkCur;
    auto propLum = [&](float x, float y, float z) {
        float lum = lightAtCPU(x, y, z, sim.blackoutCur, lc.ls, lc.wallH - LIGHT_DROP, lc.dead, lc.lightMul, ambLumP,
                               sim.ent.x, sim.ent.z, edSend, lc.vary);
        if (flareInt > 0.01f) {
            float fx = x - flarePos.x, fy = y - flarePos.y, fz = z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fx * fx + fy * fy + fz * fz)), 0.0f, 1.0f);
        }
        lum = clampf(lum, 0.0f, 1.0f);
        return 0.18f + 0.82f * lum;   // minimum brightness
    };
    // Loose objects, placed by the same rules the sim uses (simScene); only the
    // look, the bob and the sway are decided here.
    struct MarkSunken {   // flags every draw this item adds, on any way out of the loop body
        std::vector<SceneDraw> &o; size_t first; bool on;
        ~MarkSunken() { if (on) for (size_t i = first; i < o.size(); ++i) o[i].sunken = true; }
    };
    for (const SceneItem &it : simScene(sim)) {
        const int a = it.gi, b = it.gk;
        const float x = it.pos.x, gy = it.pos.y, z = it.pos.z;
        MarkSunken mark{ out, out.size(), gy < WATER_Y - 0.05f && sim.world.poolAt(a, b) };
        switch (it.kind) {
        case SceneKind::Crate:
        case SceneKind::CrateOpen: {
            Xform xf = then(rotY(-it.yaw), translate(x, gy, z));
            put(out, ObjMesh::Crate, xf);
            if (it.kind == SceneKind::Crate)
                put(out, ObjMesh::CrateLid, then(translate(0, 0.564f, 0), xf));
            else   // lid off, leaning against the side
                put(out, ObjMesh::CrateLid, then(then(rotX(1.35f), translate(0, 0.02f, 0.42f)), xf));
            break;
        }
        case SceneKind::AlmondWater:   // the mesh's base is at y = 0, and pos is on the floor or the shelf
            put(out, ObjMesh::Can, then(rotY(-it.yaw), translate(x, gy, z)));
            break;
        case SceneKind::Doubloon: {
            float bob = sinf(now * 2.0f + a * 1.7f + b) * 0.03f;
            if (mark.on) {
                // On a pool floor the water darkens and tints it toward the
                // floor's colour, and at 10 m a 17 cm coin drawn by the room's
                // light was a dark 5 px dot. It is drawn at 1.6x the size and
                // at a brightness that pulses 1.3-1.7 (a glint), so it reads as
                // gold through 1-3 m of water.
                float glint = 1.5f + 0.2f * sinf(now * 3.1f + a * 2.3f + b * 1.1f);
                put(out, cylinder({ x, gy + 0.03f, z }, 0.136f, 0.136f, 0.03f, 12,
                                  lit({ 234, 188, 74, 255 }, glint)));
                break;
            }
            put(out, cylinder({ x, gy + 0.06f + bob, z }, 0.085f, 0.085f, 0.024f, 12,
                              lit({ 234, 188, 74, 255 }, propLum(x, gy + 0.15f, z))));
            break;
        }
        case SceneKind::Battery: {   // standing on end
            float pl = propLum(x, gy + 0.15f, z);
            put(out, cube({ x, gy + 0.05f, z }, 0.06f, 0.10f, 0.06f, lit({ 70, 150, 90, 255 }, pl)));
            put(out, cylinder({ x, gy + 0.10f, z }, 0.018f, 0.018f, 0.02f, 8, lit({ 200, 180, 90, 255 }, pl)));
            break;
        }
        case SceneKind::Tape: {   // a cassette, label up, two hubs showing
            float pl = propLum(x, gy + 0.15f, z);
            float bob = sinf(now * 1.6f + a * 2.1f + b) * 0.02f;
            put(out, cube({ x, gy + 0.02f + bob, z }, 0.11f, 0.04f, 0.07f, lit({ 40, 38, 42, 255 }, pl)));
            put(out, cube({ x, gy + 0.041f + bob, z }, 0.075f, 0.001f, 0.05f, lit({ 210, 202, 182, 255 }, pl)));
            put(out, cylinder({ x - 0.028f, gy + 0.041f + bob, z }, 0.014f, 0.014f, 0.002f, 8, lit({ 30, 28, 30, 255 }, pl)));
            put(out, cylinder({ x + 0.028f, gy + 0.041f + bob, z }, 0.014f, 0.014f, 0.002f, 8, lit({ 30, 28, 30, 255 }, pl)));
            break;
        }
        case SceneKind::Key: {   // a brass mortice key, bit up, turning slowly
            float bob = sinf(now * 1.9f + a * 1.3f + b) * 0.025f;
            float spin = now * 0.8f + a;
            Rgba brass = lit({ 214, 172, 86, 255 }, propLum(x, gy + 0.15f, z));
            float y = gy + 0.055f + bob;
            // bow, shank and bit
            put(out, cylinderEx({ x, y, z }, { x, y + 0.012f, z }, 0.035f, 0.035f, 10, brass));
            put(out, cube({ x + cosf(spin) * 0.045f, y, z + sinf(spin) * 0.045f }, 0.09f, 0.012f, 0.012f, brass));
            put(out, cube({ x + cosf(spin) * 0.082f, y + 0.014f, z + sinf(spin) * 0.082f }, 0.022f, 0.028f, 0.012f, brass));
            break;
        }
        case SceneKind::Revolver:   // the model itself is each build's (looseRevolver)
            findGlowDraws(it, now, out);
            break;
        case SceneKind::Cigarettes:
            cigarettePackDraws(it, now, out);
            findGlowDraws(it, now, out);
            break;
        case SceneKind::Deck:   // where it was set down, facing the throw direction
            deckDraws(sim, then(rotY(-it.yaw), translate(x, gy, z)), it.variant != 0, now, out);
            break;
        case SceneKind::Coin: {   // gy is the floor they fell on, in this storey's frame
            float bob = sinf(now * 2.4f + x) * 0.03f;
            put(out, cylinder({ x, gy + 0.06f + bob, z }, 0.085f, 0.085f, 0.024f, 12,
                              lit({ 234, 188, 74, 255 }, propLum(x, gy + 0.1f, z))));
            break;
        }
        case SceneKind::Balloon:
            if (it.index == 0) {   // against the ceiling, string hanging down
                float by = gy + sinf(now * 0.8f + a * 1.3f + b * 2.1f) * 0.05f;
                float pl = propLum(x, by, z) * 0.85f;
                put(out, sphere({ x, by, z }, 0.17f, lit(PARTY_RGBA[it.variant], pl)));
                put(out, cylinderEx({ x, by - 0.15f, z }, { x + 0.04f, by - 0.95f, z + 0.02f },
                                    0.005f, 0.005f, 4, lit({ 190, 185, 175, 150 }, pl)));
            } else {   // one of a bunch tied to a party table
                float bx = x + sinf(now * 0.9f + a * 1.7f + (it.index - 1) * 2.3f) * 0.04f;
                float pl = propLum(bx, gy, z) * 0.9f;
                put(out, sphere({ bx, gy, z }, 0.15f, lit(PARTY_RGBA[it.variant], pl)));
                put(out, cylinderEx({ bx, gy - 0.13f, z }, { it.anchor.x + 0.02f, it.anchor.y + 0.02f, it.anchor.z },
                                    0.004f, 0.004f, 4, lit({ 200, 195, 185, 150 }, pl)));
            }
            break;
        case SceneKind::Confetti: {   // falling confetti
            float fade = clampf(it.amount * 1.6f, 0, 1);
            Rgba base = PARTY_RGBA[it.variant];
            put(out, cube({ x, gy, z }, 0.05f, 0.05f, 0.05f,
                          lit({ base.r, base.g, base.b, (unsigned char)(255 * fade) }, propLum(x, gy, z))));
            break;
        }
        case SceneKind::Chalk: {
            // The pre-placed marks are drawn duller and yellower than the
            // player's, with the same arrow shape.
            const Vec3 cm = it.pos;
            Rgba cc = it.variant ? lit({ 228, 228, 218, 210 }, propLum(cm.x, cm.y + 0.1f, cm.z))
                                 : lit({ 186, 180, 156, 150 }, propLum(cm.x, cm.y + 0.1f, cm.z));
            Vec3 along{ cosf(it.yaw), 0, sinf(it.yaw) }, side{ -along.z, 0, along.x };
            Vec3 tip = add(cm, scale(along, 0.25f));
            Vec3 tail = sub(cm, scale(along, 0.25f));
            Vec3 shoulder = add(cm, scale(along, 0.04f));
            put(out, cylinderEx(tail, tip, 0.011f, 0.011f, 4, cc));
            put(out, cylinderEx(tip, add(shoulder, scale(side, 0.17f)), 0.011f, 0.011f, 4, cc));
            put(out, cylinderEx(tip, sub(shoulder, scale(side, 0.17f)), 0.011f, 0.011f, 4, cc));
            break;
        }
        default:   // flares, impacts and the actors are drawn below, from the sim
            break;
        }
    }
    if (sim.mannequin.active && sim.mannequin.storey == sim.world.storey) {
        const auto &m = sim.mannequin;
        Vec3 forward{cosf(m.yaw),0,sinf(m.yaw)}, side{-forward.z,0,forward.x};
        auto at = [&](float x,float y,float z) { return add(m.pos,add(scale(side,x),add(Vec3{0,y,0},scale(forward,z)))); };
        float brightness = propLum(m.pos.x,m.pos.y+1.4f,m.pos.z);
        Rgba ivory = lit({205,194,166,255},brightness), joint = lit({65,58,48,255},brightness);
        auto limb = [&](Vec3 a,Vec3 b,float radius) {
            put(out,cylinderEx(a,b,radius,radius*0.8f,8,ivory));
            put(out,sphere(a,radius*1.12f,joint));
        };
        limb(at(0,0.98f,0),at(0,1.55f,0),0.19f);
        limb(at(0,1.58f,0),at(0,1.83f,0),0.07f);
        Vec3 head = at(m.pose==1 ? 0.09f : -0.035f,1.99f,0.03f);
        put(out,sphere(head,0.18f,ivory));
        for (int sign : {-1,1}) {
            float reach = m.pose==2 ? 0.42f : 0.05f;
            Vec3 shoulder=at(sign*0.24f,1.56f,0), elbow=at(sign*0.37f,m.pose==1 ? 1.55f : 1.18f,reach);
            Vec3 wrist=at(sign*0.30f,m.pose==1 ? 1.91f : 0.81f,reach+0.08f);
            limb(shoulder,elbow,0.07f); limb(elbow,wrist,0.055f);
            put(out,sphere(wrist,0.07f,ivory));
            Vec3 hip=at(sign*0.13f,0.98f,0), knee=at(sign*0.16f,0.52f,sign*0.12f), foot=at(sign*0.17f,0.08f,sign*0.1f);
            limb(hip,knee,0.095f); limb(knee,foot,0.065f);
            put(out,cylinderEx(foot,add(foot,scale(forward,0.22f)),0.07f,0.08f,8,ivory));
        }
    }
    if (sim.wayOpen()) {   // enough doubloons: exits glow green
        float pulse = 0.7f + 0.3f * sinf(now * 3.0f);
        for (int dx = -6; dx <= 6; dx++) for (int dz = -6; dz <= 6; dz++) {
            int i = pci + dx, k = pck + dz;
            float doorX = 0, doorZ = 0;
            if (sim.world.wallNVal(i, k) == WALL_EXIT) { doorX = i * CELL + 1.0f; doorZ = k * CELL; }
            else if (sim.world.wallWVal(i, k) == WALL_EXIT) { doorX = i * CELL; doorZ = k * CELL + 1.0f; }
            else continue;
            Vec3 gp = { doorX, 1.15f, doorZ };
            put(out, sphere(gp, 0.34f, { 150, 255, 170, (unsigned char)(150 * pulse) }));   // bright core
            put(out, sphere(gp, 0.62f, { 90, 255, 120, (unsigned char)(95 * pulse) }));
            put(out, sphere(gp, 1.35f, { 60, 235, 110, (unsigned char)(40 * pulse) }));     // wide halo
        }
    }
    for (const FlareProj &f : sim.litFlares) {   // each flare: core, orange halo, and the stick
        if (!f.active) continue;
        // its own intensity, not the point light's, which belongs to the
        // dominant flare only
        float glow = flareGlow(f, flick);
        Vec3 fp = { f.x, f.y + 0.05f, f.z };
        put(out, cylinder({ f.x, f.y - 0.03f, f.z }, 0.018f, 0.022f, 0.09f, 8, { 130, 30, 22, 255 }));
        put(out, sphere(fp, 0.035f + 0.012f * flick, { 255, 240, 208, 255 }));
        put(out, sphere(fp, 0.13f, { 255, 120, 40, (unsigned char)(90 * glow) }));
        put(out, sphere(fp, 0.30f, { 255, 70, 20, (unsigned char)(28 * glow) }));
    }
    if (ActorSprite sp; hunterSprite(sim, sp)) {
        const LevelCfg &c = LEVELS[sim.level];
        float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
        float lum = lightAtCPU(sp.litAt.x, sp.litAt.y, sp.litAt.z, sim.blackoutCur,
                               c.ls, c.wallH - LIGHT_DROP, c.dead, c.lightMul, ambLum,
                               sim.ent.x, sim.ent.z, sim.entDarkCur, c.vary);   // the light at his position, dimmed by entDarkCur
        if (sim.flashCur > 0.05f) {   // lit by the flashlight
            float vx2 = sim.ent.x - sim.px, vz2 = sim.ent.z - sim.pz;
            float d2 = vx2 * vx2 + vz2 * vz2 + 1e-4f, dl = sqrtf(d2);
            float cone = powf(fmaxf((vx2 * sim.fwd.x + vz2 * sim.fwd.z) / dl, 0.0f), 26.0f);
            lum = clampf(lum + sim.flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
        }
        if (flareInt > 0.01f) {   // lit by the flare or muzzle flash
            float fvx = sim.ent.x - flarePos.x, fvz = sim.ent.z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fvx * fvx + fvz * fvz)), 0.0f, 1.0f);
        }
        lum = clampf(lum, 0.0f, 1.0f);
        float fogf = expf(-sim.entDist * c.fogDen);
        unsigned char lum8 = cl8(40 + 215 * lum);
        unsigned char al = cl8(255 * clampf(fogf * 1.6f, 0, 1) * sp.fade);
        ShapeTex t = sp.sheet == SpriteSheet::Partygoer ? ShapeTex::Partygoer
                   : sp.sheet == SpriteSheet::Clark ? ShapeTex::Clark
                   : sp.sheet == SpriteSheet::Animation ? ShapeTex::Animation : ShapeTex::Smiler;
        spriteDraw(out, t, sp, sp.frame0, { lum8, lum8, lum8, cl8(al * (1.0f - sp.blend)) });
        spriteDraw(out, t, sp, sp.frame1, { lum8, lum8, lum8, cl8(al * sp.blend) });
        if (sp.glow) {   // the Smiler's eyes and grin are drawn unlit
            unsigned char ga = cl8(255 * clampf(fogf * 2.2f, 0, 1) * sp.fade);
            spriteDraw(out, ShapeTex::SmilerGlow, sp, sp.frame0, { 255, 255, 255, cl8(ga * (1.0f - sp.blend)) });
            spriteDraw(out, ShapeTex::SmilerGlow, sp, sp.frame1, { 255, 255, 255, cl8(ga * sp.blend) });
        }
    }
    if (ActorSprite sp; usherSprite(sim, sp)) {
        const LevelCfg &c = LEVELS[sim.level];
        float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
        float lum = lightAtCPU(sp.litAt.x, sp.litAt.y, sp.litAt.z, sim.blackoutCur,
                               c.ls, c.wallH - LIGHT_DROP, c.dead, c.lightMul, ambLum, 0, 0, 0, c.vary);
        float ux = sp.litAt.x - sim.px, uz = sp.litAt.z - sim.pz, ud = sqrtf(ux * ux + uz * uz) + 1e-4f;
        if (sim.flashCur > 0.05f) {
            float cone = powf(fmaxf((ux * sim.fwd.x + uz * sim.fwd.z) / ud, 0.0f), 26.0f);
            lum = clampf(lum + sim.flashCur * cone * 7.5f / (1.0f + 0.10f * ud * ud), 0.0f, 1.0f);
        }
        if (flareInt > 0.01f) {
            float fx = sp.litAt.x - flarePos.x, fz = sp.litAt.z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fx * fx + fz * fz)), 0.0f, 1.0f);
        }
        // Its own torch lights its front a little, so it shows in a dark auditorium.
        lum = clampf(lum + (sp.row == ENT_ROW_FACE ? 0.22f : 0.08f), 0.0f, 1.0f);
        const float fogf = expf(-ud * c.fogDen);
        unsigned char l8 = cl8(40 + 215 * lum);
        unsigned char al = cl8(255 * clampf(fogf * 1.6f, 0, 1) * sp.fade);
        spriteDraw(out, ShapeTex::Usher, sp, sp.frame0, { l8, l8, l8, cl8(al * (1.0f - sp.blend)) });
        spriteDraw(out, ShapeTex::Usher, sp, sp.frame1, { l8, l8, l8, cl8(al * sp.blend) });
        Vec3 torch, dir;
        if (usherTorch(sim, torch, dir)) {
            // The lens, then the beam through the haze: a cone USHER_BEAM_M long
            // and USHER_BEAM_HALF wide, as the sim sees with it. Brighter when
            // it has found the player.
            bool spot = sim.usher.st == UState::Spot;
            const float len = Sim::USHER_BEAM_M * 0.75f;
            Shape lens = billboardWhole(ShapeTex::Particle, torch, 0.20f, { 255, 238, 196, cl8(230 * fminf(1.0f, fogf * 2.0f)) });
            lens.additive = true;
            put(out, lens);
            Shape beam = cylinderEx(torch, add(torch, scale(dir, len)), 0.05f, len * tanf(Sim::USHER_BEAM_HALF), 16,
                                    { 255, 228, 176, cl8((spot ? 20 : 10) * fogf + 3) });
            beam.depthWrite = false;
            put(out, beam);
        }
    }
    for (const Sim::Bullet &b : sim.bullets) {
        Vec3 tail = sub(b.pos, scale(b.direction, fminf(1.2f, dist(b.pos, b.tail))));
        put(out, cylinderEx(tail, b.pos, 0.006f, 0.009f, 5, { 255, 211, 126, 220 }));
        put(out, sphere(b.pos, 0.012f, { 255, 237, 187, 255 }));
    }
    if (sim.level == 1) {
        // Level 1's ground mist (the wiki's "low-hanging fog"): wide, faint
        // billboards on the floor, drifting slowly. One per floor cell with a
        // hashed offset, lit by the room light at its position. No depth
        // writes, or each bank would cut a hole in the one behind it; faded out
        // near the player.
        int mci = cellOf(sim.px), mck = cellOf(sim.pz);
        for (int dx = -11; dx <= 11; dx++) for (int dz = -11; dz <= 11; dz++) {
            int a = mci + dx, b = mck + dz;
            uint32_t h = ih(a, b, 0xF06u);
            if ((h & 3) == 0 || sim.world.pillarAt(a, b)) continue;         // three cells in four
            float mxp = a * CELL + 1.0f + (((h >> 4) & 255) / 255.0f - 0.5f) * 1.2f + sinf(now * 0.07f + (h & 63)) * 0.35f;
            float mzp = b * CELL + 1.0f + (((h >> 12) & 255) / 255.0f - 0.5f) * 1.2f + cosf(now * 0.05f + (h & 31)) * 0.35f;
            float ddx = mxp - sim.px, ddz = mzp - sim.pz, d = sqrtf(ddx * ddx + ddz * ddz);
            if (d > 22.0f || d < 1.2f) continue;
            float fade = clampf((d - 1.2f) / 2.5f, 0.0f, 1.0f) * clampf((22.0f - d) / 6.0f, 0.0f, 1.0f);
            float gy = sim.world.floorY(a, b);
            float my = gy + 0.26f + ((h >> 20) & 7) * 0.03f;
            float lum = propLum(mxp, my, mzp);
            float breathe = 0.75f + 0.25f * sinf(now * 0.3f + (h >> 24));
            Rgba mc = { cl8(170 * lum), cl8(178 * lum), cl8(168 * lum), cl8(78 * fade * breathe) };
            float w = 3.0f + ((h >> 26) & 3) * 0.5f;
            Shape s = billboard(ShapeTex::Particle, 0, 0, 32, 32, { mxp, my, mzp }, w, w * 0.30f, mc);
            s.depthWrite = false;
            put(out, s);
        }
    }
    for (const Sim::BulletImpact &impact : sim.bulletImpacts) {
        float age = 0.24f - impact.life;
        Rgba color = impact.body ? Rgba{ 125, 32, 24, 190 } : Rgba{ 211, 188, 144, 190 };
        color.a = cl8(190 * impact.life / 0.24f);
        put(out, billboardWhole(ShapeTex::Particle, impact.pos, 0.06f + age * 0.7f, color));
        for (int i = 0; i < 5; ++i) {
            Vec3 spread{ cosf(i * 2.4f) * 0.7f, sinf(i * 3.7f) * 0.7f, sinf(i * 2.4f) * 0.7f };
            Vec3 p = add(impact.pos, scale(add(impact.normal, spread), age * 1.4f));
            p.y -= age * age * 2;
            put(out, sphere(p, 0.009f, color));
        }
    }
}

// A cigarette between the fingers, low on the right, raised to the mouth
// for the inhale of a drag. It shortens as it burns. Lit, it has an ember
// that flares on the inhale and a thread of smoke; after each breath out
// a cloud drifts away from the eye.
static void cigaretteDraws(const Sim &sim, const SimView &view, float now, std::vector<SceneDraw> &out) {
    const Sim::Smoke &sm = sim.smoke;
    const Vec3 F = view.forward, U = view.up, R = normalize(cross(F, U));
    float raise = 0;   // 0 at rest, 1 at the lips
    if (sm.dragT > 0) {
        float el = Sim::DRAG_TIME - sm.dragT;   // s into the drag
        raise = el < Sim::DRAG_IN ? fminf(1.0f, el / 0.3f) : fmaxf(0.0f, 1.0f - (el - Sim::DRAG_IN) / 0.4f);
        raise = raise * raise * (3 - 2 * raise);
    }
    const Vec3 rest = add(view.eye, add(scale(F, 0.24f), add(scale(R, 0.085f), scale(U, -0.085f))));
    const Vec3 lips = add(view.eye, add(scale(F, 0.07f), scale(U, -0.055f)));
    const Vec3 filterEnd = add(scale(rest, 1 - raise), scale(lips, raise));
    // Away from the hand, a little left and up; pointing out of the mouth when raised.
    const Vec3 dir = normalize(add(scale(F, 0.55f + 0.4f * raise), add(scale(R, -0.65f * (1 - raise)), scale(U, 0.12f))));
    const bool lit = sm.burnT > 0;
    const float burnt = lit ? 1.0f - sm.burnT / Sim::SMOKE_TIME : 0.0f;
    const float filterLen = 0.024f, paperLen = 0.062f * (1.0f - 0.85f * burnt), r = 0.0042f;
    const Vec3 filterTip = add(filterEnd, scale(dir, filterLen));
    const Vec3 tip = add(filterTip, scale(dir, paperLen));
    put(out, cylinderEx(filterEnd, filterTip, r, r, 8, { 206, 132, 66, 255 }));
    put(out, cylinderEx(filterTip, tip, r, r, 8, { 236, 232, 222, 255 }));
    if (!lit) return;
    const bool inhaling = sm.dragT > Sim::DRAG_TIME - Sim::DRAG_IN;
    const Vec3 ashEnd = add(tip, scale(dir, 0.006f));
    put(out, cylinderEx(tip, ashEnd, r, r * 0.9f, 8, { 96, 92, 88, 255 }));
    const float glow = inhaling ? 1.0f : 0.45f + 0.1f * sinf(now * 7.0f);
    Shape ember = billboardWhole(ShapeTex::Particle, ashEnd, 0.022f + 0.022f * glow, { 255, 110, 30, cl8(255 * glow) });
    Shape coal = billboardWhole(ShapeTex::Particle, ashEnd, 0.008f + 0.006f * glow, { 255, 220, 150, 255 });
    ember.additive = coal.additive = true;
    ember.depthWrite = coal.depthWrite = false;
    put(out, ember);
    put(out, coal);
    for (int i = 0; i < 4; i++) {   // the thread off the tip, rising and fading
        float ph = fmodf(now * 0.5f + i * 0.25f, 1.0f);
        Vec3 p = add(ashEnd, { sinf(now * 1.7f + i * 2.1f) * 0.01f * ph, ph * 0.12f, cosf(now * 1.3f + i) * 0.01f * ph });
        Shape wisp = billboardWhole(ShapeTex::Particle, p, 0.01f + ph * 0.04f, { 190, 190, 196, cl8(90 * (1 - ph)) });
        wisp.depthWrite = false;
        put(out, wisp);
    }
    const float EXHALE_S = 1.6f;   // how long a breath's cloud hangs
    if (sm.exhaleT < EXHALE_S) {
        float e = sm.exhaleT / EXHALE_S;
        for (int i = 0; i < 3; i++) {
            Vec3 p = add(lips, add(scale(F, 0.12f + 0.45f * e + i * 0.05f), scale(U, 0.03f * e + i * 0.015f)));
            Shape cloud = billboardWhole(ShapeTex::Particle, p, 0.06f + 0.30f * e + i * 0.03f,
                                         { 200, 200, 206, cl8(70 * (1 - e) * (1 - i * 0.25f)) });
            cloud.depthWrite = false;
            put(out, cloud);
        }
    }
}

void heldDraws(const Sim &sim, const SimView &view, float now, std::vector<SceneDraw> &out) {
    switch (heldItem(sim)) {
    case Held::Can: put(out, ObjMesh::Can, xformOf(heldCan(sim, view))); break;   // drinking hides the deck
    case Held::Deck: deckDraws(sim, xformOf(heldDeck(sim, view)), sim.deck.playing, now, out); break;
    case Held::Flare: put(out, ObjMesh::Flare, xformOf(heldWeapon(sim, view))); break;
    case Held::Cigarette: cigaretteDraws(sim, view, now, out); break;
    default: break;
    }
}

void muzzleDraws(const Sim &sim, Vec3 muzzle, Vec3 up, std::vector<SceneDraw> &out) {
    if (sim.muzzleT > 0) {
        float life = sim.muzzleT / Sim::MUZZLE_FLASH;
        Shape outer = billboardWhole(ShapeTex::Particle, muzzle, 0.07f * life, { 255, 175, 70, 230 });
        Shape core = billboardWhole(ShapeTex::Particle, muzzle, 0.025f * life, { 255, 245, 190, 255 });
        outer.additive = core.additive = true;
        put(out, outer);
        put(out, core);
    }
    if (sim.muzzleSmoke > 0.02f) {
        for (int i = 0; i < 3; ++i) {
            Vec3 puff = add(muzzle, scale(up, (1 - sim.muzzleSmoke) * 0.025f + i * 0.009f));
            put(out, billboardWhole(ShapeTex::Particle, puff, 0.022f + i * 0.011f,
                                    { 155, 151, 142, cl8(sim.muzzleSmoke * (55 - i * 12)) }));
        }
    }
}

void shapeTexSize(ShapeTex t, int &w, int &h) {
    switch (t) {
    case ShapeTex::Particle: w = 32; h = 32; return;
    case ShapeTex::Clark: case ShapeTex::Smiler: case ShapeTex::SmilerGlow: case ShapeTex::Partygoer:
    case ShapeTex::Usher: case ShapeTex::Animation:
        w = ENT_FRAME_W * ENT_FRAMES; h = ENT_FRAME_H * ENT_ROWS; return;
    default: w = h = 1; return;
    }
}

Vec3 cameraRight(const SimView &view) {
    // MatrixLookAt: x = normalize(cross(up, eye - target)), and eye - target
    // is -forward.
    return normalize(cross(view.up, scale(view.forward, -1.0f)));
}

// raylib's shapes (rmodels.c), triangle for triangle. Normals are the face's
// or zero: these draw unlit.
void shapeTriangles(const Shape &s, Vec3 camRight, int texW, int texH, MB &out) {
    const Vec2 uv0 = PLAIN_UV;
    const Vec3 n0 = { 0, 1, 0 };
    auto tri = [&](Vec3 a, Vec3 b, Vec3 c) { out.tri(a, b, c, n0, uv0, uv0, uv0, s.colour); };
    switch (s.kind) {
    case ShapeKind::Cube: {
        float x = s.a.x, y = s.a.y, z = s.a.z;
        float w = s.size.x * 0.5f, h = s.size.y * 0.5f, l = s.size.z * 0.5f;
        auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) { tri(a, b, c); tri(a, c, d); };
        quad({ x - w, y - h, z + l }, { x + w, y - h, z + l }, { x + w, y + h, z + l }, { x - w, y + h, z + l });   // front
        quad({ x - w, y - h, z - l }, { x - w, y + h, z - l }, { x + w, y + h, z - l }, { x + w, y - h, z - l });   // back
        quad({ x - w, y + h, z - l }, { x - w, y + h, z + l }, { x + w, y + h, z + l }, { x + w, y + h, z - l });   // top
        quad({ x - w, y - h, z - l }, { x + w, y - h, z - l }, { x + w, y - h, z + l }, { x - w, y - h, z + l });   // bottom
        quad({ x + w, y - h, z - l }, { x + w, y + h, z - l }, { x + w, y + h, z + l }, { x + w, y - h, z + l });   // right
        quad({ x - w, y - h, z - l }, { x - w, y - h, z + l }, { x - w, y + h, z + l }, { x - w, y + h, z - l });   // left
        break;
    }
    case ShapeKind::Sphere: {   // DrawSphere: DrawSphereEx at 16 rings and 16 slices
        const int rings = 16, slices = 16;
        const float ringangle = (PI_F / 180.0f) * (180.0f / (rings + 1)), sliceangle = (PI_F / 180.0f) * (360.0f / slices);
        const float cosring = cosf(ringangle), sinring = sinf(ringangle);
        const float cosslice = cosf(sliceangle), sinslice = sinf(sliceangle);
        Vec3 v[4] = {};
        v[2] = { 0, 1, 0 };
        v[3] = { sinring, cosring, 0 };
        auto at = [&](Vec3 p) { return add(s.a, scale(p, s.r0)); };
        for (int i = 0; i < rings + 1; i++) {
            for (int j = 0; j < slices; j++) {
                v[0] = v[2];
                v[1] = v[3];
                v[2] = { cosslice * v[2].x - sinslice * v[2].z, v[2].y, sinslice * v[2].x + cosslice * v[2].z };
                v[3] = { cosslice * v[3].x - sinslice * v[3].z, v[3].y, sinslice * v[3].x + cosslice * v[3].z };
                tri(at(v[0]), at(v[3]), at(v[1]));
                tri(at(v[0]), at(v[2]), at(v[3]));
            }
            v[2] = v[3];
            v[3] = { cosring * v[3].x + sinring * v[3].y, -sinring * v[3].x + cosring * v[3].y, v[3].z };
        }
        break;
    }
    case ShapeKind::Cylinder: {   // base at a, top radius r0, bottom radius r1
        const int sides = s.sides < 3 ? 3 : s.sides;
        const float step = (PI_F / 180.0f) * (360.0f / sides);
        auto ring = [&](int i, float r, float y) {
            return Vec3{ s.a.x + sinf(i * step) * r, s.a.y + y, s.a.z + cosf(i * step) * r };
        };
        const Vec3 top = { s.a.x, s.a.y + s.h, s.a.z };
        for (int i = 0; i < sides; i++) {
            if (s.r0 > 0) {
                tri(ring(i, s.r1, 0), ring(i + 1, s.r1, 0), ring(i + 1, s.r0, s.h));
                tri(ring(i, s.r0, s.h), ring(i, s.r1, 0), ring(i + 1, s.r0, s.h));
                tri(top, ring(i, s.r0, s.h), ring(i + 1, s.r0, s.h));
            } else {
                tri(top, ring(i, s.r1, 0), ring(i + 1, s.r1, 0));
            }
            tri(s.a, ring(i + 1, s.r1, 0), ring(i, s.r1, 0));
        }
        break;
    }
    case ShapeKind::CylinderEx: {
        const int sides = s.sides < 3 ? 3 : s.sides;
        Vec3 dir = sub(s.b, s.a);
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) break;
        // Vector3Perpendicular: across the direction's smallest component
        float mn = fabsf(dir.x);
        Vec3 axis = { 1, 0, 0 };
        if (fabsf(dir.y) < mn) { mn = fabsf(dir.y); axis = { 0, 1, 0 }; }
        if (fabsf(dir.z) < mn) axis = { 0, 0, 1 };
        Vec3 b1 = normalize(cross(dir, axis));
        Vec3 b2 = normalize(cross(b1, dir));
        const float base = 2.0f * PI_F / sides;
        auto ring = [&](Vec3 c, float r, int i) {
            float sn = sinf(base * i) * r, cs = cosf(base * i) * r;
            return Vec3{ c.x + sn * b1.x + cs * b2.x, c.y + sn * b1.y + cs * b2.y, c.z + sn * b1.z + cs * b2.z };
        };
        for (int i = 0; i < sides; i++) {
            Vec3 w1 = ring(s.a, s.r0, i), w2 = ring(s.a, s.r0, i + 1);
            Vec3 w3 = ring(s.b, s.r1, i), w4 = ring(s.b, s.r1, i + 1);
            if (s.r0 > 0) tri(s.a, w2, w1);
            tri(w1, w2, w3);
            tri(w2, w4, w3);
            if (s.r1 > 0) tri(s.b, w3, w4);
        }
        break;
    }
    case ShapeKind::Billboard: {   // DrawBillboardPro, up the world's y, centred
        Vec3 right = scale(camRight, s.size.x), up = { 0, s.size.y, 0 };
        Vec3 o = add(scale(normalize(right), s.size.x * 0.5f), scale(normalize(up), s.size.y * 0.5f));
        Vec3 p0 = sub(s.a, o), p1 = add(p0, right), p2 = add(p1, up), p3 = add(p0, up);
        float u0 = s.src[0] / texW, u1 = (s.src[0] + s.src[2]) / texW;
        float vTop = s.src[1] / texH, vBottom = (s.src[1] + s.src[3]) / texH;
        out.quad(p0, p1, p2, p3, n0, { u0, vBottom }, { u1, vBottom }, { u1, vTop }, { u0, vTop }, s.colour);
        break;
    }
    }
}
