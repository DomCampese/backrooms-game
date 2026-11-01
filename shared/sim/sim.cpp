// The tick, and the shape of a run: levels, descents, death and escape.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

void Sim::step(const InputFrame &in, float dt, double now) {
    updateFlashlight(in, dt);
    // The pre-placed marks are laid the tick after arrival: applyLevel runs
    // before a transition moves the player, so px/pz are still the old level's.
    if (chalkSeedPending) { seedStrangerChalk(); chalkSeedPending = false; }
    updateLook(in);
    updateMovement(in, dt, now);
    updateDevKeys(in.dev, now);
    updateWeapons(in, dt, now);
    updateBullets(dt);
    updateFlare(in, dt, now);
    updateTapeDeck(in, dt, now);
    updateInteraction(in);
    updateDrink(dt);
    updateAmbience(dt, now);
    updateManila(dt, now);
    updateCrates(now);
    AudioEvent &loops = audioEvent(AudioEvent::LOOPS);
    loops.loops = loopCue();
    loops.dt = dt;
    updateEntity(dt, now, in.forceSpawn);
    updateDogs(dt, now);
    updateExits(now);
    updateTimers(dt);
}

void Sim::updateTimers(float dt) {
    deathT = fmaxf(0, deathT - dt);
    updateHealth(dt);
    escapeT = fmaxf(0, escapeT - dt);
    killT = fmaxf(0, killT - dt);
    fellT = fmaxf(0, fellT - dt);
    winT = fmaxf(0, winT - dt);
    closeCallT = fmaxf(0, closeCallT - dt);
    tapeFoundT = fmaxf(0, tapeFoundT - dt);
    valveT = fmaxf(0, valveT - dt);
}

void Sim::updateHealth(float dt) {
    hurtT = fmaxf(0, hurtT - dt);
    sinceHurt += dt;
    if (sinceHurt > REGEN_DELAY) health = fminf(1, health + REGEN_RATE * dt);
}

void Sim::menuDrift(float dt, double now) {
    yaw += dt * 0.085f;                         // slow pan
    pitch = portableSin((float)now * 0.22f) * 0.045f;  // slow tilt
    fwd = { portableCos(pitch) * portableCos(yaw), portableSin(pitch), portableCos(pitch) * portableSin(yaw) };
    f2x = portableCos(yaw); f2z = portableSin(yaw);
    r2x = -portableSin(yaw); r2z = portableCos(yaw);
    eyeY = EYE_H; bobAmt = 0; leanCur = 0; landDip = 0; softTimer = 0; softSag = 0;
    flashOn = false; flashCur = 0;
    ent.st = EState::Hidden; entDist = 1e9f; entDarkCur = 0;
    fear = 0.0f; blackoutCur = 1.0f;
    deathT = fmaxf(0, deathT - dt);
}

void Sim::menuBegin(const InputFrame &in, double now) {
    // Ignore input for DEATH_CARD_HOLD, so a key still held from the death
    // does not dismiss the card.
    if (deathT > DEATH_CARD - DEATH_CARD_HOLD) return;
    if (in.begin) startRun(now);
}

void Sim::setPaused(bool on, double now) {
    paused = on;
    if (paused) pausedAt = now;
    else {
        // Every schedule is an absolute time, so shift each by the pause length.
        // Otherwise a long pause makes a blackout, a whisper and a spawn all
        // fire on resume.
        double held = now - pausedAt;
        nextFlareRegen += held;
        nextBlackout += held;
        if (blackoutEnd > 0) blackoutEnd += held;   // -1: no blackout running
        nextWhisper += held;
        nextDistant += held;
        runStart += held;
        ent.nextSpawn += held;
        nextPack += held;
        nextHowl += held;
        nextHeartbeat += held;
        for (auto &d : dogs) { d.nextBark += held; d.nextRoam += held; }
    }
    play(Sfx::Click).atPitch(paused ? 0.7f : 1.1f);
}

double Sim::blackoutIn(double now, double lead, double span) {
    double t = now + lead + grng.f01() * span;
    return noBlackout ? BLACKOUT_NEVER : t;
}

void Sim::startRun(double now) {
    inMenu = false;
    beginDescent(now);
    // applyLevel's schedule counted from when the menu opened; reset it so
    // the run starts with no blackout.
    blackoutCur = 1.0f; blackoutEnd = -1;
    nextBlackout = blackoutIn(now, 40, 60);
    nextWhisper = now + 45 + grng.f01() * 60;
    nextFlareRegen = now + FLARE_REGEN;
}

void Sim::beginDescent(double now) {
    world.seed = fixedSeed ? 1337u : clockSeed ^ (unsigned)(now * 977.0);
    grng = Rng(hash64(world.seed ^ 0xABCDEF));
    applyLevel(0, now);
    Vec2 sp = world.findOpenSpot(15, 15);
    px = sp.x; pz = sp.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
    yaw = 0.8f; pitch = 0.0f;
    coins = 0; almond = 0; tapes = 0; keys = 0; flares = MAXFLARES; ammo = MAXAMMO; reloadT = 0; battery = 1.0f;
    deck = TapeDeck{}; stopVoice();
    escapeCount = 0; killCount = 0; distWalked = 0;
    deepest = level;
    for (int &v : visits) v = 0;
    slide = 0; nextShift = now + 30;
    pipesPaid = false;
    // deathCount and winCount stay: they count this session's runs, and
    // dieRun increments deathCount just before calling here.
    fear = 0; boostT = 0;
    stamina = 1; sprintExhausted = false; aiming = false; aimBlend = 0;
    health = 1; hurtT = 0; sinceHurt = 0;
    sanity = 1.0f; sanityStage = 0; sanityWarnT = 0; sanityLine = "";
    migraine = 0; migraineWarned = false; notesRead = false; manilaSeen = false; noteT = 0;
    drinkT = 0; drinkLanded = false; nextHeartbeat = now + 20;
    ent.st = EState::Hidden; ent.nextSpawn = now + 30;
    for (auto &c : chalk) c.clear();
    for (bool &b : chalkSeeded) b = false;
    chalkSeedPending = true;
    runStart = now;
}

// Two arrows per level, laid once a descent, 18-40 m out.
void Sim::seedStrangerChalk() {
    chalkSeeded[level] = true;
    Rng r(hash64((uint64_t)world.seed ^ ((uint64_t)level * 0x9E3779B97F4A7C15ULL) ^ 0xC4A15ULL));
    for (int i = 0; i < 2; i++) {
        float a = r.f01() * TAU, d = 18 + r.f01() * 22;
        Vec2 spot = world.findOpenSpot(px + portableCos(a) * d, pz + portableSin(a) * d);
        chalk[level].push_back({{ spot.x, world.groundAt(spot.x, spot.y, 0.0f) + 0.016f, spot.y },
                                r.f01() * TAU, false, world.storey });
    }
}

// The platform applies the level's look (surfaces, shader uniforms, window
// title) when it sees levelEntries change.
void Sim::applyLevel(int lv, double now) {
    level = lv;
    levelEntries++;
    if (LEVEL_DEPTH[lv] > LEVEL_DEPTH[deepest]) deepest = lv;
    // Read before the increment: the first arrival is visit 0, so a fresh
    // descent at a given seed generates the same maze as before.
    world.visit = (unsigned)visits[lv];
    visits[lv]++;
    // A drink in progress still gives its sanity rather than being lost.
    if (drinkT > 0 && !drinkLanded) sanity = clampf(sanity + 0.34f + 0.04f * level, 0.0f, 1.0f);
    drinkT = 0; drinkLanded = false;
    // Carried items come along. A deck or flare left on the previous level
    // would otherwise appear in the new maze at its old coordinates.
    deck.carried = true; deck.flying = false; deck.playing = false; deck.t = 0;
    for (FlareProj &f : litFlares) f.active = f.flying = false;
    stopVoice();
    const LevelRules &c = LEVEL_RULES[lv];
    world.unloadAll();
    world.level = lv;
    world.wallH = c.wallH;
    world.storeyH = c.storeyH;
    world.storey = world.qs = 0;       // every level begins on its storey 0
    storeyNoted = false;
    ambience.humLevel = lv == 0 ? 1.0f : lv == 4 ? 0.5f : lv == 2 ? 0.035f : 0.15f;
    ambience.droneLevel = lv == 1 ? 1.0f : 0.0f;
    nextBlackout = lv == 2 ? BLACKOUT_NEVER : blackoutIn(now, 30, 60);   // no blackouts in the Poolrooms
    blackoutEnd = -1;
    shadowsStale = true;
    for (auto &d : dogs) d.st = DState::Gone;
    nextPack = now + (lv == 3 ? 8 + grng.f01() * 8 : 1e9);
    nextHowl = now + 12 + grng.f01() * 20;
    distantRng = Rng(hash64((uint64_t)world.seed ^ ((uint64_t)lv << 40) ^ ((uint64_t)world.visit << 48) ^ 0xD157A47ULL));
    nextDistant = now + DISTANT_LEAD + distantRng.f01() * DISTANT_SPAN;
    valvesTurned.clear(); pipesShut = false; valveT = 0;
    taken.clear(); coinsWorld.clear();
    // Chalk is kept per level for the whole descent; beginDescent clears it.
    chalkSeedPending = !chalkSeeded[lv];
    // Left set, a Manila Room on the previous level would keep restoring sanity here.
    manilaNear = inManila = false; noteT = 0; manilaCardT = 0;
    cratesOpened.clear(); crateWasDark = false;
    swimming = false; swimPhase = swimClimb = 0; floatRoll = 0;
    squeezing = false; squeezeBlend = 0;
    bullets.clear(); bulletImpacts.clear();
    poppedBalloons.clear(); poppedTableBunches.clear(); confetti.clear();
}

void Sim::winRun(double now) {
    winTime = (float)(now - runStart);
    winM = (int)distWalked; winKills = killCount;
    winCount++; winT = 8.0f;
    play(Sfx::Win);
    bankRecords();
    beginDescent(now);
}

void Sim::dieRun(double now, const char *by, const char *title) {
    deathBy = by;
    deathTitle = title;
    deathLevel = level;
    deathTime = (float)(now - runStart);
    deathM = (int)distWalked;
    deathKills = killCount;
    deathCount++;
    deathT = DEATH_CARD;
    if ((int)deathTime > best.longestRun) best.longestRun = (int)deathTime;
    bankRecords();
    play(Sfx::Scare);
    stopVoice();
    beginDescent(now);          // the next run's maze is ready behind the card
    inMenu = true;
}

bool Sim::hurtPlayer(double now, float dmg, const char *by, float fromX, float fromZ) {
    health -= dmg;
    if (health <= 0.001f) { health = 0; dieRun(now, by); return true; }
    hurtT = HURT_GRACE; sinceHurt = 0;
    // Pushed away, so the attacker is not overlapping the player when the grace ends.
    float dx = px - fromX, dz = pz - fromZ, d = portableHypot(dx, dz);
    if (d > 0.01f) { velx = dx / d * 6.0f; velz = dz / d * 6.0f; }
    play(Sfx::Groan).atPitch(1.3f);   // the floor groan clip, pitched up, as the hit sound
    fear = fmaxf(fear, 0.9f);
    return false;
}

void Sim::bankRecords() {
    if (!keepRecords) return;
    bool up = false;
    if (escapeCount > best.escapes) { best.escapes = escapeCount; up = true; }
    if (killCount > best.kills) { best.kills = killCount; up = true; }
    if ((int)distWalked > best.metres) { best.metres = (int)distWalked; up = true; }
    if (winCount > best.wins) { best.wins = winCount; up = true; }
    if (tapes > best.tapes) { best.tapes = tapes; up = true; }
    if (LEVEL_DEPTH[deepest] > LEVEL_DEPTH[best.deepest]) { best.deepest = deepest; up = true; }
    if (up) recordsChanged = true;
}

LoopCue Sim::loopCue() {
    return { level == 2 && !inMenu && eyeY < WATER_Y && world.poolAt(cellOf(px), cellOf(pz)),
             level == 4 && !inMenu };
}

AudioEvent &Sim::audioEvent(AudioEvent::Kind kind) {
    audio.emplace_back();
    audio.back().kind = kind;
    return audio.back();
}

AudioEvent &Sim::play(Sfx sfx, int variant) {
    AudioEvent &e = audioEvent(AudioEvent::PLAY);
    e.sfx = sfx;
    e.variant = (uint8_t)variant;
    return e;
}

void Sim::stopVoice() { audioEvent(AudioEvent::VOICE_STOP); }

void Sim::updateDevKeys(const DevKeys &dev, double now) {
    if (dev.blackout) {
        blackoutEnd = now + 3.0 + grng.f01() * 3.0;
        nextBlackout = level == 2 ? BLACKOUT_NEVER : blackoutIn(blackoutEnd, 45, 75);
    }
    if (dev.spawnAhead) {
        Vec2 spot = world.findOpenSpot(px + f2x * 12, pz + f2z * 12);
        ent.x = spot.x; ent.z = spot.y;
        ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.hp = 3; ent.stagger = 0;
    }
    if (dev.chase) {
        if (ent.st == EState::Hidden) {
            Vec2 spot = world.findOpenSpot(px + f2x * 14, pz + f2z * 14);
            ent.x = spot.x; ent.z = spot.y;
            ent.hp = 3;
        }
        ent.st = EState::Chase; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.repathT = 0;
    }
    if (dev.banish) {
        ent.st = EState::Hidden; ent.nextSpawn = now + 20 + grng.f01() * 20;
    }
    if (dev.refill) { flares = MAXFLARES; ammo = MAXAMMO; reloadT = 0; }
    if (world.storeyH > 0.0f && (dev.storeyUp || dev.storeyDown)) {
        changeStorey(dev.storeyUp ? 1 : -1, now);
        Vec2 spot = world.findOpenSpot(px, pz);
        px = spot.x; pz = spot.y; py = 0; vy = 0; grounded = true; fallFrom = 0;
    }
    if (dev.nextLevel) {
        applyLevel((level + 1) % NLEVELS, now);
        Vec2 spot = world.findOpenSpot(px, pz);
        px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
        ent.st = EState::Hidden; ent.nextSpawn = now + 30;
    }
}
