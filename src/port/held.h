#pragma once
// What the player holds and where: the revolver or a flare in the corner of the
// view, and the revolver's pose. render.cpp draws from this, and so does a
// port, so the gun sits in the same place in both. Metres, the sim's storey
// frame.
#include "../sim/sim.h"
#include "view.h"

enum class Held { None, Can, Deck, Revolver, Flare };

// Which viewmodel render.cpp draws: the can while drinking, else the weapon in
// hand (the deck only while carried, a flare only while any are left).
Held heldItem(const Sim &sim);

// A model's own axes in the world: its +x along `right`, +y along `up`, +z
// along `forward`, each times `scale`, its origin at `pos`. The three are a
// left-handed set in the sim's right-handed frame, as render.cpp has always
// drawn the gun: the reload was reversed at import to open left for this.
struct HeldFrame {
    Vec3 pos, right, up, forward;
    float scale;
};

// The revolver or a flare in hand. The revolver's farthest vertex stays inside
// 0.33 m of the eye, through recoil and reload, so no wall can cut it.
HeldFrame heldWeapon(const Sim &sim, const SimView &view);

enum class RevolverClip { Idle, Reload, Shoot };

// The clip to show and how far through it (0..1), and how many sixths of a
// turn the drum stands from where the clips leave it: the prepared Shoot clip
// is one cycle, so the drum's index across shots is added here.
struct RevolverPose {
    RevolverClip clip;
    float progress;
    float drumTurns;
};

RevolverPose revolverPose(float reloadT, float gunCd, int ammo);

// Points in the frame of the GLB's DEF_RevolverHandle joint, in metres: the
// muzzle, and the drum's pivot. The drum turns about the handle's +z.
constexpr Vec3 REVOLVER_MUZZLE = { 0.0f, 0.038805f, 0.237063f };
constexpr Vec3 REVOLVER_DRUM_PIVOT = { 0.0f, 0.038805f, 0.033062f };
