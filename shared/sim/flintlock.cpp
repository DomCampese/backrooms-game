// Pirate Clark's flintlock pistol on Level 0 (PISTOL_* in sim.h).
#include "../core/fp_strict.h"
#include "sim.h"
#include "../core/portable_math.h"
#include <cmath>

Vec3 Sim::pistolMuzzle() const {
    float dx = px - ent.x, dz = pz - ent.z, d = portableHypot(dx, dz);
    float ux = d > 0.01f ? dx / d : 1.0f, uz = d > 0.01f ? dz / d : 0.0f;
    // His right hand, at shoulder height, held out toward the player.
    return { ent.x + ux * 0.45f - uz * 0.18f, ent.dispY + 1.42f, ent.z + uz * 0.45f + ux * 0.18f };
}

void Sim::updateFlintlock(float dt, double now) {
    if (pistolEntry != levelEntries) { pistol = Flintlock{}; pistolEntry = levelEntries; }
    pistol.flashT = fmaxf(0.0f, pistol.flashT - dt);
    pistol.smokeT = fmaxf(0.0f, pistol.smokeT - dt);
    if (!clarkLevel() || ent.st != EState::Chase || deathT > 0) {
        pistol.aiming = false;
        pistol.recoverT = 0;
        pistol.readyT = fmaxf(pistol.readyT, PISTOL_FIRST);
        return;
    }
    const float dx = px - ent.x, dz = pz - ent.z, d = portableHypot(dx, dz);
    auto inSight = [&](float farM) {
        return !hidden && d <= farM && fabsf(ent.dispY - py) < 1.4f && world.lineOfSight(ent.x, ent.z, px, pz);
    };
    // + is to the player's right, as for his footsteps.
    const float inv = d > 0.01f ? 1.0f / d : 0.0f;
    const float pan = clampf((-dx * inv) * r2x + (-dz * inv) * r2z, -1.0f, 1.0f);
    if (pistol.recoverT > 0) {   // he stands after the shot, in his smoke
        pistol.recoverT -= dt;
        ent.x = pistol.holdX; ent.z = pistol.holdZ;
        ent.lunge = 0;
    }
    if (pistol.aiming) {
        ent.x = pistol.holdX; ent.z = pistol.holdZ;   // he stands to aim
        ent.lunge = 0;
        if (!inSight(PISTOL_FAR + 2.0f)) {   // out of his sight: he lowers it
            pistol.aiming = false;
            pistol.readyT = PISTOL_REAIM;
            return;
        }
        pistol.aimT -= dt;
        if (pistol.aimT > 0) return;
        pistol.aiming = false;
        pistol.readyT = PISTOL_RELOAD;
        pistol.recoverT = PISTOL_RECOVER;
        pistol.flashT = PISTOL_FLASH;
        pistol.smokeT = PISTOL_SMOKE;
        pistol.muzzle = pistolMuzzle();
        play(Sfx::Shot).atPitch(0.62f).atPan(pan).atVolume(clampf(1.6f / (1.0f + 0.01f * d * d), 0.3f, 1.0f));
        const bool hit = d < PISTOL_SURE || portableHypot(velx, velz) < PISTOL_DODGE;
        if (hit && hurtT <= 0) {
            hurtPlayer(now, PISTOL_HIT, hunterName(), ent.x, ent.z);
        } else {
            deckNoteT = 2.2f; deckNote = "the ball goes past you.";
            fear = fmaxf(fear, 0.7f);
        }
        return;
    }
    pistol.readyT = fmaxf(0.0f, pistol.readyT - dt);
    if (pistol.readyT > 0 || ent.lunge > 0 || ent.stagger > 0) return;
    if (d >= PISTOL_NEAR && inSight(PISTOL_FAR)) {
        pistol.aiming = true;
        pistol.aimT = PISTOL_AIM;
        pistol.holdX = ent.x; pistol.holdZ = ent.z;
        play(Sfx::Click).atPitch(0.45f).atPan(pan).atVolume(clampf(1.4f / (1.0f + 0.02f * d * d), 0.25f, 0.9f));
        fear = fmaxf(fear, 0.6f);
    }
}
