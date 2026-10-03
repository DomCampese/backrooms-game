#include "../core/fp_strict.h"
#include "held.h"
#include "../sim/sim_math.h"
#include "../sim/weapon_timing.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI_F = 3.14159265358979323846f;   // raylib's PI
constexpr float DEG2RAD_F = PI_F / 180.0f;         // raylib's DEG2RAD

float ease(float t) {
    t = clampf(t, 0, 1);
    return t * t * (3.0f - 2.0f * t);
}
}

Held heldItem(const Sim &sim) {
    if (sim.inMenu) return Held::None;
    if (sim.drinkT > 0) return Held::Can;
    if (sim.weapon == WEAPON_DECK) return sim.deck.carried ? Held::Deck : Held::None;
    if (sim.weapon == WEAPON_REVOLVER) return Held::Revolver;
    if (sim.weapon == WEAPON_FLARE && sim.flares > 0) return Held::Flare;
    return Held::None;
}

// Moved from render.cpp's drawHeldWeapon, in raymath's operation order.
HeldFrame heldWeapon(const Sim &sim, const SimView &view) {
    const bool gun = sim.weapon == WEAPON_REVOLVER;
    Vec3 right{ sim.r2x, 0, sim.r2z };
    Vec3 up = normalize(cross(right, sim.fwd));
    float dip = gun && sim.reloadT > 0 ? sinf(clampf(1 - sim.reloadT / RELOAD_TIME, 0, 1) * PI_F) : 0;
    float kick = gun ? sim.recoil : 0;
    float aim = gun ? sim.aimBlend * sim.aimBlend * (3 - 2 * sim.aimBlend) : 0;
    // The front blade is at GLB Z/Y (0.23706, 0.07560). Keep its tip on the
    // camera ray, with the eye just clearing the rear frame rib. Aligning the
    // rib top exactly with the blade hides the blade behind this model's solid
    // rear face.
    float tilt = gun ? 0.06f - 0.063f * aim + kick * 0.30f - dip * 0.65f : 0.85f;
    Vec3 forward = normalize(add(sim.fwd, add(scale(right, -0.20f * (1 - aim)), scale(up, tilt))));
    Vec3 axisUp = normalize(cross(right, forward));
    Vec3 axisRight = normalize(cross(forward, axisUp));
    float sway = sinf(sim.bobPhase * PI_F) * 0.003f * sim.bobAmt * (1 - aim);
    Vec3 pos = add(view.eye, add(scale(sim.fwd, 0.155f - kick * 0.012f),
        add(scale(right, 0.077f * (1 - aim) + sway), scale(up, -0.072f + 0.03605f * aim - dip * 0.024f))));
    return { pos, axisRight, axisUp, forward, gun ? 0.48f : 0.64f };
}

// Moved from render.cpp's drawDrinkCan, in raymath's operation order. Inside
// 0.34 m of the eye, like every viewmodel.
HeldFrame heldCan(const Sim &sim, const SimView &view) {
    float el = Sim::DRINK_TIME - sim.drinkT;
    float up  = ease(el / 0.42f) * (1.0f - ease((el - 1.36f) / 0.39f));   // in, then away
    float tip = ease((el - 0.34f) / 0.34f) * (1.0f - ease((el - 1.30f) / 0.34f));
    float bob = 0;
    for (int g = 0; g < 3; g++) {
        float gt = el - (0.40f + g * 0.36f);
        if (gt > 0 && gt < 0.26f) bob += sinf(gt / 0.26f * 3.14159f);
    }
    Vec3 F = sim.fwd;
    Vec3 Rt = { sim.r2x, 0, sim.r2z };
    Vec3 Up = normalize(cross(Rt, F));

    // Below the centre of the view, the lid brought toward the mouth rather
    // than the label lifted across the eyes.
    float dist = 0.265f - tip * 0.055f;
    float side = 0.112f - tip * 0.028f;
    float vert = -0.255f + up * 0.145f + tip * 0.018f + bob * 0.003f;
    float bx4 = sinf(sim.bobPhase * 3.14159f) * 0.008f * sim.bobAmt;
    Vec3 pos = add(view.eye, add(scale(F, dist), add(scale(Rt, side + bx4), scale(Up, vert))));

    const float SCALE = 0.70f;
    // Pitch about the can's right axis brings the lid to the mouth; a little
    // roll keeps the barrel and label in view. Past 38 degrees the barrel hides
    // behind the lid.
    float pitch2 = (32.0f * DEG2RAD_F) * tip + (4.0f * DEG2RAD_F) * bob;
    float roll2  = (8.0f * DEG2RAD_F) * tip + (2.0f * DEG2RAD_F) * bob;
    Vec3 x0 = Rt, y0 = Up, z0 = negate(F);   // label toward the eye
    Vec3 y1 = add(scale(y0, cosf(pitch2)), scale(z0, sinf(pitch2)));
    Vec3 z1 = add(scale(y0, -sinf(pitch2)), scale(z0, cosf(pitch2)));
    Vec3 tiltX = add(scale(x0, cosf(roll2)), scale(y1, sinf(roll2)));
    Vec3 tiltY = add(scale(x0, -sinf(roll2)), scale(y1, cosf(roll2)));

    // Turn it about its middle: 0.061 m is half the can.
    pos = sub(pos, scale(tiltY, 0.061f * SCALE));
    return { pos, tiltX, tiltY, z1, SCALE };
}

// Moved from render.cpp's drawHeldDeck, in raymath's operation order.
HeldFrame heldDeck(const Sim &sim, const SimView &view) {
    Vec3 F = sim.fwd;
    Vec3 Rt = { sim.r2x, 0, sim.r2z };
    Vec3 Up = normalize(cross(Rt, F));

    float sway = sinf(sim.bobPhase * 3.14159f) * 0.007f * sim.bobAmt;
    float rise = sinf(sim.bobPhase * 1.57079f) * 0.004f * sim.bobAmt;
    Vec3 pos = add(view.eye, add(scale(F, 0.220f), add(scale(Rt, 0.125f + sway), scale(Up, -0.128f + rise))));

    const float SCALE = 0.62f;
    // The top tipped toward the eye so the bay and reels show; held flat, only
    // the front edge does.
    const float pitch2 = 52.0f * DEG2RAD_F, yaw2 = -26.0f * DEG2RAD_F;
    Vec3 x0 = Rt, y0 = Up, z0 = negate(F);   // front toward the eye
    Vec3 y1 = add(scale(y0, cosf(pitch2)), scale(z0, sinf(pitch2)));
    Vec3 z1 = add(scale(y0, -sinf(pitch2)), scale(z0, cosf(pitch2)));
    Vec3 ax = add(scale(x0, cosf(yaw2)), scale(z1, -sinf(yaw2)));
    Vec3 az = add(scale(x0, sinf(yaw2)), scale(z1, cosf(yaw2)));

    // Turn it about its middle, not its underside.
    pos = sub(pos, scale(y1, 0.029f * SCALE));
    return { pos, ax, y1, az, SCALE };
}

RevolverPose revolverPose(float reloadT, float gunCd, int ammo) {
    RevolverPose p{ RevolverClip::Idle, 0, 0 };
    if (reloadT > 0) {
        p.clip = RevolverClip::Reload;
        p.progress = std::clamp(1 - reloadT / RELOAD_TIME, 0.0f, 1.0f);
    } else if (gunCd > 0) {
        p.clip = RevolverClip::Shoot;
        p.progress = std::clamp(1 - gunCd / SHOT_INTERVAL, 0.0f, 1.0f);
    }
    // Blended back to the authored reload's starting index early in the reload.
    p.drumTurns = 6 - std::clamp(ammo, 0, 6) - (p.clip == RevolverClip::Shoot ? 1 : 0);
    if (p.clip == RevolverClip::Reload) p.drumTurns *= 1 - std::min(p.progress * (1.1666666f / .15f), 1.0f);
    return p;
}
