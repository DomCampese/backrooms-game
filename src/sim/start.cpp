#include "../core/fp_strict.h"
#include "start.h"

void simBegin(Sim &sim, const SimStart &s, double now) {
    sim.noBlackout = s.noBlackout;
    sim.fixedSeed = s.fixedSeed;
    sim.keepRecords = s.keepRecords;
    World &world = sim.world;
    world.seed = s.seed;
    world.exitTest = s.exitTest;
    world.manilaTest = s.manilaTest;
    sim.grng = Rng(hash64(world.seed ^ 0xABCDEF));

    Vec2 sp = world.findOpenSpot(s.placed ? s.x : 15, s.placed ? s.z : 15);
    sim.px = sp.x; sim.pz = sp.y;
    if (s.placed) sim.yaw = s.yaw;
    if (s.pitched) sim.pitch = s.pitch;

    // The order of these draws on grng is part of the seed's contract.
    sim.nextFlareRegen = now + Sim::FLARE_REGEN;
    sim.nextBlackout = sim.blackoutIn(0, 40, 60);
    sim.runStart = now;
    sim.fov = s.fov;
    sim.nextWhisper = sim.runStart + 45 + sim.grng.f01() * 60;
    sim.best = s.best;
}

void simPlace(Sim &sim, const SimStart &s) {
    World &world = sim.world;
    // A position means the same on every storey: the one you stand on is at y = 0.
    if (s.storeyed && world.storeyH > 0.0f) {
        world.setStorey(s.storey);
        Vec2 sp = world.findOpenSpot(sim.px, sim.pz);
        sim.px = sp.x; sim.pz = sp.y;
    }
    if (s.flash) { sim.flashOn = true; sim.flashCur = 1.0f; }
    sim.inMenu = s.menu;
}
