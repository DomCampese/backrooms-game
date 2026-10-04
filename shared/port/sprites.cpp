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

bool dogSprite(const Sim &sim, int i, ActorSprite &out) {
    const Dog &d = sim.dogs[i];
    if (d.st == DState::Gone) return false;
    float ddx = d.x - sim.px, ddz = d.z - sim.pz;
    if (sqrtf(ddx * ddx + ddz * ddz) > 42) return false;
    out = {};
    out.sheet = SpriteSheet::Dog;
    // shoulder height ~0.75 m, and a long body: drawn wide, not tall
    out.centre = { d.x, d.dispY + 0.46f, d.z };
    out.w = 1.45f; out.h = 0.97f;
    gaitFrames(d.gait / (Sim::DOG_STRIDE * 2.0f), DOG_FRAMES, out.frame0, out.frame1, out.blend);
    out.fade = d.st == DState::Yelp ? clampf(1.0f - d.life / 2.6f, 0, 1) : 1.0f;
    out.litAt = { d.x, d.dispY + 0.5f, d.z };
    return true;
}

bool hunterSprite(const Sim &sim, ActorSprite &out) {
    const Entity &e = sim.ent;
    if (e.st == EState::Hidden || sim.entDist >= 45) return false;
    out = {};
    // LEVEL FUN has its own resident, Level 0 is Pirate Clark's, the rest are a Smiler's
    out.sheet = sim.level == 4 ? SpriteSheet::Partygoer : sim.clarkLevel() ? SpriteSheet::Clark : SpriteSheet::Smiler;
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
    // His head comes round with ent.gaze, the timer that tips a stalk into a
    // chase at 1.6 s, so the turn is the warning. In any other state he is
    // already looking at you.
    float look = (e.st == EState::Stalk) ? clampf((float)e.gaze / 1.45f, 0, 1) : 1.0f;
    out.row = look < 0.34f ? ENT_ROW_AWAY : (look < 0.72f ? ENT_ROW_HALF : ENT_ROW_FACE);
    // He tips into where he is going: only the velocity across your view can
    // show on a billboard. Below a walking pace he stays upright, so he does
    // not twitch between rows while shuffling.
    float side = e.vx * sim.r2x + e.vz * sim.r2z;
    if (e.st == EState::Chase && fabsf(side) > 1.4f)
        out.row = (side < 0) ? ENT_ROW_LEAN_L : ENT_ROW_LEAN_R;
    out.centre = { e.x, e.dispY + 0.98f - sink + bob, e.z };
    out.w = 0.98f; out.h = 1.96f;
    out.litAt = { e.x, e.dispY + 0.95f, e.z };
    return true;
}

std::vector<ActorSprite> actorSprites(const Sim &sim) {
    std::vector<ActorSprite> out;
    ActorSprite s;
    for (int i = 0; i < Sim::MAXDOGS; i++)
        if (dogSprite(sim, i, s)) out.push_back(s);
    if (hunterSprite(sim, s)) out.push_back(s);
    return out;
}
