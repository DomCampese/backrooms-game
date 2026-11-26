// Level 1's lifts: calling one, the wait, and the ride to another lift.
// Where lifts are is core's liftDoor, which the layout also builds from.
#include "../core/fp_strict.h"
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <vector>

// The point a body stands at to use a lift: on the floor 0.7 m out from the
// middle of its doors.
static Vec2 liftFront(const LiftDoor &d, float out) {
    return { d.pos.x, d.pos.z + d.normal.z * out };
}

bool Sim::liftInReach() {
    if (level != 1 || lift.state != LiftState::Idle) return false;
    const int ci = cellOf(px), ck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 2; dz++) {   // the doors are on a cell's north edge: look one cell further south
            LiftDoor d;
            if (!liftDoor(world, ci + dx, ck + dz, d)) continue;
            const Vec2 f = liftFront(d, 0.7f);
            // On the side the doors face, within reach of the front.
            if ((pz - d.pos.z) * d.normal.z < 0.1f) continue;
            if (portableHypot(px - f.x, pz - f.y) < LIFT_REACH) { lift.door = d; return true; }
        }
    return false;
}

void Sim::callLift() {
    lift.state = LiftState::Called;
    lift.t = LIFT_WAIT;
    play(Sfx::LiftCall).atVolume(0.8f);
    deckNoteT = 2.6f;
    deckNote = "the call button lights. something is coming down the shaft.";
}

// Another lift LIFT_MIN..LIFT_MAX cells from the player, nearest first, with
// open floor in front of it. The hash is cheap; only candidates in distance
// order are checked against the walls, so few chunks are generated.
bool Sim::findLiftDest(LiftDoor &out) {
    const int ci = cellOf(px), ck = cellOf(pz);
    const unsigned seed = world.sseed();
    std::vector<std::pair<int, std::pair<int, int>>> hits;
    for (int a = ci - LIFT_MAX; a <= ci + LIFT_MAX; a++)
        for (int b = ck - LIFT_MAX; b <= ck + LIFT_MAX; b++) {
            const int d2 = (a - ci) * (a - ci) + (b - ck) * (b - ck);
            if (d2 < LIFT_MIN * LIFT_MIN || d2 > LIFT_MAX * LIFT_MAX || !liftHash(a, b, seed)) continue;
            hits.push_back({ d2, { a, b } });
        }
    std::sort(hits.begin(), hits.end());
    for (const auto &h : hits) {
        LiftDoor d;
        if (!liftDoor(world, h.second.first, h.second.second, d)) continue;
        if (world.propAt(d.ci, d.ck) != PROP_NONE || world.pillarAt(d.ci, d.ck) || crateAt(d.ci, d.ck)) continue;
        out = d;
        return true;
    }
    return false;
}

void Sim::updateLift(float dt, double now) {
    if (liftEntry != levelEntries) {   // a new level or visit
        liftEntry = levelEntries;
        lift = Lift{};
        liftNoiseT = liftMissedT = 0;
    }
    liftNoiseT = fmaxf(0, liftNoiseT - dt);
    liftMissedT = fmaxf(0, liftMissedT - dt);
    liftNear = !inMenu && deathT <= 0 && liftInReach();
    if (lift.state == LiftState::Idle || level != 1) return;
    lift.t -= dt;
    if (lift.state == LiftState::Called) {
        if (lift.t > 0) return;
        const Vec2 f = liftFront(lift.door, 0.7f);
        const float dx = f.x - px, dz = f.y - pz, d = portableHypot(dx, dz);
        const float inv = d > 0.01f ? 1.0f / d : 0.0f;
        play(Sfx::LiftDing).atPan(clampf((dx * inv) * r2x + (dz * inv) * r2z, -1.0f, 1.0f))
            .atVolume(clampf(1.0f - d / 40.0f, 0.15f, 1.0f));
        liftNoiseT = LIFT_NOISE_TIME;
        liftNoiseX = f.x; liftNoiseZ = f.y;
        LiftDoor dest;
        if (d > LIFT_STAY || deathT > 0 || !findLiftDest(dest)) {
            lift.state = LiftState::Idle;
            liftMissedT = 2.5f;
            return;
        }
        lift.dest = dest;
        lift.state = LiftState::Riding;
        lift.t = LIFT_RIDE;
        // The doors shut on it: whether it was out or not, it comes back no
        // sooner than LIFT_LOSE. Testing only a hunter that was out let one
        // sent away for a shorter time (a lit Smiler, #224) come straight back.
        ent.st = EState::Hidden;
        ent.nextSpawn = fmax(ent.nextSpawn, now + LIFT_LOSE);
        return;
    }
    // Riding: held in front of the doors, in the dark, until they open elsewhere.
    const Vec2 f = liftFront(lift.door, 0.7f);
    px = f.x; pz = f.y; velx = velz = 0;
    if (lift.t > 0) return;
    const Vec2 out = liftFront(lift.dest, 0.7f);
    px = out.x; pz = out.y;
    py = world.groundAt(px, pz, lift.dest.pos.y + 0.5f);
    vy = 0;
    yaw = lift.dest.normal.z > 0 ? 1.5707963f : -1.5707963f;   // facing out of the doors
    lift.state = LiftState::Idle;
    play(Sfx::LiftDing).atVolume(0.9f);
    deckNoteT = 2.6f;
    deckNote = "the doors open onto another part of the warehouse.";
}
