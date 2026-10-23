#include "../core/fp_strict.h"
#include "draw.h"
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

// How hard one flare is burning right now: a fast flare-up as the cap comes
// off, a fade over the last second and a half, and the flicker on top. Both
// the point light and the halo spheres read it, so a flare that is nearly out
// dims in the room and in its own glow together.
float flareGlow(const FlareProj &f, float flick) {
    return clampf((Sim::FLAREBURN - f.burn) * 6.0f, 0, 1) * clampf(f.burn / Sim::FLAREFADE, 0, 1) * flick;
}

float dist(Vec3 a, Vec3 b) {
    Vec3 d = sub(a, b);
    return sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
}

Rgba lit(Rgba c, float f) { return { cl8(c.r * f), cl8(c.g * f), cl8(c.b * f), c.a }; }

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
    if (sp.sheet == SpriteSheet::Dog) {
        src[0] = (float)frame * DOG_FRAME_W; src[1] = 0; src[2] = (float)DOG_FRAME_W; src[3] = (float)DOG_FRAME_H;
        return;
    }
    src[0] = (float)frame * ENT_FRAME_W; src[1] = (float)sp.row * ENT_FRAME_H;
    src[2] = (float)ENT_FRAME_W; src[3] = (float)ENT_FRAME_H;
}
void spriteDraw(std::vector<SceneDraw> &out, ShapeTex t, const ActorSprite &sp, int frame, Rgba col) {
    float src[4];
    spriteSrc(sp, frame, src);
    put(out, billboard(t, src[0], src[1], src[2], src[3], sp.centre, sp.w, sp.h, col));
}

// One tape player: the body, the two reels turning in the bay, and the record
// lamp if the tape is running. The lamp breathes: a steady dot of red is easy
// to miss on a dark floor.
void deckDraws(const Sim &sim, const Xform &xf, bool lamp, float now, std::vector<SceneDraw> &out) {
    put(out, ObjMesh::Deck, xf);
    for (int i = 0; i < 2; i++) {
        // both hubs turn the same way: the tape only travels in one direction
        Xform r = then(rotY(sim.deck.reel), translate(i ? 0.024f : -0.024f, 0.0505f, 0.0f));
        put(out, ObjMesh::Reel, then(r, xf));
    }
    if (lamp) {
        float pu = 0.45f + 0.55f * (0.5f + 0.5f * sinf(now * 4.2f));
        put(out, ObjMesh::DeckLamp, xf, cl8(255 * pu) / 255.0f);
    }
}

}   // namespace

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

FrameLight frameLight(Sim &sim, float now, float ir) {
    FrameLight l{};
    l.time = now;
    l.blackout = sim.blackoutCur * (sim.whisperT > 0 ? 0.86f : 1.0f);   // lights sag while it whispers
    l.flash = sim.flashCur;
    if (sim.flashOn && sim.battery < 0.15f) {   // the battery's dying: a warning flicker before it goes dark
        float lowFlick = 0.55f + 0.45f * sinf(now * 19.0f) * sinf(now * 31.0f + 1.7f);
        l.flash *= clampf(sim.battery / 0.15f, 0.15f, 1.0f) * (0.7f + 0.3f * lowFlick);
    }
    l.flashDir = sim.fwd;
    l.ir = ir;
    l.flick = 0.91f + 0.09f * sinf(now * 31.0f) * sinf(now * 47.3f + 1.3f);
    // One flare point light: the fire with the most presence at your feet, so
    // a guttering flare underfoot does not hold the light off a fresh one up
    // the hall (Sim::dominantFlare).
    const FlareProj *lead = sim.dominantFlare(sim.px, sim.pz);
    l.flareInt = lead ? flareGlow(*lead, l.flick) : 0.0f;
    l.flarePos = lead ? Vec3{ lead->x, lead->y + 0.06f, lead->z } : Vec3{ sim.px, sim.eyeY, sim.pz };
    if (!lead && sim.muzzleT > 0) {   // the muzzle flash borrows the flare's light
        l.flareInt = sim.muzzleT / Sim::MUZZLE_FLASH * 1.3f;
        l.flarePos = { sim.px + sim.f2x * 0.6f, sim.eyeY - 0.05f, sim.pz + sim.f2z * 0.6f };
    }
    // the hunter's pool of dead light travels with it, and it is solid
    l.entPos = { sim.ent.x, sim.ent.dispY + 1.0f, sim.ent.z };
    l.entDark = (sim.ent.st == EState::Hidden) ? 0.0f : sim.entDarkCur;
    l.entBlock = (sim.ent.st == EState::Hidden || sim.ent.st == EState::Die) ? 0.0f : 1.0f;
    // The nearest enclosed stairwell's landing light, in this storey's frame:
    // the one rising from here, or the one arriving here whose landing is a
    // storey down. The tray grid has no fitting over an opening, so without
    // this a stair shaft is lit only by what falls down it from 7 m up.
    const int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
    Vec3 wellLamp = { 0, 0, 0 };
    float wellD2 = 1e30f;
    if (sim.world.storeyH > 0.0f) {
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            int cx = pcx + dx, cz = pcz + dz;
            auto it = sim.world.chunks.find(World::key(cx, cz));
            if (it == sim.world.chunks.end()) continue;
            const ChunkData &cd = it->second;
            for (int q = 0; q < cd.nfeat; q++) {
                const VertFeat &f = cd.feats[q];
                if (f.kind != VK_STAIRWELL) continue;
                Vec3 lp = sim.world.landingLamp(f, cx, cz);
                lp.y += (f.lo - sim.world.storey) * sim.world.storeyH;
                float d2 = (lp.x - sim.px) * (lp.x - sim.px) + (lp.y - sim.eyeY) * (lp.y - sim.eyeY) + (lp.z - sim.pz) * (lp.z - sim.pz);
                if (d2 < wellD2) { wellD2 = d2; wellLamp = lp; }
            }
        }
    }
    // The Manila Room, when one is near: its tubes out, its chandelier on. An
    // inverted rectangle masks nothing. The chandelier ignores the blackouts
    // and wavers like an old filament rather than stuttering like a tube.
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
        // A tube like any other: it goes out in a blackout and stutters with
        // the rest, rather than holding on the way the chandelier does.
        l.lamp[0] = wellLamp.x; l.lamp[1] = wellLamp.y; l.lamp[2] = wellLamp.z;
        l.lamp[3] = 0.95f * sim.blackoutCur * l.flick;
        l.lampCol = { 0.95f, 0.93f, 0.80f };
    }
    setLightExtrasCPU(l.roomMask, l.lamp);
    return l;
}

void sceneDraws(Sim &sim, const FrameLight &light, float now, std::vector<SceneDraw> &out) {
    const int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
    const int pci = cellOf(sim.px), pck = cellOf(sim.pz);
    const float flick = light.flick, flareInt = light.flareInt;
    const Vec3 flarePos = light.flarePos;
    // Stairwell landing lights: the tube on each batten near you, as bright as
    // the tubes are tonight, drawn here because a chunk mesh cannot go out.
    if (sim.world.storeyH > 0.0f) {
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            auto it = sim.world.chunks.find(World::key(pcx + dx, pcz + dz));
            if (it == sim.world.chunks.end()) continue;
            for (int q = 0; q < it->second.nfeat; q++) {
                const VertFeat &f = it->second.feats[q];
                if (f.kind != VK_STAIRWELL) continue;
                float yo = (f.lo - sim.world.storey) * sim.world.storeyH;
                Vec3 c = sim.world.featureWorld(f, pcx + dx, pcz + dz, CELL, sim.world.storeyH * 0.5f + 2.25f - 0.065f + yo,
                                                4 * CELL - WT - 0.145f);
                Vec3 e = sim.world.featureWorld(f, pcx + dx, pcz + dz, CELL + 1.0f, 0, 0);
                Vec3 o = sim.world.featureWorld(f, pcx + dx, pcz + dz, CELL, 0, 0);
                bool alongX = fabsf(e.x - o.x) > 0.5f;
                unsigned char g8 = cl8(60 + 195 * sim.blackoutCur * flick);
                put(out, cube(c, alongX ? 0.96f : 0.035f, 0.035f, alongX ? 0.035f : 0.96f, { g8, g8, cl8(g8 * 0.92f), 255 }));
            }
        }
    }
    // Small things draw unlit, so the room light at each one is estimated on
    // the CPU (plus the flare or muzzle glow): no balloons shining through a
    // blackout.
    const LevelCfg &lc = LEVELS[sim.level];
    float ambLumP = (lc.amb.x + lc.amb.y + lc.amb.z) / 3.0f;
    float edSend = (sim.ent.st == EState::Hidden) ? 0.0f : sim.entDarkCur;
    // The camcorder's infrared lamp on what is drawn unlit, as the world shader has it.
    auto irLum = [&](float x, float y, float z) {
        if (light.ir <= 0.01f) return 0.0f;
        float dx = x - sim.px, dy = y - sim.eyeY, dz = z - sim.pz;
        float d2 = dx * dx + dy * dy + dz * dz, d = sqrtf(d2) + 1e-4f;
        float ahead = fmaxf((dx * sim.fwd.x + dy * sim.fwd.y + dz * sim.fwd.z) / d, 0.0f);
        return light.ir * (0.30f + 0.70f * ahead * ahead * ahead) * 6.0f / (1.0f + 0.14f * d2);
    };
    auto propLum = [&](float x, float y, float z) {
        float lum = lightAtCPU(x, y, z, sim.blackoutCur, lc.ls, lc.wallH - LIGHT_DROP, lc.dead, lc.lightMul, ambLumP,
                               sim.ent.x, sim.ent.z, edSend, lc.vary);
        if (flareInt > 0.01f) {
            float fx = x - flarePos.x, fy = y - flarePos.y, fz = z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fx * fx + fy * fy + fz * fz)), 0.0f, 1.0f);
        }
        lum = clampf(lum + irLum(x, y, z), 0.0f, 1.0f);
        return 0.18f + 0.82f * lum;   // a hint of form survives the dark
    };
    // What stands in the world, placed by the same rules the sim tests
    // (simScene); only the look, the bob and the sway are decided here.
    for (const SceneItem &it : simScene(sim)) {
        const int a = it.gi, b = it.gk;
        const float x = it.pos.x, gy = it.pos.y, z = it.pos.z;
        switch (it.kind) {
        case SceneKind::Crate:
        case SceneKind::CrateOpen: {
            Xform xf = then(rotY(-it.yaw), translate(x, gy, z));
            put(out, ObjMesh::Crate, xf);
            if (it.kind == SceneKind::Crate)
                put(out, ObjMesh::CrateLid, then(translate(0, 0.564f, 0), xf));
            else   // prised off and leaned against the side
                put(out, ObjMesh::CrateLid, then(then(rotX(1.35f), translate(0, 0.02f, 0.42f)), xf));
            break;
        }
        case SceneKind::AlmondWater:   // the mesh's base is at y = 0, and pos is on the floor or the shelf
            put(out, ObjMesh::Can, then(rotY(-it.yaw), translate(x, gy, z)));
            break;
        case SceneKind::Doubloon: {
            float bob = sinf(now * 2.0f + a * 1.7f + b) * 0.03f;
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
            // bow, shank, bit: small, so it reads by its glint and its turn
            put(out, cylinderEx({ x, y, z }, { x, y + 0.012f, z }, 0.035f, 0.035f, 10, brass));
            put(out, cube({ x + cosf(spin) * 0.045f, y, z + sinf(spin) * 0.045f }, 0.09f, 0.012f, 0.012f, brass));
            put(out, cube({ x + cosf(spin) * 0.082f, y + 0.014f, z + sinf(spin) * 0.082f }, 0.022f, 0.028f, 0.012f, brass));
            break;
        }
        case SceneKind::Deck:   // wherever you set it down, facing the way you threw it
            deckDraws(sim, then(rotY(-it.yaw), translate(x, gy, z)), it.variant != 0, now, out);
            break;
        case SceneKind::Coin: {   // gy is the floor they fell on, in this storey's frame
            float bob = sinf(now * 2.4f + x) * 0.03f;
            put(out, cylinder({ x, gy + 0.06f + bob, z }, 0.085f, 0.085f, 0.024f, 12,
                              lit({ 234, 188, 74, 255 }, propLum(x, gy + 0.1f, z))));
            break;
        }
        case SceneKind::Balloon:
            if (it.index == 0) {   // nosing against the ceiling, string hanging down
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
        case SceneKind::Confetti: {   // bursts still tumbling to the carpet
            float fade = clampf(it.amount * 1.6f, 0, 1);
            Rgba base = PARTY_RGBA[it.variant];
            put(out, cube({ x, gy, z }, 0.05f, 0.05f, 0.05f,
                          lit({ base.r, base.g, base.b, (unsigned char)(255 * fade) }, propLum(x, gy, z))));
            break;
        }
        case SceneKind::Chalk: {
            // The stranger's chalk has been down longer than yours: duller,
            // yellower, further gone. Same arrow, so it reads as a mark first
            // and as somebody else's a moment later.
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
    if (sim.wayOpen()) {   // enough doubloons: real exits burn green, the way out
        float pulse = 0.7f + 0.3f * sinf(now * 3.0f);
        for (int dx = -6; dx <= 6; dx++) for (int dz = -6; dz <= 6; dz++) {
            int i = pci + dx, k = pck + dz;
            float doorX = 0, doorZ = 0;
            if (sim.world.wallNVal(i, k) == WALL_EXIT) { doorX = i * CELL + 1.0f; doorZ = k * CELL; }
            else if (sim.world.wallWVal(i, k) == WALL_EXIT) { doorX = i * CELL; doorZ = k * CELL + 1.0f; }
            else continue;
            if (sim.world.cursedExit(i, k)) continue;   // cursed doors stay a trap, never green
            Vec3 gp = { doorX, 1.15f, doorZ };
            put(out, sphere(gp, 0.34f, { 150, 255, 170, (unsigned char)(150 * pulse) }));   // bright core
            put(out, sphere(gp, 0.62f, { 90, 255, 120, (unsigned char)(95 * pulse) }));
            put(out, sphere(gp, 1.35f, { 60, 235, 110, (unsigned char)(40 * pulse) }));     // wide halo
        }
    }
    if (sim.level == 3) {   // valve wheels on their standpipes: red while open, green once shut
        for (int dx = -7; dx <= 7; dx++) for (int dz = -7; dz <= 7; dz++) {
            int a = pci + dx, b = pck + dz;
            if (!sim.world.valveAt(a, b)) continue;
            float vx = a * CELL + 1.0f, vz = b * CELL + 1.0f;
            float wy = sim.world.floorY(a, b) + 1.13f;
            bool shut = sim.valvesTurned.count(Sim::cellKey2(a, b)) > 0;
            float pl = propLum(vx, wy, vz);
            Rgba rim = shut ? Rgba{ 96, 210, 128, 255 } : Rgba{ 196, 62, 48, 255 };
            // a handwheel lying flat on a vertical pipe: rim, punched centre, hub
            put(out, cylinderEx({ vx, wy - 0.022f, vz }, { vx, wy + 0.022f, vz }, 0.21f, 0.21f, 12, lit(rim, pl)));
            put(out, cylinderEx({ vx, wy - 0.03f, vz }, { vx, wy + 0.03f, vz }, 0.125f, 0.125f, 12,
                                lit({ 84, 60, 44, 255 }, pl)));
            put(out, cylinderEx({ vx, wy - 0.05f, vz }, { vx, wy + 0.05f, vz }, 0.045f, 0.045f, 8,
                                lit({ 122, 96, 70, 255 }, pl)));
        }
    }
    for (int i = 0; i < Sim::MAXDOGS; i++) {   // the pack, low and wide against the red
        ActorSprite sp;
        if (!dogSprite(sim, i, sp)) continue;
        float ddx = sp.litAt.x - sim.px, ddz = sp.litAt.z - sim.pz;
        float dd = sqrtf(ddx * ddx + ddz * ddz);
        const LevelCfg &c = LEVELS[sim.level];
        float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
        float lum = lightAtCPU(sp.litAt.x, sp.litAt.y, sp.litAt.z, sim.blackoutCur,
                               c.ls, c.wallH - LIGHT_DROP, c.dead, c.lightMul, ambLum, 0, 0, 0, c.vary);
        if (sim.flashCur > 0.05f) {
            float d2 = dd * dd + 1e-4f;
            float cone = powf(fmaxf((ddx * sim.fwd.x + ddz * sim.fwd.z) / (dd + 1e-4f), 0.0f), 26.0f);
            lum = clampf(lum + sim.flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
        }
        if (flareInt > 0.01f) {
            float fx = sp.litAt.x - flarePos.x, fz = sp.litAt.z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fx * fx + fz * fz)), 0.0f, 1.0f);
        }
        lum = clampf(lum + irLum(sp.litAt.x, sp.litAt.y, sp.litAt.z), 0.0f, 1.0f);
        unsigned char l8 = cl8(40 + 215 * lum);
        unsigned char al = cl8(255 * clampf(expf(-dd * c.fogDen) * 1.6f, 0, 1) * sp.fade);
        spriteDraw(out, ShapeTex::Dog, sp, sp.frame0, { l8, l8, l8, cl8(al * (1.0f - sp.blend)) });
        spriteDraw(out, ShapeTex::Dog, sp, sp.frame1, { l8, l8, l8, cl8(al * sp.blend) });
    }
    for (const FlareProj &f : sim.litFlares) {   // each flare: hot core, orange halo, stub of a body
        if (!f.active) continue;
        // its own glow, not the point light's: that one belongs to whichever
        // flare leads, and would size every other halo wrongly
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
                               sim.ent.x, sim.ent.z, sim.entDarkCur, c.vary);   // it stands in its own pool of dead light
        if (sim.flashCur > 0.05f) {   // the torch picks it out of the dark
            float vx2 = sim.ent.x - sim.px, vz2 = sim.ent.z - sim.pz;
            float d2 = vx2 * vx2 + vz2 * vz2 + 1e-4f, dl = sqrtf(d2);
            float cone = powf(fmaxf((vx2 * sim.fwd.x + vz2 * sim.fwd.z) / dl, 0.0f), 26.0f);
            lum = clampf(lum + sim.flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
        }
        if (flareInt > 0.01f) {   // flare glow (or muzzle flash) reaches it too
            float fvx = sim.ent.x - flarePos.x, fvz = sim.ent.z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fvx * fvx + fvz * fvz)), 0.0f, 1.0f);
        }
        lum = clampf(lum + irLum(sp.litAt.x, sp.litAt.y, sp.litAt.z), 0.0f, 1.0f);
        float fogf = expf(-sim.entDist * c.fogDen);
        unsigned char lum8 = cl8(40 + 215 * lum);
        unsigned char al = cl8(255 * clampf(fogf * 1.6f, 0, 1) * sp.fade);
        ShapeTex t = sp.sheet == SpriteSheet::Partygoer ? ShapeTex::Partygoer
                   : sp.sheet == SpriteSheet::Clark ? ShapeTex::Clark : ShapeTex::Smiler;
        spriteDraw(out, t, sp, sp.frame0, { lum8, lum8, lum8, cl8(al * (1.0f - sp.blend)) });
        spriteDraw(out, t, sp, sp.frame1, { lum8, lum8, lum8, cl8(al * sp.blend) });
        if (sp.glow) {   // the Smiler's eyes and grin carry their own light
            unsigned char ga = cl8(255 * clampf(fogf * 2.2f, 0, 1) * sp.fade);
            spriteDraw(out, ShapeTex::SmilerGlow, sp, sp.frame0, { 255, 255, 255, cl8(ga * (1.0f - sp.blend)) });
            spriteDraw(out, ShapeTex::SmilerGlow, sp, sp.frame1, { 255, 255, 255, cl8(ga * sp.blend) });
        }
    }
    for (const Sim::Bullet &b : sim.bullets) {
        Vec3 tail = sub(b.pos, scale(b.direction, fminf(1.2f, dist(b.pos, b.tail))));
        put(out, cylinderEx(tail, b.pos, 0.006f, 0.009f, 5, { 255, 211, 126, 220 }));
        put(out, sphere(b.pos, 0.012f, { 255, 237, 187, 255 }));
    }
    if (sim.level == 1) {
        // Level 1's "low-hanging fog with no discernable source": soft, wide,
        // faint banks lying on the slab, drifting on a draught nobody can find.
        // One per floor cell on a hashed jitter, lit by the room at its own
        // spot. No depth writes, or each bank would cut a hole in the one
        // behind it; faded out right under you.
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

void heldDraws(const Sim &sim, const SimView &view, float now, std::vector<SceneDraw> &out) {
    switch (heldItem(sim)) {
    case Held::Can: put(out, ObjMesh::Can, xformOf(heldCan(sim, view))); break;   // both hands are busy: the deck goes away
    case Held::Deck: deckDraws(sim, xformOf(heldDeck(sim, view)), sim.deck.playing, now, out); break;
    case Held::Flare: put(out, ObjMesh::Flare, xformOf(heldWeapon(sim, view))); break;
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
    case ShapeTex::Dog: w = DOG_FRAME_W * DOG_FRAMES; h = DOG_FRAME_H; return;
    case ShapeTex::Clark: case ShapeTex::Smiler: case ShapeTex::SmilerGlow: case ShapeTex::Partygoer:
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
