#include "../core/fp_strict.h"
#include "sprites.h"
#include <cmath>

void gaitFrames(float phase, int frames, int &f0, int &f1, float &t) {
    phase -= floorf(phase);
    float u = phase * frames;
    f0 = (int)u % frames;
    f1 = (f0 + 1) % frames;
    t = u - floorf(u);
}

bool hunterSprite(const Sim &sim, ActorSprite &out) {
    const Entity &e = sim.ent;
    if (e.st == EState::Hidden || sim.entDist >= 45) return false;
    out = {};
    // LEVEL FUN has its own resident, Level 0 is Pirate Clark's, the rest are a Smiler's
    out.sheet = sim.level == LEVEL_FUN ? SpriteSheet::Partygoer : sim.clarkLevel() ? SpriteSheet::Clark : SpriteSheet::Smiler;
    out.glow = out.sheet == SpriteSheet::Smiler;
    float sink = 0;
    if (e.st == EState::Die) {   // crumples into the carpet
        float t = clampf(e.life / 1.2f, 0, 1);
        sink = 1.1f * t * t; out.fade = 1.0f - t;
    }
    // ent.gait counts strides (an integer is a foot landing) and a walk cycle
    // is two of them, hence the halving. It is also what fires his footfalls,
    // so the foot plants on the sound.
    float gph = e.gait * 0.5f;
    gaitFrames(gph, ENT_FRAMES, out.frame0, out.frame1, out.blend);
    // A walk rises and falls once per step, lowest as a foot lands: |sin| of
    // the phase the legs run on.
    float bob = 0.032f * fabsf(sinf(gph * TAU));
    // The head turns with ent.gaze, the timer that turns a stalk into a chase
    // at 1.6 s, so the turn is the warning. In any other state he faces the
    // camera.
    float look = (e.st == EState::Stalk) ? clampf((float)e.gaze / 1.45f, 0, 1) : 1.0f;
    out.row = look < 0.34f ? ENT_ROW_AWAY : (look < 0.72f ? ENT_ROW_HALF : ENT_ROW_FACE);
    // Lean into the direction of travel; only sideways velocity relative to the
    // view can show on a billboard. Below walking pace he stays upright, so he
    // does not flick between rows.
    float side = e.vx * sim.r2x + e.vz * sim.r2z;
    if (e.st == EState::Chase && fabsf(side) > 1.4f)
        out.row = (side < 0) ? ENT_ROW_LEAN_L : ENT_ROW_LEAN_R;
    out.centre = { e.x, e.dispY + 0.98f - sink + bob, e.z };
    out.w = 0.98f; out.h = 1.96f;
    out.litAt = { e.x, e.dispY + 0.95f, e.z };
    return true;
}

bool usherSprite(const Sim &sim, ActorSprite &out) {
    const Usher &u = sim.usher;
    if (u.st == UState::Gone || sim.usherDist >= 45) return false;
    out = {};
    out.sheet = SpriteSheet::Usher;
    float sink = 0;
    if (u.st == UState::Down) {   // folds at the knees and fades
        float t = clampf(u.life / 1.5f, 0, 1);
        sink = 1.2f * t * t; out.fade = 1.0f - t;
    }
    float gph = u.gait * 0.5f;
    gaitFrames(gph, ENT_FRAMES, out.frame0, out.frame1, out.blend);
    float bob = 0.026f * fabsf(sinf(gph * TAU));
    float cx = sim.px - u.x, cz = sim.pz - u.z, cl = sqrtf(cx * cx + cz * cz) + 1e-4f;
    float facing = (cosf(u.beamYaw) * cx + sinf(u.beamYaw) * cz) / cl;
    out.row = facing > 0.55f ? ENT_ROW_FACE : facing > -0.2f ? ENT_ROW_HALF : ENT_ROW_AWAY;
    out.centre = { u.x, u.dispY + 1.06f - sink + bob, u.z };
    out.w = 1.06f; out.h = 2.12f;
    out.litAt = { u.x, u.dispY + 1.0f, u.z };
    return true;
}

// Held at chest height a little ahead of it, tipped 10 degrees down.
bool usherTorch(const Sim &sim, Vec3 &at, Vec3 &dir) {
    const Usher &u = sim.usher;
    if ((u.st != UState::Patrol && u.st != UState::Spot) || sim.usherDist >= 45) return false;
    float c = cosf(u.beamYaw), s = sinf(u.beamYaw);
    at = { u.x + c * 0.30f, u.dispY + Sim::USHER_TORCH_Y, u.z + s * 0.30f };
    dir = { c * 0.985f, -0.174f, s * 0.985f };
    return true;
}

std::vector<ActorSprite> actorSprites(const Sim &sim) {
    std::vector<ActorSprite> out;
    ActorSprite s;
    if (hunterSprite(sim, s)) out.push_back(s);
    if (usherSprite(sim, s)) out.push_back(s);
    return out;
}
