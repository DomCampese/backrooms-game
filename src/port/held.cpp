#include "../core/fp_strict.h"
#include "held.h"
#include "../sim/sim_math.h"
#include "../sim/weapon_timing.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI_F = 3.14159265358979323846f;   // raylib's PI
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
