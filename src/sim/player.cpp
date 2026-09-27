// The player's body: looking, walking, swimming, falling, hiding, storeys.
#include "sim.h"
#include <cmath>

void Sim::updateFlashlight(const InputFrame &in, float dt) {
    if (in.flashlight) {
        if (flashOn || battery > 0.001f) {
            flashOn = !flashOn;
            play(Sfx::Click).atPitch(flashOn ? 1.0f : 0.85f);
        } else play(Sfx::Click).atPitch(0.55f);   // dead battery: just the switch
    }
    flashCur += ((flashOn ? 1.0f : 0.0f) - flashCur) * fminf(1, 25 * dt);
    if (flashOn) {
        battery = fmaxf(0.0f, battery - dt / 100.0f);
        if (battery <= 0.0f) flashOn = false;
    }
}

void Sim::updateLook(const InputFrame &in) {
    if (in.playing) {
        float sensitivity = 0.0030f * (1 - 0.25f * aimBlend);   // rad per px
        yaw += in.look.x * sensitivity;
        pitch = clampf(pitch - in.look.y * sensitivity, -1.45f, 1.45f);
    }
    fwd = { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) };
    f2x = cosf(yaw); f2z = sinf(yaw);
    r2x = -sinf(yaw); r2z = cosf(yaw);
}

void Sim::updateSprint(bool requested, bool moving, bool crouched, float dt) {
    // Hysteresis, so holding sprint on an empty meter does not flicker.
    if (stamina <= 0.02f) sprintExhausted = true;
    if (stamina >= 0.25f) sprintExhausted = false;
    sprinting = moving && requested && !sprintExhausted && !crouched && !aiming;
    // Zero turns sprint limiting off; the meter, its recovery and the
    // regression check stay live. A limit is 1/seconds (it was 1/16).
    const float SPRINT_DRAIN = 0.0f;
    stamina = clampf(stamina + (sprinting ? -SPRINT_DRAIN * dt : dt / 6.0f), 0, 1);
}

// Holding rise swims up to the surface float and holds it; letting go sinks
// you, which is the dive. Exponential drag behaves the same at any frame rate.
bool Sim::updateSwimming(float dt, bool rise) {
    float bottom = world.groundAt(px, pz, py);
    swimming = world.poolAt(cellOf(px), cellOf(pz)) && bottom < WATER_Y - 1.4f && py < WATER_Y - 0.35f;
    if (!swimming) return false;
    grounded = false;
    float desired = rise ? clampf((FLOAT_PY - py) * 3.0f, -1.5f, 2.2f) : -1.2f;
    vy += (desired - vy) * (1 - expf(-5.0f * dt));
    py += vy * dt;
    if (py < bottom) { py = bottom; vy = fmaxf(vy, 0); }
    // Held at the surface rather than bobbing out of it in a row of jumps.
    if (py > FLOAT_PY && rise) { py = FLOAT_PY; vy = fminf(vy, 0); }
    swimPhase += dt * (1.1f + 0.45f * hypotf(velx, velz));
    return true;
}

void Sim::updateSqueeze(bool held, float dt) {
    if (held) squeezing = true;
    else if (squeezing) {
        // Stay narrow until the full radius fits: expanding inside a gap lets
        // collision push you out through the far side of a thin wall.
        AABB boxes[MAX_NEARBY_AABBS];
        int n = 0, a = cellOf(px), b = cellOf(pz);
        for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z)
            n = world.gatherCellAABBs(a + x, b + z, boxes, MAX_NEARBY_AABBS, n);
        bool fits = true;
        for (int i = 0; i < n; ++i) {
            const AABB &box = boxes[i];
            if (py >= box.top - 0.02f) continue;
            float dx = px - clampf(px, box.minx, box.maxx), dz = pz - clampf(pz, box.minz, box.maxz);
            if (dx * dx + dz * dz < PR * PR) { fits = false; break; }
        }
        if (fits) squeezing = false;
    }
    squeezeBlend += ((squeezing ? 1.0f : 0.0f) - squeezeBlend) * fminf(1, 10 * dt);
}

void Sim::updateMovement(const InputFrame &in, float dt, double now) {
    float ix = 0, iz = 0;
    if (in.forward) { ix += f2x; iz += f2z; }
    if (in.back) { ix -= f2x; iz -= f2z; }
    if (in.right) { ix += r2x; iz += r2z; }
    if (in.left) { ix -= r2x; iz -= r2z; }
    float il = sqrtf(ix * ix + iz * iz);
    bool moving = il > 0.01f;
    if (moving) { ix /= il; iz /= il; }
    if (moving) { ix *= in.moveScale; iz *= in.moveScale; }   // half a stick push is half a speed
    updateSqueeze(in.playing && in.squeeze, dt);
    bool inWater = world.poolAt(cellOf(px), cellOf(pz)) && py < WATER_Y - 0.08f;
    bool crouched = in.crouch && !inWater;
    crouchCur += ((crouched ? 1.0f : 0.0f) - crouchCur) * fminf(1, 10 * dt);
    // Double-tap W: a second press inside W_TAP runs until W is let go.
    wTapT = fmaxf(0, wTapT - dt);
    if (!in.touch && in.forwardPressed) { if (wTapT > 0) wSprint = true; wTapT = W_TAP; }
    if (!in.forward) wSprint = false;
    updateSprint(in.sprint || wSprint, moving, crouched || squeezing, dt);
    boostT = fmaxf(0, boostT - dt);
    swimClimb *= expf(-10 * dt);
    float speed = (squeezing ? 1.1f : sprinting ? 6.8f : crouched ? 1.9f : 3.6f) * (inWater ? 0.55f : 1.0f)
                * (boostT > 0 ? 1.12f : 1.0f);
    if (swimming) speed = sprinting ? 3.5f : 2.4f;
    float tvx = ix * speed, tvz = iz * speed;
    float accel = inWater ? (moving ? 3.8f : 2.8f) : (moving ? 12.0f : 9.0f);
    float drag = inWater ? 1 - expf(-accel * dt) : fminf(1, accel * dt);
    velx += (tvx - velx) * drag;
    velz += (tvz - velz) * drag;
    if (squeezing) { float v = hypotf(velx, velz); if (v > speed) { velx *= speed / v; velz *= speed / v; } }
    float oldX = px, oldZ = pz;
    px += velx * dt; pz += velz * dt;
    world.collideCircle(px, pz, squeezing ? SQUEEZE_R : PR, py);
    if (inWater) {
        float ledge = world.groundAt(px, pz, py);
        if (ledge > py + MAX_STEP) {
            if (py + EYE_H < WATER_Y + 0.03f) {
                // Under water you cannot pass a basin riser; surface and the
                // same edge lifts you out.
                px = oldX; pz = oldZ; velx = velz = 0;
            } else {
                swimClimb += ledge - py; py = ledge; vy = 0; grounded = true; swimming = false;
                int v = grng.ri(0, 2);
                play(Sfx::SplashOut, v).atPitch(0.92f + grng.f01() * 0.16f).atVolume(0.6f);
            }
        }
    }
    // Distance actually covered drives footsteps, bob and records: running into
    // a wall is not a sprint.
    float spd = hypotf(px - oldX, pz - oldZ) / fmaxf(dt, 0.0001f);
    distWalked += spd * dt;

    strafeInput = clampf((velx * r2x + velz * r2z) / 6.0f, -1.0f, 1.0f);
    leanCur += (strafeInput - leanCur) * fminf(1, 6 * dt);
    landDip = fmaxf(0.0f, landDip - dt * 2.4f);

    updateSoftFloor(dt, now);

    // Recomputed after collision; furniture tops count.
    float groundY = world.groundAt(px, pz, py);
    bool wasSwimming = swimming;
    bool afloat = updateSwimming(dt, in.jumpHeld);
    if (afloat && !wasSwimming) {
        int v = grng.ri(0, 2);
        play(Sfx::SplashIn, v).atPitch(0.95f + grng.f01() * 0.1f).atVolume(0.55f);
    }
    if (!afloat && in.jumpPressed && grounded) { vy = inWater ? 4.3f : 5.6f; grounded = false; }
    if (grounded) {
        if (py > groundY + 0.05f && world.poolAt(cellOf(px), cellOf(pz))) { grounded = false; vy = 0; }  // pool edge: drop in
        // Off anything taller than a step you fall; the glide below would
        // lower you off a cabinet as if it were a ramp.
        else if (groundY < py - MAX_STEP) { grounded = false; vy = 0; }
        else {   // stairs, steps, furniture edges: glide to the new floor height
            py += (groundY - py) * fminf(1, 14 * dt);
            if (fabsf(py - groundY) < 0.004f) py = groundY;
        }
    }
    if (!grounded && !afloat) {
        vy -= 20.0f * dt;
        py += vy * dt;
        fallFrom = fmaxf(fallFrom, py);
        if (py <= groundY) {
            py = groundY; grounded = true;
            landFrom(fallFrom - groundY, now);
            if (groundY < -0.1f && world.poolAt(cellOf(px), cellOf(pz))) {
                int v = grng.ri(0, 2);
                play(Sfx::SplashIn, v).atPitch(0.9f + grng.f01() * 0.15f).atVolume(clampf(-vy * 0.12f, 0.4f, 0.8f));
            } else {
                int v = grng.ri(0, 3);    // landing thud
                play(Sfx::Step, v).atPitch(0.62f + grng.f01() * 0.1f).atVolume(clampf(-vy * 0.14f, 0.3f, 0.85f));
            }
            landDip = clampf(-vy * 0.028f, 0.0f, 0.22f);
            vy = 0;
        }
    }
    if (grounded || afloat) fallFrom = py;
    // Past the middle of a flight, or halfway down a fall into the floor below,
    // the storey changes. The 10 cm either side is hysteresis, so standing at
    // the midpoint cannot flicker between floors.
    if (world.storeyH > 0.0f && deathT <= 0) {
        float half = world.storeyH * 0.5f;
        if (py > half + 0.1f) changeStorey(+1, now);
        else if (py < -half - 0.1f) changeStorey(-1, now);
    }

    updateGait(spd, speed, inWater, dt);

    // The window's base FOV, plus the sprint and aim pulls and the slide's
    // narrowing. The pulls are the same vertical degrees at every screen shape;
    // at 1440x850 this is 70 / 79 / 62 / 55.
    float fovT = in.screenFov + (sprinting ? 9.0f : -8.0f * aimBlend) - slide * 15.0f;
    fovT = clampf(fovT, 50.0f, 115.0f);
    fov += (fovT - fov) * fminf(1, 6 * dt);

    updateHiding(spd, dt);
}

// Level 0's rotten patches give way to Level 1 after 0.9 s. They warn first:
// the floor sags under you and the subfloor groans, faster as it worsens.
void Sim::updateSoftFloor(float dt, double now) {
    // The patch is dished, so standing in it puts py below zero; test against
    // the dip, or the trapdoor never fires.
    if (level == 0 && grounded && py > -SOFT_DEPTH - 0.05f && world.softAt(cellOf(px), cellOf(pz))) {
        if (softTimer <= 0.0f) nextGroan = now;
        softTimer += dt;
        if (now >= nextGroan && fellT <= 0) {
            play(Sfx::Groan).atPitch(0.88f + softTimer * 0.30f).atVolume(0.55f + softTimer * 0.45f);
            nextGroan = now + 0.55 - softTimer * 0.30;
        }
        if (softTimer > 0.9f && fellT <= 0 && escapeT <= 0 && deathT <= 0) {
            fellT = 4.0f;
            play(Sfx::SplashIn, 0).atVolume(0.5f).atPitch(0.5f);
            applyLevel(1, now);
            Vector2 spot = world.findOpenSpot(px, pz);
            px = spot.x; pz = spot.y; velx = velz = 0; py = 0.6f; vy = 0; grounded = false;
            ent.st = EState::Hidden; ent.nextSpawn = now + 20;
        }
    } else softTimer = fmaxf(0.0f, softTimer - dt * 2.0f);
    // Eased, and quadratic in the time stood: the last of the give comes fastest.
    softSag += (softTimer / 0.9f * softTimer / 0.9f * 0.065f - softSag) * fminf(1, 9 * dt);
}

// bobPhase counts footfalls: +1 a stride, an integer is a foot landing. The
// step sound fires on the integer crossing, where the -cos bob is lowest.
// Stride grows with speed so the cadence stays 2-3 steps a second. render.cpp
// sways the viewmodel on sinf(bobPhase * PI), one cycle per two footfalls.
void Sim::updateGait(float spd, float speed, bool inWater, float dt) {
    bobAmt = clampf(spd / 5.3f, 0, 1) * (grounded ? 1.0f : 0.0f);
    float strideLen = 0.49f + speed * 0.24f;
    float lastPhase = bobPhase;
    bobPhase += (spd * dt / strideLen) * (grounded ? 1.0f : 0.0f);
    eyeY = EYE_H - 0.55f * crouchCur - landDip - softSag + py - cosf(bobPhase * 6.28318f) * 0.032f * bobAmt;
    if (swimming) {
        // Two swells that never line up and a dip each stroke, fading as you
        // sink below the surface float.
        floatT += dt;
        float surf = clampf(1.0f - (FLOAT_PY - py) / 0.8f, 0, 1);
        float swell = 0.075f * sinf(floatT * 1.55f) + 0.03f * sinf(floatT * 2.6f + 1.3f);
        eyeY = py + EYE_H + swell * surf + sinf(swimPhase) * 0.022f;
        floatRoll = (0.045f * sinf(floatT * 1.05f + 0.6f) + 0.015f * sinf(floatT * 2.3f)) * surf;
        if ((int)(swimPhase / 3.14159f) != (int)((swimPhase - dt * (1.1f + 0.45f * spd)) / 3.14159f) && spd > 0.2f) {
            int v = grng.ri(0, 3);
            play(Sfx::SwimStroke, v).atPitch(0.9f + grng.f01() * 0.2f).atVolume(0.55f);
        }
    } else floatRoll *= expf(-6 * dt);
    eyeY -= swimClimb;
    if (floorf(bobPhase) > floorf(lastPhase)) {
        int v = grng.ri(0, 3);   // wading, a stroke pitched up is a leg pushing through water
        play(inWater ? Sfx::SwimStroke : Sfx::Step, v)
            .atPitch((inWater ? 1.15f : 0.9f) + grng.f01() * 0.22f)
            .atVolume((0.35f + 0.3f * bobAmt) * (inWater ? 1.2f : 1.0f));
    }
    // Wrap at an even whole number of strides, so the footfall crossing and
    // the two-footfall sway are both continuous across it.
    if (bobPhase > 4096.0f) bobPhase -= 4096.0f;
}

// Hiding needs cover and stillness: under HIDE_ENTER to tuck in, and above
// HIDE_BREAK for HIDE_GRACE to lose it. The pack listens for stillness alone.
void Sim::updateHiding(float speed, float dt) {
    nearCover = false;
    if (crouchCur > 0.75f) {
        int hci = cellOf(px), hck = cellOf(pz);
        for (int dx = -1; dx <= 1 && !nearCover; dx++) for (int dz = -1; dz <= 1 && !nearCover; dz++) {
            int a = hci + dx, b = hck + dz;
            if (!hideSpotAt(a, b)) continue;
            float hx = a * CELL + 1.0f, hz = b * CELL + 1.0f;
            float ddx = px - hx, ddz = pz - hz;
            if (ddx * ddx + ddz * ddz < 1.35f * 1.35f) nearCover = true;
        }
    }
    still = speed < HIDE_ENTER;
    if (!nearCover) { hidden = false; hideBreakT = 0; }
    else if (!hidden) { if (speed < HIDE_ENTER) hidden = true; hideBreakT = 0; }
    else if (speed > HIDE_BREAK) {
        hideBreakT += dt;
        if (hideBreakT > HIDE_GRACE) { hidden = false; hideBreakT = 0; }
    } else hideBreakT = 0;
}

// Everything with a position moves by the pitch, so nothing jumps: a flare on
// the landing below is still on that landing. The hunter follows you onto the
// flight if he was close behind, and otherwise loses you.
void Sim::changeStorey(int dir, double now) {
    const float sh = dir * world.storeyH;
    // The flight you are on, looked up before the accessors switch storey.
    VertFeat used; int ucx = 0, ucz = 0; bool onFlight = false;
    {
        int ci = cellOf(px), ck = cellOf(pz);
        ucx = fdiv(ci, CCELLS); ucz = fdiv(ck, CCELLS);
        ChunkData &d = world.data(ucx, ucz);
        int li = ci - ucx * CCELLS, lk = ck - ucz * CCELLS;
        if (d.vfeat[li][lk] >= 0) {
            used = d.feats[d.vfeat[li][lk]];
            onFlight = used.kind == VK_STAIRWELL || used.kind == VK_STAIR || used.stairU >= 0;
        }
    }
    world.setStorey(world.storey + dir);
    py -= sh; fallFrom -= sh; eyeY -= sh;
    for (FlareProj &f : litFlares) {
        if (!f.active) continue;
        f.y -= sh;
        f.onStorey = f.y > -0.6f && f.y < world.storeyH - 0.2f;
        // A flare in the air lands where it is: its arc was against the old floor.
        if (f.flying) { f.flying = false; f.vx = f.vy = f.vz = 0; }
    }
    if (!deck.carried) { deck.y -= sh; if (deck.flying) { deck.flying = false; deck.vx = deck.vy = deck.vz = 0; } }
    for (auto &b : bullets) { b.pos.y -= sh; b.tail.y -= sh; }
    for (auto &im : bulletImpacts) im.pos.y -= sh;
    for (auto &cw : coinsWorld) cw.y -= sh;
    for (auto &c : confetti) c.pos.y -= sh;
    for (auto &d : dogs) d.dispY -= sh;
    ent.dispY -= sh;
    if (ent.st != EState::Hidden && ent.st != EState::Die) {
        if (ent.st == EState::Chase && entDist < 16.0f && onFlight) {
            // On the flight you used: below you if you climbed, above if you came down.
            float u, v;
            if (used.kind == VK_STAIRWELL) { u = dir > 0 ? 1.0f : 3.0f; v = dir > 0 ? 3.0f : 3.2f; }
            else {
                int s0 = used.kind == VK_STAIR ? 0 : used.stairU;
                int s1 = used.kind == VK_STAIR ? used.wu - 1 : used.stairU;
                u = (s0 + s1 + 1) * CELL * 0.5f; v = dir > 0 ? 3.0f : 9.0f;
            }
            Vector3 at = world.featureWorld(used, ucx, ucz, u, 0, v);
            ent.x = at.x; ent.z = at.z;
            ent.dispY = world.groundAt(ent.x, ent.z, py + 1.0f);
            ent.wpx = ent.x; ent.wpz = ent.z; ent.repathT = 0;
        } else {
            ent.st = EState::Hidden;
            ent.nextSpawn = now + 25 + grng.f01() * 25;
        }
    }
    shadowsStale = true;
    manilaNear = inManila = false;
    if (!storeyNoted) {
        storeyNoted = true;
        deckNoteT = 3.4f;
        deckNote = dir > 0 ? "another floor. it looks exactly like the last one."
                           : "a floor below. it looks exactly like the one above.";
    }
}

// Nothing under 2.2 m. A storey costs about 0.4 of the health meter, two most
// of it, three all of it.
void Sim::landFrom(float drop, double now) {
    if (drop < 2.2f || deathT > 0 || inMenu) return;
    float dmg = clampf((drop - 2.2f) * 0.19f, 0.0f, 1.6f);
    fear = fmaxf(fear, 0.7f);
    hurtPlayer(now, dmg, "THE FALL", px, pz);
}
