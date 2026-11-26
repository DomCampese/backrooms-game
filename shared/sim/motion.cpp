// Level 94: the day and night, the Animation's stop-motion pose, and the
// houses that shelter the player at night.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

float Sim::daylight() const {
    if (level != MOTION) return 1.0f;
    const float dusk = DAY_TIME, night = dusk + DUSK_TIME, dawn = night + NIGHT_TIME;
    const float t = sky.t;
    if (t < dusk) return 1.0f;
    if (t < night) return 1.0f + (NIGHT_LIGHT - 1.0f) * (t - dusk) / DUSK_TIME;
    if (t < dawn) return NIGHT_LIGHT;
    return NIGHT_LIGHT + (1.0f - NIGHT_LIGHT) * (t - dawn) / DAWN_TIME;
}

// Night runs from the middle of dusk to the middle of dawn, while the sky is
// at or under halfway dark.
void Sim::updateSky(float dt) {
    sheltered = false;
    inBrokenHouse = false;
    if (level != MOTION) return;
    if (inMenu || deathT > 0) return;
    const float cycle = DAY_TIME + DUSK_TIME + NIGHT_TIME + DAWN_TIME;
    sky.t = fmodf(sky.t + dt, cycle);
    const bool night = sky.t >= DAY_TIME + DUSK_TIME * 0.5f && sky.t < DAY_TIME + DUSK_TIME + NIGHT_TIME + DAWN_TIME * 0.5f;
    if (night != sky.night) {
        sky.night = night;
        deckNoteT = 2.6f;
        deckNote = night ? "it's dark. the Animations come out at night. get inside a house."
                         : "morning. nothing moves out there in the day.";
        if (night) play(Sfx::Scare).atPitch(0.55f).atVolume(0.5f);
        else brokenHouses.clear();   // the houses are whole again by day
    }
    updateSiege(dt);
    sheltered = shutOut(px, pz);
    inBrokenHouse = !sheltered && !brokenHouses.empty() && houseKey(px, pz) != 0;
    bedNear = bedInReach() && sleepT <= 0;
    int ta, tb;
    truckNear = truckInReach(ta, tb) && !trucksServed.count(cellKey2(ta, tb));
    knockedT += dt;
}

// The pose the Animation is drawn in holds for 1 / STOP_MOTION_FPS s, then
// jumps to where the sim has moved it, nudged as a hand-placed model is, with
// the click of a camera's shutter.
void Sim::updateStopPose(float dt) {
    StopPose &p = stopPose;
    p.t += dt;
    if (p.t < 1.0f / STOP_MOTION_FPS && ent.st != EState::Hidden) return;
    const uint32_t f = p.frame + 1;
    const uint32_t h = ih((int)f, 94, 0x5707u);
    const float jx = ((h & 255) / 255.0f - 0.5f) * 2.0f * POSE_JITTER;
    const float jz = (((h >> 8) & 255) / 255.0f - 0.5f) * 2.0f * POSE_JITTER;
    p = { ent.x + jx, ent.z + jz, ent.dispY, ent.gait, 0.0f, f };
    if (ent.st == EState::Hidden || ent.st == EState::Die || entDist >= SHUTTER_HEAR) return;
    const float inv = entDist > 0.01f ? 1.0f / entDist : 0.0f;
    play(Sfx::Shutter, (int)(h >> 16) % 3)
        .atPan(clampf(((ent.x - px) * inv) * r2x + ((ent.z - pz) * inv) * r2z, -1.0f, 1.0f))
        .atVolume(clampf(1.0f - entDist / SHUTTER_HEAR, 0.0f, 1.0f) * 0.55f);
}

bool Sim::bedInReach() {
    if (level != MOTION || inMenu || deathT > 0) return false;
    const int pci = cellOf(px), pck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        const int a = pci + dx, b = pck + dz;
        if (world.propAt(a, b) != PROP_BED) continue;
        const float ex = px - (a * CELL + 1.0f), ez = pz - (b * CELL + 1.0f);
        if (ex * ex + ez * ez < BED_REACH * BED_REACH) return true;
    }
    return false;
}

bool Sim::truckInReach(int &cabI, int &cabK) {
    if (level != MOTION || inMenu || deathT > 0 || world.storey != 0) return false;
    const int pci = cellOf(px), pck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        const int a = pci + dx, b = pck + dz;
        if (world.propAt(a, b) != PROP_TRUCK) continue;
        const float ex = px - (a * CELL + 1.0f), ez = pz - (b * CELL + 1.0f);
        if (ex * ex + ez * ez >= TRUCK_REACH * TRUCK_REACH) continue;
        const uint8_t rot = world.propRotAt(a, b);
        const int dir = rot & 3;
        cabI = rot & PROP_TRUCK_BACK ? a - TRUCK_BACK_STEP[dir][0] : a;
        cabK = rot & PROP_TRUCK_BACK ? b - TRUCK_BACK_STEP[dir][1] : b;
        return true;
    }
    return false;
}

// One can a truck, left on the counter under its awning.
void Sim::takeFromTruck(int cabI, int cabK) {
    deckNoteT = 2.6f;
    if (!trucksServed.insert(cellKey2(cabI, cabK)).second) { deckNote = "the hatch is empty. nobody is inside."; return; }
    almond++;
    play(Sfx::Vend).atPitch(1.1f);
    deckNote = "a can of almond water, left on the truck's counter.";
}

// Sleep only at night (or as the sun goes down), and not with something
// hunting within 30 m.
void Sim::trySleep() {
    if (sleepT > 0) return;
    deckNoteT = 2.6f;
    if (!sky.night && sky.t < DAY_TIME) { deckNote = "you aren't tired. it's still day."; return; }
    if (ent.st != EState::Hidden && ent.st != EState::Die && entDist < 30.0f) {
        deckNote = "you can't sleep. it's looking for you.";
        return;
    }
    deckNoteT = 0;
    sleepT = 0.001f;
}

// Lying down: the screen fades out (the HUD reads sleepT), then it is morning.
void Sim::updateSleep(float dt) {
    wakeT = fmaxf(0.0f, wakeT - dt);
    if (sleepT <= 0) return;
    velx = velz = 0;
    sleepT += dt;
    if (sleepT < SLEEP_FADE) return;
    sleepT = 0;
    wakeT = 2.0f;
    sky.t = 0;   // the start of the day: updateSky turns night off and says so
    ent.st = EState::Hidden;
    health = 1.0f;
    sanity = clampf(sanity + SLEEP_SANITY, 0.0f, 1.0f);
}

// The house a point is in: the chunk and quarter of the MotionHouse whose
// walls hold it, as one key, or 0 out of doors. motionHouse reads only the
// bare seed, so the key is the same on both storeys of a tall house.
uint64_t Sim::houseKey(float x, float z) {
    const int ci = cellOf(x), ck = cellOf(z);
    if (level != MOTION || !world.indoors(ci, ck)) return 0;
    const int cx = fdiv(ci, CCELLS), cz = fdiv(ck, CCELLS);
    const int li = ci - cx * CCELLS, lk = ck - cz * CCELLS;
    const int q = (li >= CCELLS / 2 ? 1 : 0) + (lk >= CCELLS / 2 ? 2 : 0);
    MotionHouse h;
    if (!world.motionHouse(cx, cz, q, h) || li < h.i0 || li > h.i1 || lk < h.k0 || lk > h.k1) return 0;
    return (((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz) * 4 + (uint64_t)q + 1;
}

bool Sim::houseBroken(uint64_t key) const {
    for (const BrokenHouse &b : brokenHouses) if (b.key == key) return true;
    return false;
}

bool Sim::shutOut(float x, float z) {
    if (!world.indoors(cellOf(x), cellOf(z))) return false;
    return brokenHouses.empty() || !houseBroken(houseKey(x, z));
}

// The Animation waiting outside the player's house for SIEGE_TIME breaks in.
// The clock runs only while it chases, its next waypoint is inside a house
// it cannot enter, it is within SIEGE_NEAR, and the player is in that house.
void Sim::updateSiege(float dt) {
    if (siegeEntry != levelEntries) {
        siegeEntry = levelEntries;
        brokenHouses.clear();
        siegeT = rattleT = 0;
    }
    const uint64_t home = houseKey(px, pz);
    const bool waiting = sky.night && home != 0 && !houseBroken(home) && ent.st == EState::Chase &&
                         shutOut(ent.wpx, ent.wpz) && entDist < SIEGE_NEAR && deathT <= 0 && sleepT <= 0;
    if (!waiting) { siegeT = fmaxf(0, siegeT - dt * 0.5f); return; }
    siegeT += dt;
    const float inv = entDist > 0.01f ? 1.0f / entDist : 0.0f;
    const float pan = clampf(((ent.x - px) * inv) * r2x + ((ent.z - pz) * inv) * r2z, -1.0f, 1.0f);
    if (siegeT >= SIEGE_WARN) {
        rattleT -= dt;
        if (rattleT <= 0) {   // a pane shaken in its frame: the knock, higher and quicker
            rattleT = SIEGE_RATTLE_GAP;
            play(Sfx::DoorKnock, (int)(siegeT * 7.0f) % 3).atPitch(1.7f).atPan(pan).atVolume(0.75f);
        }
    }
    if (siegeT < SIEGE_TIME) return;
    siegeT = 0;
    rattleT = 0;
    brokenHouses.push_back({ home, ent.x, ent.z, ent.dispY, world.storey });
    play(Sfx::GlassBreak).atPan(pan).atVolume(1.0f);
    fear = 1.0f;
    deckNoteT = 2.8f;
    deckNote = "glass breaks. it is coming in. get out, or find another house.";
    knockedT = 99.0f;   // the knocking line gives way to the siege's
    hunterNotice(px, pz);   // and in it comes, through the door
}
