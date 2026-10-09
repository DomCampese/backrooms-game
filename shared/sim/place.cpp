// The place itself: blackouts, whispers, sanity and its slide, walls that
// move, exits, and Level 0's Manila Room.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

// Full to empty in about 9 / 7.5 / 6.5 / 5 / 4 minutes, standing in the light
// with nothing hunting.
float Sim::sanityDrain(int lv) {
    static const float R[NLEVELS] = { 1.0f / 540, 1.0f / 450, 1.0f / 390, 1.0f / 300, 1.0f / 240 };
    return R[(lv < 0 || lv >= NLEVELS) ? 0 : lv];
}

void Sim::updateAmbience(float dt, double now) {
    if (level != 2 && whisperT <= 0 && now > nextWhisper && ent.st == EState::Hidden) {
        whisperT = 4.5f;
        nextWhisper = now + (70 + grng.f01() * 90) * (0.35 + 0.65 * sanity);
    }
    whisperT = level == 2 ? 0 : fmaxf(0, whisperT - dt);
    ambience.whisper = whisperT > 0 ? 0.55f : 0.0f;

    bool blackout = now < blackoutEnd;
    if (!blackout && now > nextBlackout) {
        blackoutEnd = now + 2.5 + grng.f01() * 4.0;
        nextBlackout = blackoutIn(blackoutEnd, 45, 75);
        if (grng.f01() < 0.45f && ent.st == EState::Hidden)
            ent.nextSpawn = blackoutEnd + 1.5;   // something arrives in the dark
        blackout = true;
    }
    blackoutCur += ((blackout ? 0.02f : 1.0f) - blackoutCur) * fminf(1, 18 * dt);
    ambience.hum = blackout ? 0.12f : inManila ? 0.22f : 1.0f;   // the Manila Room is quiet

    updateRoomSound(blackout);
    if (updateSanity(dt, blackout, now)) return;

    // In the slide, the building starts moving on you.
    if (level != 2 && slide > 0.0f && now > nextShift && !inMenu) {
        nextShift = now + SHIFT_GAP_MIN + grng.f01() * SHIFT_GAP_SPAN * (1.0f - slide);
        if (shiftAWall()) shadowsStale = true;
    }

    for (size_t i = 0; i < confetti.size();) {   // popped balloons: drift down, settle, fade
        Confetti &c = confetti[i];
        c.vel.y -= 3.4f * dt;
        c.vel.x *= 0.96f; c.vel.z *= 0.96f;
        c.pos.x += c.vel.x * dt; c.pos.y += c.vel.y * dt; c.pos.z += c.vel.z * dt;
        c.life -= dt;
        float fy = world.floorY(cellOf(c.pos.x), cellOf(c.pos.z));
        if (c.pos.y < fy + 0.02f) { c.pos.y = fy + 0.02f; c.vel = { 0, 0, 0 }; c.life -= dt * 3.0f; }
        if (c.life <= 0) confetti[i] = confetti.back(), confetti.pop_back();
        else ++i;
    }
}

// The hum swells under a live fitting, and the reverb follows the size of the
// space you are in.
void Sim::updateRoomSound(bool blackout) {
    // Fittings sit at the centres of the level's uLS grid, the one the shader
    // lights from, so the nearest is your position rounded to it.
    float ls = LEVEL_RULES[level].ls;
    float nx = (floorf(px / ls) + 0.5f) * ls, nz = (floorf(pz / ls) + 0.5f) * ls;
    float pd = sqrtf((px - nx) * (px - nx) + (pz - nz) * (pz - nz));
    ambience.panel = (blackout || inManila) ? 0.0f : clampf(1.0f - pd / 4.0f, 0.0f, 1.0f);   // gone by 4 m

    // The mean open run in the four cardinal directions, the same measure the
    // map harness reports.
    int ci = cellOf(px), ck = cellOf(pz);
    const int DIRS[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int total = 0;
    for (auto &d : DIRS) {
        int a = ci, b = ck;
        for (int step = 0; step < ROOM_PROBE; step++) {
            int na = a + d[0], nb = b + d[1];
            if (!world.canStep(a, b, na, nb)) break;
            a = na; b = nb; total++;
        }
    }
    float openRun = (total / 4.0f) * CELL;
    ambience.space = clampf(openRun / (ROOM_PROBE * CELL), 0.0f, 1.0f);
}

// Sanity only drains on its own. Its last SLIDE_FROM is a slide: the view
// narrows, the whispers close in, your pulse takes over, and zero ends the run.
bool Sim::updateSanity(float dt, bool blackout, double now) {
    float drain = sanityDrain(level);
    if (blackout) drain *= 2.2f;
    else if (!flashOn && !anyFlareLit()) drain *= 1.35f;
    if (ent.st == EState::Chase) drain *= 2.6f;
    else if (ent.st != EState::Hidden) drain *= 1.5f;
    if (hidden) drain *= 0.45f;
    else if (crouchCur > 0.7f) drain *= 0.8f;
    drain *= 1.0f + 0.6f * migraine;
    // The Poolrooms and the Manila Room give some back.
    sanity = clampf(sanity + ((level == 2 || inManila) ? 0.018f : -drain) * dt, 0.0f, 1.0f);

    static const char *SANITY_LINES[] = {
        "your hands won't hold still.",
        "the corridor is not the same width twice.",
        "something is breathing in time with you.",
        "you are not going to remember this part.",
    };
    int stage = sanity < 0.12f ? 4 : sanity < 0.30f ? 3 : sanity < 0.50f ? 2
              : sanity < 0.72f ? 1 : 0;
    if (stage > sanityStage) {           // slipped a notch: say so, once
        sanityStage = stage;
        sanityWarnT = 4.0f;
        sanityLine = SANITY_LINES[stage - 1];
    } else if (stage < sanityStage) {
        sanityStage = stage;             // a can walks it back and re-arms the warning
    }
    sanityWarnT = fmaxf(0, sanityWarnT - dt);

    if (sanity < 0.34f && now > nextHeartbeat) {   // your own pulse, quicker and louder in the slide
        play(Sfx::Heartbeat).atVolume(clampf(0.55f + 0.45f * slide, 0.0f, 1.0f));
        nextHeartbeat = now + (1.1 + sanity * 3.4) * (1.0f - 0.45f * slide);
    }
    slide = clampf((SLIDE_FROM - sanity) / SLIDE_FROM, 0.0f, 1.0f);
    if (slide > 0.0f) {
        ambience.whisper = fmaxf(ambience.whisper, 0.30f + 0.70f * slide);
        fear = fmaxf(fear, 0.35f + 0.55f * slide);
    }
    if (sanity <= 0.0f && deathT <= 0 && !inMenu) {
        dieRun(now, "THE PLACE ITSELF", "YOU STOPPED KEEPING TRACK");
        return true;
    }
    return false;
}

// Walls off one doorway near you. Neither cell it joins may be in your line of
// sight, and both keep two other ways out, so no pocket is sealed.
bool Sim::shiftAWall() {
    int ci = cellOf(px), ck = cellOf(pz);
    auto ways = [&](int a, int b) {
        int n = 0;
        if (!blocksEdge(world.wallNVal(a, b))) n++;
        if (!blocksEdge(world.wallNVal(a, b + 1))) n++;
        if (!blocksEdge(world.wallWVal(a, b))) n++;
        if (!blocksEdge(world.wallWVal(a + 1, b))) n++;
        return n;
    };
    for (int tries = 0; tries < 24; tries++) {
        int a = ci + grng.ri(-SHIFT_RING, SHIFT_RING);
        int b = ck + grng.ri(-SHIFT_RING, SHIFT_RING);
        int d2 = (a - ci) * (a - ci) + (b - ck) * (b - ck);
        if (d2 < 9) continue;                       // not right on top of you
        bool west = grng.ri(0, 1) != 0;
        if (blocksEdge(west ? world.wallWVal(a, b) : world.wallNVal(a, b))) continue;   // already not a doorway
        int oa = west ? a - 1 : a, ob = west ? b : b - 1;   // the cell on the far side
        if (world.pillarAt(a, b) || world.pillarAt(oa, ob)) continue;
        if (ways(a, b) < 3 || ways(oa, ob) < 3) continue;
        float cx = a * CELL + 1.0f, cz = b * CELL + 1.0f;
        float ox = oa * CELL + 1.0f, oz = ob * CELL + 1.0f;
        if (world.lineOfSight(px, pz, cx, cz)) continue;
        if (world.lineOfSight(px, pz, ox, oz)) continue;
        world.shiftEdge(a, b, west);
        return true;
    }
    return false;
}

// Both orientations of exit are checked at every cell, and the scan stops at
// the first one taken: after applyLevel the old coordinates mean nothing.
void Sim::updateExits(double now) {
    if (escapeT > 0) return;
    int ci = cellOf(px), ck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++)
    for (int west = 0; west < 2; ++west) {
        int i = ci + dx, k = ck + dz;
        if ((west ? world.wallWVal(i, k) : world.wallNVal(i, k)) != WALL_EXIT) continue;
        float doorX = i * CELL + (west ? 0.0f : CELL * 0.5f);
        float doorZ = k * CELL + (west ? CELL * 0.5f : 0.0f);
        float ddx = px - doorX, ddz = pz - doorZ;
        if (ddx * ddx + ddz * ddz < 0.72f * 0.72f) {
            bool cursed = world.cursedExit(i, k);
            if (wayOpen() && !cursed) { winRun(now); return; }   // the true way out
            // Cursed doors lead to the Red Halls; the rest lead onward.
            play(Sfx::Win);
            escapeT = 6.0f; escapeCount++;
            bankRecords();
            applyLevel(cursed ? 3 : EXIT_NEXT[level], now);
            Vec2 spot = world.findOpenSpot(px, pz);
            px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
            ent.st = EState::Hidden; ent.nextSpawn = now + 30;
            return;
        }
    }
}

const char *const MANILA_NOTES[MANILA_NOTE_COUNT][4] = {
    { "if you are reading this you noclipped in, same as all of us.",
      "this room is safe. the hum is quieter here. sit down a minute.",
      "you will not meet anyone out there. you only meet people in here.",
      "(E for the next page)" },
    { "the only way out of level 0 is the way you came in: noclip.",
      "find a wall that is not quite there. the paper TEARS on it,",
      "just for a second, like a bad tape. walk straight into it.",
      "i chalked an arrow outside the door that faces the nearest one." },
    { "do not drink what is in the carpet. it is not water.",
      "there is almond water in the cupboard under this table. take it.",
      "the lights give you a migraine that follows you out. rest in here.",
      "" },
    { "if the wallpaper starts turning RED, turn around.",
      "red means the red rooms are on the other side of that wall.",
      "the carpet goes coarse and sticky near them. nobody comes back.",
      "- left by the ones before you" },
};

// The second note says the way out is chalked, so chalk it: an arrow outside
// whichever of the room's four doors best faces the nearest uncursed exit,
// looking up to three chunks out.
void Sim::markWayOut() {
    int ci = cellOf(manilaX), ck = cellOf(manilaZ);
    float bestD2 = 1e30f, tx = 0, tz = 0;
    for (int dk = -48; dk <= 48; dk++) for (int di = -48; di <= 48; di++) {
        int i = ci + di, k = ck + dk;
        bool n = world.wallNVal(i, k) == WALL_EXIT, w = world.wallWVal(i, k) == WALL_EXIT;
        if ((!n && !w) || world.cursedExit(i, k)) continue;
        float ex = i * CELL + (n ? 1.0f : 0.0f), ez = k * CELL + (n ? 0.0f : 1.0f);
        float d2 = (ex - manilaX) * (ex - manilaX) + (ez - manilaZ) * (ez - manilaZ);
        if (d2 < bestD2) { bestD2 = d2; tx = ex; tz = ez; }
    }
    if (bestD2 >= 1e30f) return;   // none in reach
    // The doors as (outward direction, centre of the opening).
    const float DOORS[4][4] = {
        {  0, -1, manilaX - 1.0f, manilaZ - 4.0f },   // north, in room cell 7
        {  0,  1, manilaX + 1.0f, manilaZ + 4.0f },   // south, cell 8
        { -1,  0, manilaX - 4.0f, manilaZ + 1.0f },   // west, cell 8
        {  1,  0, manilaX + 4.0f, manilaZ - 1.0f },   // east, cell 7
    };
    float dx = tx - manilaX, dz = tz - manilaZ, dl = sqrtf(dx * dx + dz * dz);
    int pick = 0; float pd = -2;
    for (int q = 0; q < 4; q++) {
        float dot = (DOORS[q][0] * dx + DOORS[q][1] * dz) / dl;
        if (dot > pd) { pd = dot; pick = q; }
    }
    float mx = DOORS[pick][2] + DOORS[pick][0] * 1.3f, mz = DOORS[pick][3] + DOORS[pick][1] * 1.3f;
    float yawTo = atan2f(tz - mz, tx - mx);
    chalk[level].insert(chalk[level].begin(), ChalkMark{ { mx, world.groundAt(mx, mz, 0.0f) + 0.016f, mz }, yawTo, false,
                                                         world.storey });
}

void Sim::updateManila(float dt, double now) {
    manilaNear = level == 0 && world.manilaNear(px, pz, manilaX, manilaZ);
    bool wasIn = inManila;
    inManila = manilaNear && fabsf(px - manilaX) < 3.9f && fabsf(pz - manilaZ) < 3.9f;
    if (inManila && !wasIn && !manilaSeen) { manilaSeen = true; manilaCardT = 5.0f; }
    manilaCardT = fmaxf(0, manilaCardT - dt);
    noteT = fmaxf(0, noteT - dt);
    if (noteT > 0 && !inManila) noteT = fminf(noteT, 1.0f);   // walked off with it: let it fade

    // The hum's migraine builds under Level 0's tubes (about four minutes to
    // the worst of it), eases anywhere else, fastest in the Manila Room, and
    // survives a noclip.
    bool blackout = now < blackoutEnd;
    if (level == 0 && !inManila && !blackout) {
        migraine = fminf(1.0f, migraine + dt / 240.0f * (0.45f + 0.55f * ambience.panel));
    } else {
        migraine = fmaxf(0.0f, migraine - dt / (inManila ? 25.0f : 150.0f));
    }
    if (migraine > 0.35f && !migraineWarned) {
        migraineWarned = true;
        sanityWarnT = 4.0f;
        sanityLine = "the hum has worked its way in behind your eyes.";
    }
}
