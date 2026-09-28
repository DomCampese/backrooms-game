// What you hold: the revolver and its rounds, flares, the party balloons.
#include "sim.h"
#include <algorithm>
#include <cmath>

void Sim::updateAim(bool held, float dt) {
    aiming = held && weapon == WEAPON_REVOLVER && reloadT <= 0 &&
        drinkT <= 0 && deathT <= 0 && winT <= 0 && !paused && !inMenu;
    float target = aiming ? 1.0f : 0.0f;
    // Fixed travel time, the same at any frame rate.
    aimBlend += clampf(target - aimBlend, -dt / 0.16f, dt / 0.20f);
    if (weapon != WEAPON_REVOLVER || reloadT > 0 || drinkT > 0) aimBlend = 0;
}

bool Sim::canReload() const {
    return !aiming && aimBlend <= 0 && weapon == WEAPON_REVOLVER &&
        ammo < MAXAMMO && reloadT <= 0 && drinkT <= 0 && deathT <= 0 && winT <= 0;
}

void Sim::updateWeapons(const InputFrame &in, float dt, double now) {
    if (in.pickRevolver) weapon = WEAPON_REVOLVER;
    if (in.pickFlare) weapon = WEAPON_FLARE;
    if (in.pickDeck) weapon = WEAPON_DECK;
    wheelCd = fmaxf(0, wheelCd - dt);
    // The wheel runs the loop both ways: +1 forward, -1 as +(N-1) to stay positive.
    if (wheelCd <= 0 && fabsf(in.wheel) > 0.5f) {
        weapon = (weapon + (in.wheel > 0 ? 1 : WEAPON_COUNT - 1)) % WEAPON_COUNT;
        wheelCd = 0.25f;
    }
    updateAim(in.playing && in.aim, dt);
    // A touch AIM latches, so a refused aim must drop it or the button stays
    // lit over a lowered gun. A reload keeps it: the sights rise when it ends,
    // as holding RMB through one does.
    if (!aiming && !(weapon == WEAPON_REVOLVER && reloadT > 0)) dropAimLatch = true;
    gunCd = fmaxf(0, gunCd - dt);
    muzzleT = fmaxf(0, muzzleT - dt);
    muzzleSmoke = fmaxf(0, muzzleSmoke - dt * 0.7f);
    recoil += (0 - recoil) * fminf(1, 10 * dt);
    if (reloadT > 0) {
        reloadT -= dt;
        if (reloadT <= 0) { ammo = MAXAMMO; play(Sfx::Click).atPitch(1.15f); }
    }
    if (weapon == WEAPON_REVOLVER && in.playing && in.fire && deathT <= 0 && reloadT <= 0 && gunCd <= 0 &&
        drinkT <= 0) {
        if (ammo <= 0) { play(Sfx::Click).atPitch(0.7f); gunCd = 0.25f; }  // dry fire
        else {
            ammo--; gunCd = SHOT_INTERVAL; muzzleT = MUZZLE_FLASH; recoil = 1.0f; muzzleSmoke = 1.0f;
            play(Sfx::Shot);
            fireBullet();
            // The report carries: it brings him sooner, or turns his head.
            if (ent.st == EState::Hidden) ent.nextSpawn = fmin(ent.nextSpawn, now + 5 + grng.f01() * 6);
            else if (ent.st == EState::Stalk) ent.gaze += 0.8f;
        }
    }
    if (in.reload && in.playing && canReload()) {
        reloadT = RELOAD_TIME;
        play(Sfx::Click).atPitch(0.95f);
    }
}

// Along the sight ray, so the front blade is the aim point.
void Sim::fireBullet() {
    Vector3 origin{px, eyeY, pz};
    bullets.push_back({origin, origin, Vector3Normalize(fwd), 60.0f, 0.0f});
}

// Rounds travel at 220 m/s and test the whole segment each tick. Level
// geometry, actors and balloons compete for one nearest hit, so a round hits
// one thing.
void Sim::updateBullets(float dt) {
    for (auto &impact : bulletImpacts) impact.life -= dt;
    bulletImpacts.erase(std::remove_if(bulletImpacts.begin(), bulletImpacts.end(),
        [](const BulletImpact &i) { return i.life <= 0; }), bulletImpacts.end());
    for (auto &bullet : bullets) {
        if (bullet.remaining <= 0) { bullet.fade -= dt; continue; }
        float travel = fminf(220.0f * dt, bullet.remaining);
        Ray ray{bullet.pos, bullet.direction};
        float nearest = travel;
        bool hit = false;
        int target = -1;             // -1 level geometry, 0..MAXDOGS-1 a dog, MAXDOGS the hunter, -2 a balloon
        Vector3 normal = Vector3Negate(bullet.direction);
        if (tracer && tracer->nearestSolid(ray, nearest, normal)) hit = true;
        auto body = [&](float x, float z, float y, float h, float radius, int id) {
            RayCollision c = rayBox(ray, {{x - radius, y, z - radius}, {x + radius, y + h, z + radius}});
            if (c.hit && c.distance >= 0 && c.distance <= nearest) {
                nearest = c.distance; normal = c.normal; target = id; hit = true;
            }
        };
        for (int i = 0; i < MAXDOGS; ++i) {
            const Dog &d = dogs[i];
            if (d.st != DState::Gone && d.st != DState::Yelp)
                body(d.x, d.z, d.dispY, 0.92f, 0.6f, i);
        }
        if (ent.st == EState::Stalk || ent.st == EState::Chase || ent.st == EState::Flee)
            body(ent.x, ent.z, ent.dispY, 1.95f, 0.55f, MAXDOGS);
        // Balloons up to the nearest solid hit, in steps shorter than their radius.
        if (level == 4) for (float d = 0; d < nearest; d += 0.1f) {
            if (popBalloonAt(Vector3Add(ray.position, Vector3Scale(ray.direction, d)))) {
                nearest = d; hit = true; target = -2; break;
            }
        }
        bullet.tail = bullet.pos;
        bullet.pos = Vector3Add(ray.position, Vector3Scale(ray.direction, nearest));
        bullet.remaining -= nearest;
        bullet.fade = 0.055f;
        if (!hit) continue;
        bullet.remaining = 0;
        bulletImpacts.push_back({Vector3Add(bullet.pos, Vector3Scale(normal, 0.012f)), normal, 0.24f, target >= 0});
        if (target >= 0 && target < MAXDOGS) bulletHitsDog(target);
        else if (target == MAXDOGS) bulletHitsHunter();
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
        [](const Bullet &b) { return b.remaining <= 0 && b.fade <= 0; }), bullets.end());
}

void Sim::bulletHitsDog(int i) {
    Dog &d = dogs[i];
    float ex = d.x - px, ez = d.z - pz;
    if (--d.hp <= 0) {
        d.st = DState::Yelp; d.life = 0;
        play(Sfx::Bark, i % 3).atPitch(1.6f).atVolume(0.9f);
    } else {
        play(Sfx::Hit);
        float dl = sqrtf(ex * ex + ez * ez) + 1e-4f;
        d.x += ex / dl * 0.8f; d.z += ez / dl * 0.8f;   // rocked, not repelled
        world.collideCircle(d.x, d.z, 0.3f, d.dispY);
    }
}

void Sim::bulletHitsHunter() {
    ent.hp--;
    if (ent.hp <= 0) {
        play(Sfx::Kill);
        killT = 3.0f; killCount++;
        ent.st = EState::Die; ent.life = 0;
        for (int c2 = 0; c2 < 5; c2++) {   // he spills his doubloons
            float aa = c2 * 1.2566f + grng.f01();
            coinsWorld.push_back({ ent.x + cosf(aa) * 0.5f, ent.dispY, ent.z + sinf(aa) * 0.5f });
        }
        bankRecords();
    } else {
        // Rocked back, staggered, and now he knows where you are.
        play(Sfx::Hit);
        float ex = ent.x - px, ez = ent.z - pz;
        float dd = sqrtf(ex * ex + ez * ez);
        if (dd > 0.01f) { ent.x += ex / dd * 0.5f; ent.z += ez / dd * 0.5f; }
        world.collideCircle(ent.x, ent.z, 0.38f, ent.dispY);
        ent.stagger = 0.45f;
        if (ent.st != EState::Chase) {
            ent.st = EState::Chase;
            ent.life = 0; ent.unseen = 0; ent.repathT = 0;
        }
    }
}

bool Sim::anyFlareLit() const {
    for (const FlareProj &f : litFlares) if (f.active) return true;
    return false;
}

// Distance on the floor plane only: everything asking is reasoning about a
// hall, not a stairwell.
const FlareProj *Sim::nearestLitFlare(float x, float z) const {
    const FlareProj *pick = nullptr;
    float pickD2 = 1e30f;
    for (const FlareProj &f : litFlares) {
        if (!f.active) continue;
        if (!f.onStorey) continue;   // a fire on the floor below wards nothing up here
        float dx = f.x - x, dz = f.z - z, d2 = dx * dx + dz * dz;
        if (d2 < pickD2) { pickD2 = d2; pick = &f; }
    }
    return pick;
}

float Sim::flarePresence(const FlareProj &f, float x, float z) {
    if (!f.active) return 0.0f;
    if (!f.onStorey) return 0.0f;
    float dx = f.x - x, dz = f.z - z;
    return clampf(f.burn / FLAREFADE, 0, 1) / (1.0f + FLAREFALL * (dx * dx + dz * dz));
}

// By presence, not distance: a flare guttering at your feet must not hold the
// light off a fresh one up the hall, or the light jumps the frame it dies.
const FlareProj *Sim::dominantFlare(float x, float z) const {
    const FlareProj *pick = nullptr;
    float pickP = 0.0f;
    for (const FlareProj &f : litFlares) {
        float p = flarePresence(f, x, z);
        if (p > pickP) { pickP = p; pick = &f; }
    }
    return pick;
}

void Sim::updateFlare(const InputFrame &in, float dt, double now) {
    if (in.playing && deathT <= 0 && flares > 0 && drinkT <= 0 &&
        (in.throwFlare || (weapon == WEAPON_FLARE && in.fire))) {
        flares--;
        // A free slot, or else the fire with least burn left: overwriting a
        // burning flare mid-burn made it vanish, light, hiss and all.
        FlareProj *slot = nullptr;
        for (FlareProj &f : litFlares) {
            if (!f.active) { slot = &f; break; }
            if (!slot || f.burn < slot->burn) slot = &f;
        }
        FlareProj &fl = *slot;
        fl.active = true; fl.flying = true; fl.burn = FLAREBURN;
        fl.x = px + fwd.x * 0.4f; fl.y = eyeY - 0.15f; fl.z = pz + fwd.z * 0.4f;
        fl.vx = fwd.x * 10.5f; fl.vz = fwd.z * 10.5f; fl.vy = fwd.y * 10.5f + 2.4f;
        play(Sfx::FlareStrike);
        if (ent.st == EState::Hidden) ent.nextSpawn = fmin(ent.nextSpawn, now + 12 + grng.f01() * 10);  // he hears the strike
    }
    float hiss = 0;
    for (FlareProj &flare : litFlares) {
        if (!flare.active) continue;
        if (flare.flying && !flyFlare(flare, dt)) continue;
        flare.burn -= dt;
        if (flare.burn <= 0) { flare.active = false; continue; }
        // One on the storey below burns and is drawn, but does not light or
        // ward this storey.
        flare.onStorey = world.storeyH <= 0.0f || (flare.y > -0.6f && flare.y < world.storeyH - 0.2f);
        // One hiss channel: the fire with most presence takes it, the same
        // one the renderer lights the room by.
        hiss = fmaxf(hiss, flarePresence(flare, px, pz));
    }
    ambience.hiss = hiss;
    if (flares >= MAXFLARES) nextFlareRegen = now + FLARE_REGEN;
    else if (now > nextFlareRegen) { flares++; nextFlareRegen = now + FLARE_REGEN; }
}

bool Sim::flyFlare(FlareProj &flare, float dt) {
    flare.vy -= 18.0f * dt;
    flare.x += flare.vx * dt; flare.y += flare.vy * dt; flare.z += flare.vz * dt;
    float ox = flare.x, oz = flare.z;
    world.collideCircle(flare.x, flare.z, 0.07f, flare.y);
    if (fabsf(ox - flare.x) > 1e-5f) flare.vx *= -0.35f;   // clatter off walls
    if (fabsf(oz - flare.z) > 1e-5f) flare.vz *= -0.35f;
    float fg = world.groundAt(flare.x, flare.z, flare.y);
    if (flare.y < fg + 0.04f && flare.vy < 0) {
        flare.y = fg + 0.04f;
        if (flare.vy < -2.0f) { flare.vy *= -0.30f; flare.vx *= 0.55f; flare.vz *= 0.55f; }
        else { flare.flying = false; flare.vx = flare.vy = flare.vz = 0; }
    }
    if (world.poolAt(cellOf(flare.x), cellOf(flare.z)) && flare.y < -0.10f) {
        play(Sfx::SplashOut, grng.ri(0, 2)).atPitch(1.3f).atVolume(0.7f);
        flare.active = false;
        return false;
    }
    return true;
}

// LEVEL FUN's ceiling balloon in this cell, if one floats here unpopped.
bool Sim::balloonAt(int a, int b, Vector3 &out) {
    if (level != 4) return false;
    if (poppedBalloons.count(cellKey2(a, b))) return false;
    uint32_t h = ih(a, b, pickupSalt() ^ 0xBA11u);
    if (h % 17 != 0 || world.pillarAt(a, b)) return false;
    out = { a * CELL + 1.0f + (((h >> 4) & 7) / 7.0f - 0.5f) * 0.9f,
            world.wallH - 0.21f,
            b * CELL + 1.0f + (((h >> 7) & 7) / 7.0f - 0.5f) * 0.9f };
    return true;
}

// Without the renderer's sway, which is small next to the hit radius.
int Sim::tableBalloonBunch(int a, int b, Vector3 *pos, Color *cols, Vector3 &tie) {
    if (level != 4 || world.propAt(a, b) != PROP_PARTY_TABLE) return 0;
    uint32_t h = ih(a, b, pickupSalt() ^ 0x8A11u);
    if (h % 3 != 0) return 0;                       // most tables, not all
    float tx = a * CELL + 1.0f, tz = b * CELL + 1.0f;
    float ty = world.floorY(a, b) + 0.74f;          // knotted at the tabletop
    tie = { tx, ty, tz };
    int nb = 2 + (int)(h % 3);
    for (int k = 0; k < nb; k++) {
        uint32_t bh = h * 2246822519u + (uint32_t)k * 2654435761u;
        float ox = (((bh >> 3) & 7) / 7.0f - 0.5f) * 0.42f;
        float oz = (((bh >> 7) & 7) / 7.0f - 0.5f) * 0.42f;
        float by = ty + 1.15f + (((bh >> 11) & 3) * 0.06f);
        pos[k] = { tx + ox, by, tz + oz };
        if (cols) cols[k] = PARTY[(bh >> 13) % 5];
    }
    return nb;
}

bool Sim::popBalloonAt(Vector3 point) {
    if (level != 4) return false;
    auto burst = [&](Vector3 at, Color base, int n) {
        for (int c2 = 0; c2 < n; c2++) {
            float aa = grng.f01() * TAU, sp = 1.2f + grng.f01() * 2.2f;
            confetti.push_back({ at, { cosf(aa) * sp, 0.6f + grng.f01() * 1.6f, sinf(aa) * sp },
                                 1.3f + grng.f01() * 0.9f,
                                 grng.f01() < 0.5f ? base : PARTY[c2 % 5] });
        }
    };
    float wx = point.x, wy = point.y, wz = point.z;
    int a = cellOf(wx), b = cellOf(wz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        int ca = a + dx, cb = b + dz;
        Vector3 bp;
        if (balloonAt(ca, cb, bp)) {   // a lone ceiling balloon
            float ex = bp.x - wx, ey = bp.y - wy, ez = bp.z - wz;
            if (ex * ex + ey * ey + ez * ez <= 0.24f * 0.24f) {
                poppedBalloons.insert(cellKey2(ca, cb));
                play(Sfx::Pop).atPitch(0.9f + grng.f01() * 0.3f).atPan(0);
                burst(bp, PARTY[(ih(ca, cb, pickupSalt() ^ 0xBA11u) >> 10) % 5], 16);
                return true;
            }
        }
        if (!poppedTableBunches.count(cellKey2(ca, cb))) {   // a table bunch: all of it goes
            Vector3 bpos[4], tie; Color bcol[4];
            int nb = tableBalloonBunch(ca, cb, bpos, bcol, tie);
            for (int k = 0; k < nb; k++) {
                float ex = bpos[k].x - wx, ey = bpos[k].y - wy, ez = bpos[k].z - wz;
                if (ex * ex + ey * ey + ez * ez > 0.26f * 0.26f) continue;
                poppedTableBunches.insert(cellKey2(ca, cb));
                play(Sfx::Pop).atPitch(0.95f + grng.f01() * 0.3f).atPan(0);
                for (int j = 0; j < nb; j++) burst(bpos[j], bcol[j], 11);
                return true;
            }
        }
    }
    return false;
}
