#include "../core/fp_strict.h"
#include "scene.h"
#include <cmath>

namespace {

// The spins the raylib renderer gives cans and crates, so they are not clones.
// These are raylib angles: MatrixRotateY turns +x toward -z, the other way from
// a SceneItem's yaw, so the scene stores them negated.
float canYaw(int a, int b) { return (float)(ih(a, b, 0x0CA9u) & 1023) / 1023.0f * TAU; }
float crateYaw(int a, int b) {
    return (float)(ih(a, b, 0xC2A7u) & 3) * 1.5707963f + (((ih(a, b, 0xC2A8u) & 255) / 255.0f) - 0.5f) * 0.4f;
}

void pickups(Sim &sim, int ci, int ck, std::vector<SceneItem> &out) {
    const int reach = SCENE_PICKUP_CELLS;
    for (int dx = -reach; dx <= reach; dx++)
        for (int dz = -reach; dz <= reach; dz++) {
            int a = ci + dx, b = ck + dz;
            if (sim.taken.count(sim.cellKey(a, b))) continue;
            Pickup kind = sim.pickupAt(a, b);
            if (kind == Pickup::None) continue;
            Vec2 spot = sim.pickupSpot(a, b);
            SceneItem it;
            it.pos = { spot.x, sim.world.floorY(a, b), spot.y };
            it.gi = a, it.gk = b;
            switch (kind) {
            case Pickup::AlmondWater: {
                float shelf = sim.bottleShelfY(a, b);
                it.kind = SceneKind::AlmondWater;
                it.pos.y += shelf >= 0 ? shelf : 0.0f;
                it.yaw = -canYaw(a, b);
                break;
            }
            case Pickup::Doubloon: it.kind = SceneKind::Doubloon; break;
            case Pickup::Battery:  it.kind = SceneKind::Battery; break;
            case Pickup::Tape:     it.kind = SceneKind::Tape; break;
            case Pickup::Key:      it.kind = SceneKind::Key; break;
            case Pickup::None:     continue;
            }
            out.push_back(it);
        }
}

void crates(Sim &sim, int ci, int ck, std::vector<SceneItem> &out) {
    if (sim.level != 1) return;
    const int reach = SCENE_CRATE_CELLS;
    for (int dx = -reach; dx <= reach; dx++)
        for (int dz = -reach; dz <= reach; dz++) {
            int a = ci + dx, b = ck + dz;
            if (!sim.crateAt(a, b)) continue;
            SceneItem it;
            it.kind = sim.cratesOpened.count(Sim::cellKey2(a, b)) ? SceneKind::CrateOpen : SceneKind::Crate;
            it.pos = { a * CELL + 1.0f, sim.world.floorY(a, b), b * CELL + 1.0f };
            it.yaw = -crateYaw(a, b);
            it.gi = a, it.gk = b;
            out.push_back(it);
        }
}

void balloons(Sim &sim, int ci, int ck, std::vector<SceneItem> &out) {
    if (sim.level != 4) return;
    const int reach = SCENE_BALLOON_CELLS;
    for (int dx = -reach; dx <= reach; dx++)
        for (int dz = -reach; dz <= reach; dz++) {
            int a = ci + dx, b = ck + dz;
            SceneItem it;
            it.kind = SceneKind::Balloon;
            it.gi = a, it.gk = b;
            if (!sim.balloonAt(a, b, it.pos)) continue;
            it.variant = (uint8_t)((ih(a, b, sim.pickupSalt() ^ 0xBA11u) >> 10) % PARTY_COLOURS);
            out.push_back(it);
        }
}

void tableBunches(Sim &sim, int ci, int ck, std::vector<SceneItem> &out) {
    if (sim.level != 4) return;
    const int reach = SCENE_BUNCH_CELLS;
    for (int dx = -reach; dx <= reach; dx++)
        for (int dz = -reach; dz <= reach; dz++) {
            int a = ci + dx, b = ck + dz;
            if (sim.poppedTableBunches.count(Sim::cellKey2(a, b))) continue;
            Vec3 pos[4], tie;
            uint8_t colours[4];
            int n = sim.tableBalloonBunch(a, b, pos, colours, tie);
            for (int k = 0; k < n; k++) {
                SceneItem it;
                it.kind = SceneKind::Balloon;
                it.pos = pos[k];
                it.variant = colours[k];
                it.gi = a, it.gk = b;
                it.index = (uint8_t)(k + 1);
                it.anchor = tie;
                out.push_back(it);
            }
        }
}

float yawOf(float dx, float dz) { return atan2f(dz, dx); }

}  // namespace

std::vector<SceneItem> simScene(Sim &sim) {
    std::vector<SceneItem> out;
    const int ci = cellOf(sim.px), ck = cellOf(sim.pz);
    crates(sim, ci, ck, out);
    pickups(sim, ci, ck, out);
    if (!sim.deck.carried) {
        SceneItem it;
        it.kind = SceneKind::Deck;
        it.pos = { sim.deck.x, sim.deck.y, sim.deck.z };
        it.yaw = -sim.deck.yaw;   // a raylib angle, as above
        it.variant = sim.deck.playing ? 1 : 0;
        out.push_back(it);
    }
    for (const Vec3 &c : sim.coinsWorld) {
        SceneItem it;
        it.kind = SceneKind::Coin;
        it.pos = c;
        out.push_back(it);
    }
    balloons(sim, ci, ck, out);
    tableBunches(sim, ci, ck, out);
    if (sim.level == 4)
        for (const Sim::Confetti &c : sim.confetti) {
            SceneItem it;
            it.kind = SceneKind::Confetti;
            it.pos = c.pos;
            it.variant = c.colour;
            it.amount = c.life;
            out.push_back(it);
        }
    // Chalk stays on the floor it was drawn on; a storey away it is where it
    // lies, further not at all.
    for (const ChalkMark &m : sim.chalk[sim.level]) {
        int ds = m.storey - sim.world.storey;
        if (ds < -1 || ds > 1) continue;
        SceneItem it;
        it.kind = SceneKind::Chalk;
        it.pos = { m.pos.x, m.pos.y + ds * sim.world.storeyH, m.pos.z };
        if (fabsf(it.pos.x - sim.px) > SCENE_CHALK_M || fabsf(it.pos.z - sim.pz) > SCENE_CHALK_M) continue;
        it.yaw = m.yaw;
        it.variant = m.mine ? 1 : 0;
        out.push_back(it);
    }
    for (const FlareProj &f : sim.litFlares) {
        if (!f.active) continue;
        SceneItem it;
        it.kind = SceneKind::Flare;
        it.pos = { f.x, f.y, f.z };
        it.amount = clampf(f.burn / Sim::FLAREBURN, 0.0f, 1.0f);
        out.push_back(it);
    }
    for (const Sim::BulletImpact &b : sim.bulletImpacts) {
        SceneItem it;
        it.kind = SceneKind::Impact;
        it.pos = b.pos;
        it.normal = b.normal;
        it.amount = b.life;
        out.push_back(it);
    }
    if (sim.ent.st != EState::Hidden && sim.entDist < SCENE_HUNTER_M) {
        SceneItem it;
        it.kind = SceneKind::Hunter;
        it.pos = { sim.ent.x, sim.ent.dispY, sim.ent.z };
        // Facing where it is going, or else at you.
        bool moving = sim.ent.vx * sim.ent.vx + sim.ent.vz * sim.ent.vz > 0.01f;
        it.yaw = moving ? yawOf(sim.ent.vx, sim.ent.vz) : yawOf(sim.px - sim.ent.x, sim.pz - sim.ent.z);
        it.variant = (uint8_t)sim.ent.st;
        out.push_back(it);
    }
    for (const Dog &d : sim.dogs) {
        float dx = d.x - sim.px, dz = d.z - sim.pz;
        if (d.st == DState::Gone || sqrtf(dx * dx + dz * dz) > SCENE_DOG_M) continue;
        SceneItem it;
        it.kind = SceneKind::Dog;
        it.pos = { d.x, d.dispY, d.z };
        it.yaw = yawOf(d.wpx - d.x, d.wpz - d.z);
        it.variant = (uint8_t)d.st;
        out.push_back(it);
    }
    return out;
}
