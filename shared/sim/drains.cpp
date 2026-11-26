// The Poolrooms' drains: which runs when, its pull on a body in the water and
// its sound. Where they are is core's World::poolDrain; the mesher builds the
// grate and port/draw.cpp the bubbles over a running one.
#include "../core/fp_strict.h"
#include "sim.h"
#include "../core/hash.h"
#include <cmath>

float Sim::drainRun(int ci, int ck) const {
    float t = drainClock + (float)(ih(ci, ck, world.sseed() ^ 0xD4A3u) % 1000) * (DRAIN_CYCLE / 1000.0f);
    if (t >= DRAIN_CYCLE) t -= DRAIN_CYCLE;
    if (t >= DRAIN_ON) return 0;
    return clampf(t / DRAIN_SPIN, 0, 1) * clampf((DRAIN_ON - t) / DRAIN_SPIN, 0, 1);
}

void Sim::updateDrains(float dt) {
    if (drainEntry != levelEntries) {   // a new level or visit: the clock starts at arrival
        drainEntry = levelEntries;
        drainNear = {};
        drainClock = drainScanT = drainGurgleT = 0;
        drainGurgleVar = 0;
    }
    drainNear.pull = drainNear.grip = 0;
    if (level != 2 || inMenu) { drainNear.found = false; return; }
    drainClock += dt;
    if (drainClock >= DRAIN_CYCLE) drainClock -= DRAIN_CYCLE;

    drainScanT -= dt;
    if (drainScanT <= 0) {
        drainScanT = DRAIN_SCAN_GAP;
        const int a = cellOf(px), b = cellOf(pz), r = (int)(DRAIN_HEAR / CELL);
        float nearest = DRAIN_HEAR * DRAIN_HEAR;
        drainNear.found = false;
        for (int i = a - r; i <= a + r; i++)
            for (int k = b - r; k <= b + r; k++) {
                const float x = (i + 0.5f) * CELL, z = (k + 0.5f) * CELL;
                const float d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
                if (d2 >= nearest || !world.poolDrain(i, k)) continue;
                nearest = d2;
                drainNear.found = true;
                drainNear.ci = i; drainNear.ck = k; drainNear.x = x; drainNear.z = z;
                drainNear.floorY = world.floorY(i, k);
            }
    }
    if (!drainNear.found) { drainNear.run = 0; return; }

    const float dx = drainNear.x - px, dz = drainNear.z - pz, d = portableHypot(dx, dz);
    const float was = drainNear.run;
    drainNear.dist = d;
    drainNear.run = drainRun(drainNear.ci, drainNear.ck);
    const bool inWater = world.poolAt(cellOf(px), cellOf(pz)) && py < WATER_Y - 0.08f;
    if (inWater && deathT <= 0 && drainNear.run > 0 && d < DRAIN_REACH) {
        // Inside 0.3 m the pull fades, so a body over the grate settles there.
        drainNear.grip = drainNear.run * (1 - d / DRAIN_REACH);
        drainNear.pull = drainNear.grip * fminf(d / 0.3f, 1.0f);
        drainNear.dirx = d > 1e-4f ? dx / d : 0;
        drainNear.dirz = d > 1e-4f ? dz / d : 0;
    }

    // Sound: the valve as it opens, then the rush every DRAIN_GURGLE_GAP.
    const float near = (1 - d / DRAIN_HEAR) * (1 - d / DRAIN_HEAR);
    const float inv = d > 1e-4f ? 1 / d : 0;
    const float pan = clampf((dx * r2x + dz * r2z) * inv, -1.0f, 1.0f);
    if (was <= 0 && drainNear.run > 0 && deathT <= 0) {
        play(Sfx::DrainStart).atPan(pan).atVolume(0.25f + 0.75f * near);
        drainGurgleT = 0.6f;
    }
    drainGurgleT -= dt;
    if (drainNear.run > 0.05f && drainGurgleT <= 0 && deathT <= 0) {
        play(Sfx::DrainRun, drainGurgleVar).atPan(pan).atVolume(clampf(drainNear.run * (0.15f + 0.85f * near), 0, 1));
        drainGurgleVar ^= 1;
        drainGurgleT = DRAIN_GURGLE_GAP;
    }
}
