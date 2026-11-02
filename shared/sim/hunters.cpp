// The level's hunter (Pirate Clark, a Smiler, the Partygoer) and the Red Halls
// pack.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

// The pack hunts by sound only. Cover does not hide the player, and a
// carried deck that is playing gives them away even when standing still.
bool Sim::packDeaf() const {
    return still && !(deck.playing && deck.carried);
}

void Sim::updateEntity(float dt, double now, bool forceSpawn) {
    // Nothing hunts on Level 2.
    if (level == 2) {
        ent.st = EState::Hidden; ent.nextSpawn = now + 60; entDist = 1e9f;
        entDarkCur += (0 - entDarkCur) * fminf(1, 4 * dt);
        fear += (0 - fear) * fminf(1, 2 * dt);
        ambience.growl = 0;
        return;
    }
    if (forceSpawn && ent.st == EState::Hidden) {
        Vec2 spot = world.findOpenSpot(px + fwd.x * 8, pz + fwd.z * 8);
        ent.x = spot.x; ent.z = spot.y;
        ent.st = EState::Stalk; ent.gaze = -100; ent.life = 0; ent.hp = 3; ent.stagger = 0;
    }
    float fearT = 0.06f;
    ent.stagger = fmaxf(0.0f, ent.stagger - dt);
    entDist = 1e9f;
    if (ent.st == EState::Hidden) {
        if (sprinting) ent.nextSpawn -= 1.5 * dt;   // sprinting brings the spawn forward
        if (now > ent.nextSpawn) spawnHunter();
    } else {
        float gaitFromX = ent.x, gaitFromZ = ent.z;
        float ex = ent.x - px, ez = ent.z - pz;
        entDist = sqrtf(ex * ex + ez * ez);
        float dirDot = (entDist > 0.01f) ? (fwd.x * ex + fwd.z * ez) / entDist : 1;
        bool entLos = world.lineOfSight(px, pz, ent.x, ent.z);
        bool entVisible = entDist < 36 && dirDot > 0.86f && entLos;
        if (crouchCur > 0.7f && entDist > 7) entVisible = false;   // crouched and over 7 m away: not visible to him
        if (hidden) entVisible = false;
        if (ent.st == EState::Stalk || ent.st == EState::Chase) {
            // Any flare within 6 m makes him flee, whichever was thrown first.
            const FlareProj *fire = nearestLitFlare(ent.x, ent.z);
            if (fire) {
                float fx = ent.x - fire->x, fz = ent.z - fire->z;
                if (fx * fx + fz * fz < 6.0f * 6.0f) {
                    ent.st = EState::Flee; ent.life = 0; ent.gaze = 0;
                }
            }
        }
        if (ent.st == EState::Stalk) {
            ent.life += dt;
            fearT = entVisible ? 0.45f : 0.15f;
            if (entVisible) ent.gaze += dt;
            if (ent.gaze > 1.6f || (entVisible && entDist < 8) || (entDist < 3.0f && !hidden)) {
                ent.st = EState::Chase; ent.repathT = 0;
                hunterNotice(px, pz);
            }
            else if (ent.life > 24 && !entVisible) ent.st = EState::Hidden, ent.nextSpawn = now + 18 + grng.f01() * 35;
        }
        if (ent.st == EState::Chase && hunterChase(dt, now, fearT)) return;
        if (ent.st == EState::Flee) {   // runs from the nearest flare, or from the player once none are lit
            fearT = 0.18f;
            ent.life += dt;
            const FlareProj *from = nearestLitFlare(ent.x, ent.z);
            float rx = ent.x - (from ? from->x : px), rz = ent.z - (from ? from->z : pz);
            float rl = sqrtf(rx * rx + rz * rz);
            if (rl > 0.01f) { ent.x += rx / rl * 6.5f * dt; ent.z += rz / rl * 6.5f * dt; }
            world.collideCircle(ent.x, ent.z, 0.38f, ent.dispY);
            if (ent.life > 3.0f) { ent.st = EState::Hidden; ent.nextSpawn = now + 25 + grng.f01() * 35; }
        }
        // His gait counts footfalls by distance covered, in every state. An
        // integer is a footfall, used for both the sound here and the
        // walk-cycle frame render.cpp draws.
        float moved = portableHypot(ent.x - gaitFromX, ent.z - gaitFromZ);
        float lastGait = ent.gait;
        ent.gait += moved / ENT_STRIDE;
        if (ent.gait > 4096.0f) ent.gait -= 4096.0f;   // an even integer keeps the cycle continuous
        if (floorf(ent.gait) > floorf(lastGait) && entDist < 22.0f && deathT <= 0) {
            float inv = entDist > 0.01f ? 1.0f / entDist : 0.0f;
            float sd = clampf((ex * inv) * r2x + (ez * inv) * r2z, -1.0f, 1.0f);   // + is to the player's right
            // Through a wall, use the muffled clips and a lower volume.
            int pick = grng.ri(0, 3);
            float occ = entLos ? 1.0f : 0.45f;
            play(entLos ? Sfx::EntStep : Sfx::EntStepThrough, pick)
                .atPan(sd)
                .atPitch(0.66f + grng.f01() * 0.08f)
                .atVolume(clampf(1.4f * occ / (1.0f + (entLos ? 0.07f : 0.13f) * entDist * entDist), 0.0f, 0.9f));
        }
        if (ent.st == EState::Die) {
            fearT = 0.10f;
            ent.life += dt;
            if (ent.life > 1.2f) { ent.st = EState::Hidden; ent.nextSpawn = now + 90 + grng.f01() * 60; }
        }
    }
    // Measured from his position, because three states move him in different places.
    if (dt > 1e-5f) {
        ent.vx = (ent.x - entPrevX) / dt;
        ent.vz = (ent.z - entPrevZ) / dt;
    }
    entPrevX = ent.x; entPrevZ = ent.z;
    if (ent.st != EState::Hidden) {
        // groundAt: on a flight, the tread under him; over a hole, the flight
        // coming up through it.
        float egt = world.groundAt(ent.x, ent.z, ent.dispY + MAX_STEP);
        ent.dispY += (egt - ent.dispY) * fminf(1, 10 * dt);
    }
    fear += (fearT - fear) * fminf(1, 2.2f * dt);
    if (whisperT > 0) fear = fmaxf(fear, 0.22f);
    fear = fmaxf(fear, (1.0f - sanity) * 0.60f);
    // Lights near him dim, more so the closer he is to catching the player.
    float darkT = (ent.st == EState::Chase) ? 1.0f
                : (ent.st == EState::Stalk)  ? 0.45f
                : (ent.st == EState::Flee)   ? 0.2f : 0.0f;
    if (level == 2) darkT *= 0.5f;
    entDarkCur += (darkT - entDarkCur) * fminf(1, 1.6f * dt);
    // No growl: his state must not be audible through walls. Only his
    // footsteps and the lunge make sound.
    ambience.growl = 0.0f;
    audioEvent(AudioEvent::AMBIENCE).mix = ambience;
}

// Three spawn options: ahead where the player's route turns out of sight,
// round any corner, or out in the fog. The first two must be out of the
// player's line of sight and inside fog range.
void Sim::spawnHunter() {
    auto unseenSpot = [&](float wx, float wz, float &ox, float &oz) {
        Vec2 s = world.findOpenSpot(wx, wz);
        float dx = s.x - px, dz = s.y - pz, d = sqrtf(dx * dx + dz * dz);
        if (d < SPAWN_NEAR_MIN || d > SPAWN_NEAR_MAX) return false;
        if (world.lineOfSight(px, pz, s.x, s.y)) return false;
        ox = s.x; oz = s.y; return true;
    };
    float sx = 0, sz = 0;
    bool placed = false;
    float roll = grng.f01();
    if (roll < 0.45f) {
        // Follow the router's path along the player's heading and take the first
        // cell out of sight, so it is reachable, not behind a wall.
        int ci = cellOf(px), ck = cellOf(pz);
        int ti = cellOf(px + f2x * 26.0f), tk = cellOf(pz + f2z * 26.0f);
        for (int step = 0; step < 20 && !placed; step++) {
            int oi, ok;
            if (!world.pathStep(ci, ck, ti, tk, oi, ok)) break;
            ci = oi; ck = ok;
            float cx = ci * CELL + 1.0f, cz = ck * CELL + 1.0f;
            float dx = cx - px, dz = cz - pz;
            if (dx * dx + dz * dz > SPAWN_NEAR_MAX * SPAWN_NEAR_MAX) break;
            placed = unseenSpot(cx, cz, sx, sz);
        }
    } else if (roll < 0.80f) {
        // Round a corner at any bearing; a few tries almost always find one.
        for (int tries = 0; tries < 16 && !placed; tries++) {
            float a = grng.f01() * TAU;
            float d = SPAWN_NEAR_MIN + grng.f01() * (SPAWN_NEAR_MAX - SPAWN_NEAR_MIN);
            placed = unseenSpot(px + portableCos(a) * d, pz + portableSin(a) * d, sx, sz);
        }
    }
    if (!placed) {
        float a = grng.f01() * TAU;
        float d = SPAWN_FAR_MIN + grng.f01() * SPAWN_FAR_SPAN;
        Vec2 spot = world.findOpenSpot(px + portableCos(a) * d, pz + portableSin(a) * d);
        sx = spot.x; sz = spot.y;
    }
    ent.x = sx; ent.z = sz;
    ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.hp = 3; ent.stagger = 0;
    ent.gait = 0;   // standing still when first seen
    ent.dispY = world.groundAt(ent.x, ent.z, py + 1.0f);
}

// He catches the player only during a lunge, which starts inside LUNGE_REACH,
// has a warning, and lasts LUNGE_TIME. Sprinting away escapes it; walking
// does not.
bool Sim::hunterChase(float dt, double now, float &fearT) {
    fearT = entDist < 8 ? 1.0f : 0.8f;
    // He sees the player with line of sight inside HUNTER_SIGHT, unless they
    // are hidden or crouched more than 7 m away. He hears the same noises the
    // pack does (packNoise). Either tells him where the player is.
    bool sees = entDist < HUNTER_SIGHT && !hidden && !(crouchCur > 0.7f && entDist > 7) &&
                world.lineOfSight(ent.x, ent.z, px, pz);
    float noiseX, noiseZ;
    float noise = packNoise(noiseX, noiseZ);
    float hx = noiseX - ent.x, hz = noiseZ - ent.z;
    if (sees) hunterNotice(px, pz);
    else if (hx * hx + hz * hz < noise * noise) hunterNotice(noiseX, noiseZ);
    else ent.lostT += dt;
    ent.lunge = fmaxf(0, ent.lunge - dt);
    ent.lungeCd = fmaxf(0, ent.lungeCd - dt);
    // Ignore a catch across a large height difference on stairs.
    bool level2 = fabsf(ent.dispY - py) < 1.4f;
    if (sees && entDist < LUNGE_REACH && level2 && ent.lunge <= 0 && ent.lungeCd <= 0) {
        ent.lunge = LUNGE_TIME;
        ent.lungeCd = LUNGE_TIME + 1.2f;   // pause between lunges
        play(Sfx::Scare).atPitch(0.62f).atVolume(0.85f);
        fearT = 1.0f;
    }
    float chaseSpd = 3.3f + 1.0f * clampf(1 - entDist / 25.0f, 0, 1) + (ent.lunge > 0 ? 3.2f : 0.0f);
    if (!sees && ent.lunge <= 0) chaseSpd = SEARCH_SPEED;
    if (ent.stagger > 0) chaseSpd *= 0.35f;
    if (!sees) fearT = 0.5f;
    // Straight at the target when there is line of sight to it; otherwise
    // along the cell router. The target is the player while he sees them, and
    // otherwise the last place he saw or heard them, then spots around it.
    float tx = sees ? px : ent.lastX, tz = sees ? pz : ent.lastZ;
    float ldx = ent.lastX - ent.x, ldz = ent.lastZ - ent.z;
    if (!sees && ldx * ldx + ldz * ldz < 1.0f) {
        float a = grng.f01() * TAU, d = SEARCH_STEP_MIN + grng.f01() * (SEARCH_STEP_MAX - SEARCH_STEP_MIN);
        Vec2 spot = world.findOpenSpot(ent.lastX + portableCos(a) * d, ent.lastZ + portableSin(a) * d);
        ent.lastX = spot.x; ent.lastZ = spot.y;
        tx = spot.x; tz = spot.y;
        ent.repathT = 0;
    }
    ent.repathT -= dt;
    float wdx = ent.wpx - ent.x, wdz = ent.wpz - ent.z;
    if (ent.repathT <= 0 || wdx * wdx + wdz * wdz < 0.20f) {
        ent.repathT = 0.25;
        int eci = cellOf(ent.x), eck = cellOf(ent.z);
        int oi, ok;
        float gx = tx - ent.x, gz = tz - ent.z;
        if (gx * gx + gz * gz > 2.5f * 2.5f && !world.lineOfSight(ent.x, ent.z, tx, tz) &&
            world.pathStep(eci, eck, cellOf(tx), cellOf(tz), oi, ok) && !(oi == eci && ok == eck)) {
            ent.wpx = oi * CELL + 1.0f; ent.wpz = ok * CELL + 1.0f;
        } else {
            ent.wpx = tx; ent.wpz = tz;
        }
    }
    float sx = ent.wpx - ent.x, sz = ent.wpz - ent.z, sl = sqrtf(sx * sx + sz * sz) + 1e-4f;
    ent.x += sx / sl * chaseSpd * dt;
    ent.z += sz / sl * chaseSpd * dt;
    world.collideCircle(ent.x, ent.z, 0.38f, ent.dispY);
    if (ent.lostT > SEARCH_TIME) {
        ent.st = EState::Hidden;
        ent.nextSpawn = now + 25 + grng.f01() * 40;
    }
    if (hidden && entDist < 2.2f && closeCallT <= 0) {   // close by, but he has not seen the hidden player
        closeCallT = 3.0f;
        play(Sfx::Heartbeat);
    }
    if (entDist < CATCH_REACH && level2 && ent.lunge > 0 && !hidden && hurtT <= 0) {
        if (hurtPlayer(now, ENTITY_HIT, hunterName(), ent.x, ent.z)) return true;
        // A hit: the lunge ends and he staggers.
        ent.lunge = 0; ent.lungeCd = HURT_GRACE + 1.0f; ent.stagger = 0.8f;
    }
    return false;
}

// The pack hunts by sound and is faster than the player, so running does not
// work. Flares turn them, a shot kills one, and silence loses them.
void Sim::updateDogs(float dt, double now) {
    if (level != 3) {
        for (auto &d : dogs) d.st = DState::Gone;
        return;
    }
    float nsx, nsz;
    float noise = packNoise(nsx, nsz);

    if (now > nextHowl && deathT <= 0) {
        nextHowl = now + 26 + grng.f01() * 34;
        int live = 0;
        for (auto &d : dogs) if (d.st != DState::Gone) live++;
        if (live > 0) play(Sfx::Howl).atPan(0);
    }
    // They arrive one at a time, not all at once.
    if (now > nextPack) {
        nextPack = now + 14 + grng.f01() * 16;
        for (auto &d : dogs) {
            if (d.st != DState::Gone) continue;
            float a = grng.f01() * TAU, dist = 17 + grng.f01() * 9;
            Vec2 spot = world.findOpenSpot(px + portableCos(a) * dist, pz + portableSin(a) * dist);
            d.x = spot.x; d.z = spot.y;
            d.st = DState::Prowl; d.life = 0; d.lost = 0; d.hp = 2;
            d.dispY = world.groundAt(d.x, d.z, py + 1.0f);
            d.repathT = 0; d.wpx = d.x; d.wpz = d.z;
            d.nextBark = now + grng.f01() * 3.0;
            break;
        }
    }
    for (int i = 0; i < MAXDOGS; i++) {
        if (dogs[i].st == DState::Gone) continue;
        if (updateDog(i, dt, now, noise, nsx, nsz)) return;
    }
}

// How far the loudest noise carries this tick, in metres, and where it is.
// Usually the player; a playing deck that was set down is a separate source.
float Sim::packNoise(float &x, float &z) const {
    float noise = 5.0f;
    x = px; z = pz;
    if (sprinting) noise = 20.0f;
    else if (velx * velx + velz * velz > 1.0f) noise = 11.0f;
    if (crouchCur > 0.7f) noise *= 0.45f;
    if (deck.playing && TAPE_NOISE > noise) {
        noise = TAPE_NOISE;
        x = deck.carried ? px : deck.x;
        z = deck.carried ? pz : deck.z;
    }
    if (muzzleT > 0 || gunCd > SHOT_INTERVAL - 0.07f) { noise = 45.0f; x = px; z = pz; }   // a shot
    bool justStruck = false;   // a flare's strike, whichever flare
    for (const FlareProj &f : litFlares) if (f.active && f.burn > FLAREBURN - 0.6f) justStruck = true;
    if (justStruck) { noise = 30.0f; x = px; z = pz; }
    return noise;
}

bool Sim::updateDog(int i, float dt, double now, float noise, float nsx, float nsz) {
    Dog &d = dogs[i];
    float ddx = px - d.x, ddz = pz - d.z;
    float dist = sqrtf(ddx * ddx + ddz * ddz) + 1e-4f;
    d.life += dt;
    if (d.st == DState::Yelp) {          // shot or burned: runs, then despawns
        float rx = d.x - px, rz = d.z - pz, rl = sqrtf(rx * rx + rz * rz) + 1e-4f;
        d.x += rx / rl * 7.5f * dt; d.z += rz / rl * 7.5f * dt;
        d.gait += 7.5f * dt;
        world.collideCircle(d.x, d.z, 0.3f, d.dispY);
        if (d.life > 2.6f) d.st = DState::Gone;
    } else {
        const FlareProj *nearest = nearestLitFlare(d.x, d.z);
        if (nearest) {
            float fx = d.x - nearest->x, fz = d.z - nearest->z;
            if (fx * fx + fz * fz < 5.5f * 5.5f) {
                d.st = DState::Yelp; d.life = 0;
                play(Sfx::Bark, i % 3).atPitch(1.5f);
            }
        }
        float sdx = nsx - d.x, sdz = nsz - d.z;
        bool heard = sqrtf(sdx * sdx + sdz * sdz) < noise;   // the noise source, not the player
        if (d.st == DState::Prowl) {
            if (heard) { d.st = DState::Charge; d.life = 0; d.lost = 0; d.repathT = 0; }
            else if (d.life > 55) d.st = DState::Gone;
        } else if (d.st == DState::Charge) {
            d.lost = heard ? 0.0f : d.lost + dt;
            if (d.lost > 6.0f) { d.st = DState::Prowl; d.life = 0; }
        }
        // A prowling dog wanders on its own; only a charging dog goes to the
        // noise, or noise would have no effect on what they do.
        float spd = (d.st == DState::Charge) ? 5.6f : 2.1f;
        float tgx, tgz;
        if (d.st == DState::Charge) { tgx = nsx; tgz = nsz; }
        else {
            float rdx = d.roamX - d.x, rdz = d.roamZ - d.z;
            if (now > d.nextRoam || rdx * rdx + rdz * rdz < 1.4f * 1.4f) {
                float a = grng.f01() * TAU, r = 9 + grng.f01() * 11;
                Vec2 sp = world.findOpenSpot(d.x + portableCos(a) * r, d.z + portableSin(a) * r);
                d.roamX = sp.x; d.roamZ = sp.y;
                d.nextRoam = now + 9 + grng.f01() * 9;
            }
            tgx = d.roamX; tgz = d.roamZ;
        }
        d.repathT -= dt;
        float wdx = d.wpx - d.x, wdz = d.wpz - d.z;
        if (d.repathT <= 0 || wdx * wdx + wdz * wdz < 0.2f) {
            d.repathT = 0.3;
            int oi, ok;
            float tdx = tgx - d.x, tdz = tgz - d.z;
            if (tdx * tdx + tdz * tdz > 2.0f * 2.0f && !world.lineOfSight(d.x, d.z, tgx, tgz) &&
                world.pathStep(cellOf(d.x), cellOf(d.z), cellOf(tgx), cellOf(tgz), oi, ok))
                { d.wpx = oi * CELL + 1.0f; d.wpz = ok * CELL + 1.0f; }
            else { d.wpx = tgx; d.wpz = tgz; }
        }
        float sx = d.wpx - d.x, sz = d.wpz - d.z, sl = sqrtf(sx * sx + sz * sz) + 1e-4f;
        d.x += sx / sl * spd * dt; d.z += sz / sl * spd * dt;
        d.gait += spd * dt;
        world.collideCircle(d.x, d.z, 0.3f, d.dispY);

        if (now > d.nextBark && dist < 26.0f && deathT <= 0) {
            d.nextBark = now + (d.st == DState::Charge ? 0.7 + grng.f01() * 0.6
                                                      : 3.5 + grng.f01() * 4.0);
            bool dogLos = world.lineOfSight(px, pz, d.x, d.z);
            float sd = clampf((ddx / dist) * r2x + (ddz / dist) * r2z, -1.0f, 1.0f);
            float occ = dogLos ? 1.0f : 0.45f;
            play(dogLos ? Sfx::Bark : Sfx::BarkThrough, i % 3)
                .atPan(sd)
                .atPitch(0.92f + grng.f01() * 0.2f)
                .atVolume(clampf(1.5f * occ / (1.0f + (dogLos ? 0.05f : 0.10f) * dist * dist), 0.0f, 0.95f));
        }
        // Cover does not stop them; only standing still with no playing deck carried.
        if (dist < 1.15f && deathT <= 0 && hurtT <= 0 && !packDeaf()) {
            if (hurtPlayer(now, PACK_BITE, "THE PACK", d.x, d.z)) return true;
        }
    }
    float gy = world.groundAt(d.x, d.z, d.dispY + MAX_STEP);
    d.dispY += (gy - d.dispY) * fminf(1, 10 * dt);
    return false;
}

void Sim::hunterNotice(float x, float z) {
    ent.lastX = x;
    ent.lastZ = z;
    ent.lostT = 0;
}
