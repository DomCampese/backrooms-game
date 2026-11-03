// The theater's Usher: a second figure that patrols the auditoriums with a
// torch and calls the hunter when its beam finds the player.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

// s for a downed Usher to crumple and fade, matching usherSprite's sink.
static constexpr float USHER_FALL = 1.5f;

static float wrapPi(float a) {
    a = fmodf(a + TAU * 0.5f, TAU);
    if (a < 0) a += TAU;
    return a - TAU * 0.5f;
}

static int chunkOf(float x) { return fdiv(cellOf(x), CCELLS); }

static Vec2 cellCentre(const Auditorium &a, int u, int v) {
    int x, z;
    audCell(a.dir, u, v, x, z);
    return { a.cx * CHUNK + x * CELL + 1.0f, a.cz * CHUNK + z * CELL + 1.0f };
}

// It comes out from behind a screen out of the player's sight; with none in
// reach, from a spot 14-24 m off that the player cannot see.
void Sim::spawnUsher(double now) {
    bool placed = false;
    for (const Auditorium &a : world.auditoriums(chunkOf(px), chunkOf(pz))) {
        Vec2 s = cellCentre(a, AUD_N / 2, AUD_BACKSTAGE - 1);
        if (portableHypot(s.x - px, s.y - pz) < 8.0f || world.lineOfSight(px, pz, s.x, s.y)) continue;
        usher.x = s.x; usher.z = s.y; placed = true;
        break;
    }
    for (int tries = 0; tries < 16 && !placed; tries++) {
        float a = usherRng.f01() * TAU, d = 14.0f + usherRng.f01() * 10.0f;
        Vec2 s = world.findOpenSpot(px + portableCos(a) * d, pz + portableSin(a) * d);
        usher.x = s.x; usher.z = s.y;
        placed = !world.lineOfSight(px, pz, s.x, s.y);
    }
    usher.st = UState::Patrol;
    usher.life = 0; usher.caught = 0; usher.whistleCd = 0; usher.gait = 0; usher.hp = 2;
    usher.vx = usher.vz = 0;
    usher.dispY = world.groundAt(usher.x, usher.z, 1.0f);
    usher.beamYaw = portableAtan2(usher.z - pz, usher.x - px);   // facing away from the player
    usher.wpx = usher.x; usher.wpz = usher.z; usher.repathT = 0;
    usherRoamTarget(now);
}

// Down an aisle of a nearby auditorium, along a walkway backstage, or now and
// then somewhere 6-14 m off, which takes it through the lobbies.
void Sim::usherRoamTarget(double now) {
    static const int AISLES[3] = { 0, AUD_N / 2, AUD_N - 1 };
    std::vector<Auditorium> auds = world.auditoriums(chunkOf(usher.x), chunkOf(usher.z));
    Vec2 t;
    if (!auds.empty() && usherRng.f01() < 0.8f) {
        const Auditorium &a = auds[usherRng.ri(0, (int)auds.size() - 1)];
        if (usherRng.f01() < 0.2f) t = cellCentre(a, usherRng.ri(0, AUD_N - 1), AUD_BACKSTAGE - 1);
        else t = cellCentre(a, AISLES[usherRng.ri(0, 2)], usherRng.ri(AUD_BACKSTAGE, AUD_N - 1));
    } else {
        float a = usherRng.f01() * TAU, r = 6.0f + usherRng.f01() * 8.0f;
        t = world.findOpenSpot(usher.x + portableCos(a) * r, usher.z + portableSin(a) * r);
    }
    usher.roamX = t.x; usher.roamZ = t.y;
    usher.nextRoam = now + 14.0 + usherRng.f01() * 10.0;
}

// Hiding beats the beam outright; crouching halves its reach.
bool Sim::inUsherBeam() {
    if ((usher.st != UState::Patrol && usher.st != UState::Spot) || hidden) return false;
    float dx = px - usher.x, dz = pz - usher.z, d = portableHypot(dx, dz);
    float reach = crouchCur > 0.7f ? USHER_BEAM_M * 0.5f : USHER_BEAM_M;
    if (d > reach) return false;
    if (d > 0.01f) {
        float along = (dx * portableCos(usher.beamYaw) + dz * portableSin(usher.beamYaw)) / d;
        if (along < portableCos(USHER_BEAM_HALF)) return false;
    }
    return world.lineOfSight(usher.x, usher.z, px, pz);
}

void Sim::usherWhistle() {
    float inv = usherDist > 0.01f ? 1.0f / usherDist : 0.0f;
    float sd = clampf(((usher.x - px) * inv) * r2x + ((usher.z - pz) * inv) * r2z, -1.0f, 1.0f);
    play(Sfx::Whistle).atPan(sd).atVolume(clampf(1.6f / (1.0f + 0.012f * usherDist * usherDist), 0.3f, 1.0f));
    deckNoteT = 2.6f; deckNote = "a whistle. something heard it.";
    if (ent.st == EState::Hidden) spawnHunter();
    if (ent.st == EState::Stalk) {
        ent.st = EState::Chase;
        ent.life = 0; ent.unseen = 0; ent.repathT = 0;
    }
}

void Sim::bulletHitsUsher() {
    if (--usher.hp <= 0) {
        play(Sfx::Kill).atPitch(1.25f);
        usher.st = UState::Down; usher.life = 0;
        return;
    }
    play(Sfx::Hit);
    if (usher.whistleCd <= 0 && usher.st == UState::Patrol) {
        usher.st = UState::Spot; usher.life = 0;
        usherWhistle();
    }
}

// Patrol: it walks its route sweeping the torch, and turns toward a sprint, a
// shot or a lit flashlight it can see. Spot: it stops, holds the beam on the
// player for USHER_HOLD, then walks on and cannot whistle for USHER_REST.
void Sim::updateUsher(float dt, double now) {
    if (level != THEATER) { usher.st = UState::Gone; usherDist = 1e9f; return; }
    usher.whistleCd = fmaxf(0.0f, usher.whistleCd - dt);
    if (usher.st == UState::Gone) {
        usherDist = 1e9f;
        if (now > usher.nextSpawn && deathT <= 0) spawnUsher(now);
        return;
    }
    const float fromX = usher.x, fromZ = usher.z;
    usher.life += dt;
    float dx = px - usher.x, dz = pz - usher.z;
    usherDist = portableHypot(dx, dz);
    if (usher.st == UState::Down) {
        if (usher.life > USHER_FALL) { usher.st = UState::Gone; usher.nextSpawn = now + 90.0 + usherRng.f01() * 60.0; }
        return;
    }
    const float toPlayer = portableAtan2(dz, dx);
    float aimYaw = usher.beamYaw;
    if (usher.st == UState::Patrol) {
        bool loud = sprinting || (flashOn && flashCur > 0.5f) || muzzleT > 0;
        bool heard = loud && usherDist < USHER_HEAR_M && world.lineOfSight(usher.x, usher.z, px, pz);
        if (heard) { usher.roamX = px; usher.roamZ = pz; usher.nextRoam = now + 6.0; }
        float rdx = usher.roamX - usher.x, rdz = usher.roamZ - usher.z;
        if (now > usher.nextRoam || rdx * rdx + rdz * rdz < 1.0f) usherRoamTarget(now);
        usher.repathT -= dt;
        float wdx = usher.wpx - usher.x, wdz = usher.wpz - usher.z;
        if (usher.repathT <= 0 || wdx * wdx + wdz * wdz < 0.2f) {
            usher.repathT = 0.4;
            int oi, ok;
            if (!world.lineOfSight(usher.x, usher.z, usher.roamX, usher.roamZ) &&
                world.pathStep(cellOf(usher.x), cellOf(usher.z), cellOf(usher.roamX), cellOf(usher.roamZ), oi, ok))
                { usher.wpx = oi * CELL + 1.0f; usher.wpz = ok * CELL + 1.0f; }
            else { usher.wpx = usher.roamX; usher.wpz = usher.roamZ; }
        }
        float sx = usher.wpx - usher.x, sz = usher.wpz - usher.z, sl = portableHypot(sx, sz);
        if (sl > 0.05f) {
            usher.x += sx / sl * USHER_WALK * dt; usher.z += sz / sl * USHER_WALK * dt;
            aimYaw = portableAtan2(sz, sx) + 0.45f * portableSin(usher.life * 0.8f);
        }
        world.collideCircle(usher.x, usher.z, 0.32f, usher.dispY);
        if (heard) aimYaw = toPlayer;
        usher.caught = inUsherBeam() ? usher.caught + dt : fmaxf(0.0f, usher.caught - dt);
        if (usher.caught > USHER_SPOT && usher.whistleCd <= 0) {
            usher.st = UState::Spot; usher.life = 0;
            usherWhistle();
        }
    } else {
        aimYaw = toPlayer;
        if (usher.life > USHER_HOLD) {
            usher.st = UState::Patrol; usher.life = 0; usher.caught = 0; usher.whistleCd = USHER_REST;
            usherRoamTarget(now);
        }
    }
    float turn = (usher.st == UState::Spot ? 3.0f : 1.6f) * dt;
    usher.beamYaw = wrapPi(usher.beamYaw + clampf(wrapPi(aimYaw - usher.beamYaw), -turn, turn));
    if (inUsherBeam()) fear = fmaxf(fear, 0.35f);

    float moved = portableHypot(usher.x - fromX, usher.z - fromZ);
    float lastGait = usher.gait;
    usher.gait += moved / ENT_STRIDE;
    if (usher.gait > 4096.0f) usher.gait -= 4096.0f;
    if (floorf(usher.gait) > floorf(lastGait) && usherDist < 18.0f && deathT <= 0) {
        bool los = world.lineOfSight(px, pz, usher.x, usher.z);
        float inv = usherDist > 0.01f ? 1.0f / usherDist : 0.0f;
        float sd = clampf((-dx * inv) * r2x + (-dz * inv) * r2z, -1.0f, 1.0f);
        play(los ? Sfx::EntStep : Sfx::EntStepThrough, usherRng.ri(0, 3))
            .atPan(sd)
            .atPitch(0.95f + usherRng.f01() * 0.08f)
            .atVolume(clampf(0.8f * (los ? 1.0f : 0.45f) / (1.0f + 0.09f * usherDist * usherDist), 0.0f, 0.6f));
    }
    if (dt > 1e-5f) { usher.vx = (usher.x - fromX) / dt; usher.vz = (usher.z - fromZ) / dt; }
    float gy = world.groundAt(usher.x, usher.z, usher.dispY + MAX_STEP);
    usher.dispY += (gy - usher.dispY) * fminf(1.0f, 10.0f * dt);
}
