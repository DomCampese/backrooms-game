#include "game.h"
#include "textures.h"
#include "sfx.h"
#include "shaders.h"
#include "rlgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>

void Game::init() {
    shotPath = getenv("BACKROOMS_SHOT");
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1440, 850, "THE BACKROOMS — Level 0");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(640, 400);
    InitAudioDevice();
    rlDisableBackfaceCulling();

    texEntity = makeEntityTex();
    texProps = makePropsTex();
    // per-level surface sets: [floor, ceiling, walls]
    floorTexs[0] = makeCarpetTex(); floorTexs[1] = makeConcreteFloorTex();
    floorTexs[2] = makeTileTex();   floorTexs[4] = makePartyCarpetTex();
    ceilTexs[0] = makeCeilingTex(); ceilTexs[1] = makeConcreteCeilTex(); ceilTexs[2] = floorTexs[2];
    wallTexs[0] = makeWallpaperTex(); wallTexs[1] = makeConcreteWallTex();
    wallTexs[2] = floorTexs[2];       wallTexs[3] = makeRedBrickTex();
    wallTexs[4] = makePartyWallTex();
    floorTexs[3] = floorTexs[1];   // red halls reuse the concrete floor/ceiling in red light
    ceilTexs[3] = ceilTexs[1];
    ceilTexs[4] = ceilTexs[0];     // the party is under the same office tiles as Level 0

    worldShader = LoadShaderFromMemory(WORLD_VS, WORLD_FS);
    locTime = GetShaderLocation(worldShader, "uTime");
    locBlackout = GetShaderLocation(worldShader, "uBlackout");
    locViewPos = GetShaderLocation(worldShader, "uViewPos");
    locFlash = GetShaderLocation(worldShader, "uFlash");
    locFlashDir = GetShaderLocation(worldShader, "uFlashDir");
    locAmb = GetShaderLocation(worldShader, "uAmb");
    locFogCol = GetShaderLocation(worldShader, "uFogCol");
    locFogDen = GetShaderLocation(worldShader, "uFogDen");
    locLightCol = GetShaderLocation(worldShader, "uLightCol");
    locLS = GetShaderLocation(worldShader, "uLS");
    locLY = GetShaderLocation(worldShader, "uLY");
    locDead = GetShaderLocation(worldShader, "uDead");
    locLightMul = GetShaderLocation(worldShader, "uLightMul");
    locFlarePos = GetShaderLocation(worldShader, "uFlarePos");
    locFlareInt = GetShaderLocation(worldShader, "uFlareInt");
    locGloss = GetShaderLocation(worldShader, "uGloss");
    postShader = LoadShaderFromMemory(NULL, POST_FS);
    locPTime = GetShaderLocation(postShader, "uTime");
    locPFear = GetShaderLocation(postShader, "uFear");

    for (int i = 0; i < 4; i++) {
        mats[i] = LoadMaterialDefault();
        mats[i].shader = worldShader;
    }
    mats[3].maps[MATERIAL_MAP_DIFFUSE].texture = texProps;

    for (int i = 0; i < 4; i++) steps[i] = makeFootstep(100 + i * 17);
    splashes[0] = makeSplash(1, false); splashes[1] = makeSplash(2, false);
    sndBigSplash = makeSplash(3, true);
    sndClick = makeClick();
    sndScare = makeJumpscare();
    sndWin = makeWinChime();
    sndFlare = makeFlareStrike();
    sndShot = makeGunshot();
    sndHit = makeJumpscare();  SetSoundPitch(sndHit, 1.7f);  SetSoundVolume(sndHit, 0.40f);
    sndKill = makeJumpscare(); SetSoundPitch(sndKill, 0.55f); SetSoundVolume(sndKill, 0.80f);

    synth.init();

    world.seed = shotPath ? 1337u : (unsigned)time(nullptr);
    world.exitTest = getenv("BACKROOMS_EXITS") != nullptr;

    grng = Rng(hash64(world.seed ^ 0xABCDEF));

    Vector2 sp = world.findOpenSpot(15, 15);
    px = sp.x; pz = sp.y;
    if (const char *posEnv = getenv("BACKROOMS_POS")) {   // testing: "x,z,yaw"
        float ex, ez, ey;
        if (sscanf(posEnv, "%f,%f,%f", &ex, &ez, &ey) == 3) {
            Vector2 s2 = world.findOpenSpot(ex, ez);
            px = s2.x; pz = s2.y; yaw = ey;
        }
    }

    nextFlareRegen = GetTime() + 75;

    nextBlackout = 40 + grng.f01() * 60;
    runStart = GetTime();
    rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    nextWhisper = runStart + 45 + grng.f01() * 60;
    snprintf(bestPath, sizeof(bestPath), "%s/.backrooms_best", getenv("HOME") ? getenv("HOME") : ".");
    if (FILE *bf = fopen(bestPath, "r")) {
        if (fscanf(bf, "%d %d %d", &bestEsc, &bestKill, &bestM) != 3) bestEsc = bestKill = bestM = 0;
        fclose(bf);
    }

    applyLevel(0);
    if (const char *lvEnv = getenv("BACKROOMS_LEVEL")) applyLevel(atoi(lvEnv) % NLEVELS);   // testing

    if (!shotPath) DisableCursor();
}

void Game::shutdown() {
    saveBest();
    CloseAudioDevice();
    CloseWindow();
}

void Game::saveBest() {
    bool up = false;
    if (escapeCount > bestEsc) { bestEsc = escapeCount; up = true; }
    if (killCount > bestKill) { bestKill = killCount; up = true; }
    if ((int)distWalked > bestM) { bestM = (int)distWalked; up = true; }
    if (up) if (FILE *bf = fopen(bestPath, "w")) { fprintf(bf, "%d %d %d\n", bestEsc, bestKill, bestM); fclose(bf); }
}

void Game::applyLevel(int lv) {
    level = lv;
    const LevelCfg &c = LEVELS[lv];
    world.unloadAll();
    world.level = lv;
    world.wallH = c.wallH;
    for (int i = 0; i < 3; i++) {
        Texture2D t = i == 0 ? floorTexs[lv] : i == 1 ? ceilTexs[lv] : wallTexs[lv];
        mats[i].maps[MATERIAL_MAP_DIFFUSE].texture = t;
    }
    float ly = c.wallH - 0.12f;
    SetShaderValue(worldShader, locAmb, &c.amb, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFogCol, &c.fogCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFogDen, &c.fogDen, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locLightCol, &c.lightCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locLS, &c.ls, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locLY, &ly, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locDead, &c.dead, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locLightMul, &c.lightMul, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locGloss, &c.gloss, SHADER_UNIFORM_FLOAT);
    synth.tHum = lv == 0 ? 1.0f : lv == 4 ? 0.5f : 0.15f;
    synth.tDrone = lv == 1 ? 1.0f : 0.0f;
    synth.tWater = lv == 2 ? 1.0f : 0.0f;
    synth.tParty = lv == 4 ? 1.0f : 0.0f;
    nextBlackout = lv == 2 ? 1e18 : GetTime() + 30 + grng.f01() * 60;   // no blackouts in the poolrooms
    blackoutEnd = -1;
    taken.clear(); coinsWorld.clear(); chalk.clear();   // it's a different maze down here
    SetWindowTitle(TextFormat("THE BACKROOMS — %s", c.name));
}

bool Game::bottleAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, (uint32_t)world.seed ^ 0xA1A1u) % 331 == 0;
}

bool Game::coinAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, (uint32_t)world.seed ^ 0xC01Du) % 449 == 0;
}

// One frame: advance the simulation in a fixed order, then draw it.
// Returns false when the run should end (headless screenshot captured).
bool Game::tick() {
    float dt = fminf(GetFrameTime(), 0.05f);
    double now = GetTime();
    frame++;

    if (IsWindowResized()) {
        UnloadRenderTexture(rt);
        rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    }
    if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
    if (IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_L)) {
        flashOn = !flashOn;
        SetSoundPitch(sndClick, flashOn ? 1.0f : 0.85f);
        PlaySound(sndClick);
    }
    flashCur += ((flashOn ? 1.0f : 0.0f) - flashCur) * fminf(1, 25 * dt);
    if (IsKeyPressed(KEY_F3)) debugHud = !debugHud;
    if (IsKeyPressed(KEY_ESCAPE) && IsCursorHidden()) EnableCursor();
    if (!IsCursorHidden() && !shotPath && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) DisableCursor();

    updateLook();
    updateMovement(dt);
    updateDevKeys(now);
    updateWeapons(dt, now);
    updateFlare(dt, now);
    updateInteraction();
    updateAmbience(dt, now);
    updateEntity(dt, now);
    updateExits(now);
    caughtT = fmaxf(0, caughtT - dt);
    escapeT = fmaxf(0, escapeT - dt);
    killT = fmaxf(0, killT - dt);
    streamChunks();

    renderScene(now);
    renderUI(now);

    if (shotPath && frame == 600) {
        TakeScreenshot(shotPath);
        printf("fps=%d chunks=%d\n", GetFPS(), (int)world.chunks.size());
        return false;
    }
    return true;
}

void Game::updateLook() {
    // ---- look
    if (IsCursorHidden()) {
        Vector2 md = GetMouseDelta();
        yaw += md.x * 0.0030f;
        pitch = clampf(pitch - md.y * 0.0030f, -1.45f, 1.45f);
    }
    fwd = { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) };
    f2x = cosf(yaw); f2z = sinf(yaw);
    r2x = -sinf(yaw); r2z = cosf(yaw);
}

void Game::updateMovement(float dt) {
    // ---- move
    float ix = 0, iz = 0;
    if (IsKeyDown(KEY_W)) { ix += f2x; iz += f2z; }
    if (IsKeyDown(KEY_S)) { ix -= f2x; iz -= f2z; }
    if (IsKeyDown(KEY_D)) { ix += r2x; iz += r2z; }
    if (IsKeyDown(KEY_A)) { ix -= r2x; iz -= r2z; }
    float il = sqrtf(ix * ix + iz * iz);
    bool moving = il > 0.01f;
    if (moving) { ix /= il; iz /= il; }
    bool crouched = IsKeyDown(KEY_LEFT_CONTROL);
    crouchCur += ((crouched ? 1.0f : 0.0f) - crouchCur) * fminf(1, 10 * dt);
    sprinting = moving && IsKeyDown(KEY_LEFT_SHIFT) && stamina > 0.02f && !crouched;
    stamina = clampf(stamina + (sprinting ? -dt / 10.0f : dt / 6.0f), 0, 1);
    boostT = fmaxf(0, boostT - dt);
    float groundY = world.groundAt(px, pz, py);
    bool inWater = grounded && py < -0.1f && world.poolAt(cellOf(px), cellOf(pz));
    float speed = (sprinting ? 6.8f : crouched ? 1.9f : 3.6f) * (inWater ? 0.55f : 1.0f)
                * (boostT > 0 ? 1.12f : 1.0f);
    float tvx = ix * speed, tvz = iz * speed;
    float accel = moving ? 12.0f : 9.0f;
    velx += (tvx - velx) * fminf(1, accel * dt);
    velz += (tvz - velz) * fminf(1, accel * dt);
    px += velx * dt; pz += velz * dt;
    world.collideCircle(px, pz, PR, py);
    float spd = sqrtf(velx * velx + velz * velz);
    distWalked += spd * dt;

    // jump + floor height (groundY recomputed after collision; furniture tops count)
    groundY = world.groundAt(px, pz, py);
    if (IsKeyPressed(KEY_SPACE) && grounded) { vy = inWater ? 4.3f : 5.6f; grounded = false; }
    if (grounded) {
        if (py > groundY + 0.05f && world.poolAt(cellOf(px), cellOf(pz))) { grounded = false; vy = 0; }  // pool edge: drop in
        else {   // stairs, steps, furniture edges: glide to the new floor height
            py += (groundY - py) * fminf(1, 14 * dt);
            if (fabsf(py - groundY) < 0.004f) py = groundY;
        }
    }
    if (!grounded) {
        vy -= 20.0f * dt;
        py += vy * dt;
        if (py <= groundY) {
            py = groundY; grounded = true;
            if (groundY < -0.1f && world.poolAt(cellOf(px), cellOf(pz))) { SetSoundVolume(sndBigSplash, 0.7f); PlaySound(sndBigSplash); }
            else {
                Sound &s = steps[grng.ri(0, 3)];    // landing thud
                SetSoundPitch(s, 0.62f + grng.f01() * 0.1f);
                SetSoundVolume(s, clampf(-vy * 0.14f, 0.3f, 0.85f));
                PlaySound(s);
            }
            vy = 0;
        }
    }

    // head bob + footsteps (grounded only)
    bobAmt = clampf(spd / 5.3f, 0, 1) * (grounded ? 1.0f : 0.0f);
    stepAcc += spd * dt * (grounded ? 1.0f : 0.0f);
    bobPhase += spd * dt * 1.65f;
    eyeY = 1.62f - 0.55f * crouchCur + py + sinf(bobPhase * 3.14159f) * 0.045f * bobAmt;
    float stepLen = 1.85f + speed * 0.13f;
    if (stepAcc > stepLen) {
        stepAcc -= stepLen;
        Sound &s = inWater ? splashes[grng.ri(0, 1)] : steps[grng.ri(0, 3)];
        SetSoundPitch(s, 0.9f + grng.f01() * 0.22f);
        SetSoundVolume(s, (0.35f + 0.3f * bobAmt) * (inWater ? 1.4f : 1.0f));
        PlaySound(s);
    }
    float fovT = sprinting ? 79.0f : 70.0f;
    fov += (fovT - fov) * fminf(1, 6 * dt);
}

void Game::updateDevKeys(double now) {
    // ---- dev tools (only while the F3 debug HUD is up)
    if (debugHud) {
        if (IsKeyPressed(KEY_B)) {   // force a blackout right now
            blackoutEnd = now + 3.0 + grng.f01() * 3.0;
            nextBlackout = level == 2 ? 1e18 : blackoutEnd + 45 + grng.f01() * 75;
        }
        if (IsKeyPressed(KEY_E)) {   // (re)spawn Clark stalking ~12m ahead
            Vector2 spot = world.findOpenSpot(px + f2x * 12, pz + f2z * 12);
            ent.x = spot.x; ent.z = spot.y;
            ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.hp = 3;
        }
        if (IsKeyPressed(KEY_C)) {   // force chase (spawns him first if hidden)
            if (ent.st == EState::Hidden) {
                Vector2 spot = world.findOpenSpot(px + f2x * 14, pz + f2z * 14);
                ent.x = spot.x; ent.z = spot.y;
                ent.hp = 3;
            }
            ent.st = EState::Chase; ent.gaze = 0; ent.life = 0; ent.unseen = 0;
        }
        if (IsKeyPressed(KEY_H)) {   // banish him
            ent.st = EState::Hidden; ent.nextSpawn = now + 20 + grng.f01() * 20;
        }
        if (IsKeyPressed(KEY_G)) { flares = MAXFLARES; ammo = MAXAMMO; reloadT = 0; }   // refill weapons
        if (IsKeyPressed(KEY_N)) {   // jump to next level (incl. Red Halls)
            applyLevel((level + 1) % NLEVELS);
            Vector2 spot = world.findOpenSpot(px, pz);
            px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
            ent.st = EState::Hidden; ent.nextSpawn = now + 30;
        }
    }
}

void Game::updateWeapons(float dt, double now) {
    // ---- weapons: 1 flare, 2 revolver; wheel cycles; left click uses the selected one
    if (IsKeyPressed(KEY_ONE)) weapon = 0;
    if (IsKeyPressed(KEY_TWO)) weapon = 1;
    wheelCd = fmaxf(0, wheelCd - dt);
    if (wheelCd <= 0 && fabsf(GetMouseWheelMove()) > 0.5f) { weapon ^= 1; wheelCd = 0.25f; }
    gunCd = fmaxf(0, gunCd - dt);
    muzzleT = fmaxf(0, muzzleT - dt);
    recoil += (0 - recoil) * fminf(1, 10 * dt);
    if (reloadT > 0) {
        reloadT -= dt;
        if (reloadT <= 0) { ammo = MAXAMMO; SetSoundPitch(sndClick, 1.15f); PlaySound(sndClick); }
    }
    if (weapon == 1 && IsCursorHidden() && caughtT <= 0 && reloadT <= 0 && gunCd <= 0 &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (ammo <= 0) { SetSoundPitch(sndClick, 0.7f); PlaySound(sndClick); gunCd = 0.25f; }  // dry fire
        else {
            ammo--; gunCd = 0.42f; muzzleT = 0.09f; recoil = 1.0f;
            PlaySound(sndShot);
            // the report carries down every hallway
            if (ent.st == EState::Hidden) ent.nextSpawn = fmin(ent.nextSpawn, now + 5 + grng.f01() * 6);
            else if (ent.st == EState::Stalk) ent.gaze += 0.8f;
            if (ent.st == EState::Stalk || ent.st == EState::Chase || ent.st == EState::Flee) {
                float ex = ent.x - px, ez = ent.z - pz;
                float along = ex * f2x + ez * f2z;          // in front of the muzzle
                float perp = fabsf(ex * r2x + ez * r2z);    // off the aim line
                if (along > 0 && perp < 0.55f && world.lineOfSight(px, pz, ent.x, ent.z)) {
                    ent.hp--;
                    if (ent.hp <= 0) {   // put down
                        PlaySound(sndKill);
                        killT = 3.0f; killCount++;
                        ent.st = EState::Die; ent.life = 0;
                        for (int c2 = 0; c2 < 5; c2++) {   // he spills his doubloons
                            float aa = c2 * 1.2566f + grng.f01();
                            coinsWorld.push_back({ ent.x + cosf(aa) * 0.5f, 0, ent.z + sinf(aa) * 0.5f });
                        }
                        saveBest();
                    } else {             // staggered: knocked back, bolts
                        PlaySound(sndHit);
                        float dd = sqrtf(ex * ex + ez * ez);
                        if (dd > 0.01f) { ent.x += ex / dd * 2.0f; ent.z += ez / dd * 2.0f; }
                        world.collideCircle(ent.x, ent.z, 0.38f);
                        ent.st = EState::Flee; ent.life = 1.4f;
                    }
                }
            }
        }
    }
    if (IsKeyPressed(KEY_R) && weapon == 1 && ammo < MAXAMMO && reloadT <= 0) {
        reloadT = 1.8f;
        SetSoundPitch(sndClick, 0.95f); PlaySound(sndClick);
    }
}

void Game::updateFlare(float dt, double now) {
    // ---- flare weapon
    if (IsCursorHidden() && caughtT <= 0 && flares > 0 &&
        (IsKeyPressed(KEY_Q) || (weapon == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
        flares--;
        flare.active = true; flare.flying = true; flare.burn = FLAREBURN;
        flare.x = px + fwd.x * 0.4f; flare.y = eyeY - 0.15f; flare.z = pz + fwd.z * 0.4f;
        flare.vx = fwd.x * 10.5f; flare.vz = fwd.z * 10.5f; flare.vy = fwd.y * 10.5f + 2.4f;
        PlaySound(sndFlare);
        if (ent.st == EState::Hidden) ent.nextSpawn = fmin(ent.nextSpawn, now + 12 + grng.f01() * 10);  // he hears the strike
    }
    if (flare.active) {
        if (flare.flying) {
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
            if (world.poolAt(cellOf(flare.x), cellOf(flare.z)) && flare.y < -0.10f) {   // hit pool water: fizzles out
                Sound &s = splashes[grng.ri(0, 1)];
                SetSoundPitch(s, 1.1f); SetSoundVolume(s, 0.8f);
                PlaySound(s);
                flare.active = false;
            }
        }
        flare.burn -= dt;
        if (flare.burn <= 0) flare.active = false;
    }
    if (flares >= MAXFLARES) nextFlareRegen = now + 75;   // scavenge a fresh flare over time
    else if (now > nextFlareRegen) { flares++; nextFlareRegen = now + 75; }
    if (flare.active) {
        float fdx = flare.x - px, fdz = flare.z - pz;
        synth.hissTarget = clampf(flare.burn / 1.5f, 0, 1) / (1.0f + 0.05f * (fdx * fdx + fdz * fdz));
    } else synth.hissTarget = 0;
}

void Game::updateInteraction() {
    // ---- pickups, drinking, vending machines, chalk
    int pci = cellOf(px), pck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        int a = pci + dx, b = pck + dz;
        uint64_t ky = cellKey2(a, b);
        if (taken.count(ky)) continue;
        bool isB = bottleAt(a, b), isC = !isB && coinAt(a, b);
        if (!isB && !isC) continue;
        float bxx = a * CELL + 1.0f, bzz = b * CELL + 1.0f;
        float ddx = px - bxx, ddz = pz - bzz;
        if (ddx * ddx + ddz * ddz < 0.8f * 0.8f) {
            taken.insert(ky);
            if (isB) almond++; else coins++;
            SetSoundPitch(sndClick, isB ? 1.3f : 1.6f); PlaySound(sndClick);
        }
    }
    for (size_t c2 = 0; c2 < coinsWorld.size();) {   // spilled doubloons
        float ddx = px - coinsWorld[c2].x, ddz = pz - coinsWorld[c2].z;
        if (ddx * ddx + ddz * ddz < 0.7f * 0.7f) {
            coins++;
            SetSoundPitch(sndClick, 1.6f); PlaySound(sndClick);
            coinsWorld.erase(coinsWorld.begin() + c2);
        } else ++c2;
    }
    if (IsKeyPressed(KEY_THREE) && almond > 0) {   // drink: catch your breath, steady your hands
        almond--; stamina = 1.0f; fear *= 0.35f; boostT = 8.0f;
        SetSoundPitch(splashes[0], 1.5f); SetSoundVolume(splashes[0], 0.5f);
        PlaySound(splashes[0]);
    }
    if (IsKeyPressed(KEY_E)) {   // vending machine: three doubloons a bottle
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            int a = pci + dx, b = pck + dz;
            if (world.propAt(a, b) != 10) continue;
            float mx = a * CELL + 1.0f, mz = b * CELL + 1.0f;
            float ddx = px - mx, ddz = pz - mz;
            if (ddx * ddx + ddz * ddz < 1.6f * 1.6f && coins >= 3) {
                coins -= 3; almond++;
                SetSoundPitch(sndClick, 0.8f); PlaySound(sndClick);
            }
        }
    }
    if (IsKeyPressed(KEY_M)) {   // chalk mark: the only map you get
        chalk.push_back({ px + f2x * 0.5f, world.groundAt(px + f2x * 0.5f, pz + f2z * 0.5f, py) + 0.012f,
                          pz + f2z * 0.5f });
        if (chalk.size() > 128) chalk.erase(chalk.begin());
    }
}

void Game::updateAmbience(float dt, double now) {
    // ---- whispers in the walls
    if (whisperT <= 0 && now > nextWhisper && ent.st == EState::Hidden) {
        whisperT = 4.5f;
        nextWhisper = now + 70 + grng.f01() * 90;
    }
    whisperT = fmaxf(0, whisperT - dt);
    synth.whisperTarget = whisperT > 0 ? 0.55f : 0.0f;

    // ---- blackout events
    bool blackout = now < blackoutEnd;
    if (!blackout && now > nextBlackout) {
        blackoutEnd = now + 2.5 + grng.f01() * 4.0;
        nextBlackout = blackoutEnd + 45 + grng.f01() * 75;
        if (grng.f01() < 0.45f && ent.st == EState::Hidden)
            ent.nextSpawn = blackoutEnd + 1.5;   // something arrives in the dark
        blackout = true;
    }
    blackoutCur += ((blackout ? 0.02f : 1.0f) - blackoutCur) * fminf(1, 18 * dt);
    synth.humTarget = blackout ? 0.12f : 1.0f;
}

void Game::updateEntity(float dt, double now) {
    // ---- entity
    if (shotPath && frame == 300 && ent.st == EState::Hidden) {   // autotest: force a visible spawn
        Vector2 spot = world.findOpenSpot(px + fwd.x * 8, pz + fwd.z * 8);
        ent.x = spot.x; ent.z = spot.y;
        ent.st = EState::Stalk; ent.gaze = -100; ent.life = 0; ent.unseen = 0; ent.hp = 3;
    }
    float fearT = 0.06f;
    entDist = 1e9f;
    bool entVisible = false;
    if (ent.st == EState::Hidden) {
        if (sprinting) ent.nextSpawn -= 1.5 * dt;   // running feet echo a long way
        if (now > ent.nextSpawn) {
            float a = grng.f01() * 6.2831853f;
            float d = 20 + grng.f01() * 10;
            Vector2 spot = world.findOpenSpot(px + cosf(a) * d, pz + sinf(a) * d);
            ent.x = spot.x; ent.z = spot.y;
            ent.st = EState::Stalk; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.hp = 3;
            ent.dispY = world.floorY(cellOf(ent.x), cellOf(ent.z));
        }
    } else {
        float ex = ent.x - px, ez = ent.z - pz;
        entDist = sqrtf(ex * ex + ez * ez);
        float dirDot = (entDist > 0.01f) ? (fwd.x * ex + fwd.z * ez) / entDist : 1;
        entVisible = entDist < 36 && dirDot > 0.86f && world.lineOfSight(px, pz, ent.x, ent.z);
        if (crouchCur > 0.7f && entDist > 7) entVisible = false;   // low and quiet: hard to pick out
        if (flare.active && (ent.st == EState::Stalk || ent.st == EState::Chase)) {
            float fx = ent.x - flare.x, fz = ent.z - flare.z;
            if (fx * fx + fz * fz < 6.0f * 6.0f) {   // fire is the one thing he remembers
                ent.st = EState::Flee; ent.life = 0; ent.gaze = 0;
            }
        }
        if (ent.st == EState::Stalk) {
            ent.life += dt;
            fearT = entVisible ? 0.45f : 0.15f;
            if (entVisible) ent.gaze += dt;
            if (ent.gaze > 1.6f || (entVisible && entDist < 8) || entDist < 3.0f) ent.st = EState::Chase;
            else if (ent.life > 24 && !entVisible) ent.st = EState::Hidden, ent.nextSpawn = now + 18 + grng.f01() * 35;
        }
        if (ent.st == EState::Chase) {
            fearT = entDist < 8 ? 1.0f : 0.8f;
            ent.lunge = fmaxf(0, ent.lunge - dt);
            if (entDist < 5.5f && entDist > 1.6f && ent.lunge <= 0 && grng.f01() < dt * 0.5f)
                ent.lunge = 0.55f;   // sudden burst — don't let him get close
            float chaseSpd = 3.3f + 1.0f * clampf(1 - entDist / 25.0f, 0, 1) + (ent.lunge > 0 ? 3.2f : 0.0f);
            if (entDist > 0.01f) {
                ent.x -= ex / entDist * chaseSpd * dt;
                ent.z -= ez / entDist * chaseSpd * dt;
            }
            world.collideCircle(ent.x, ent.z, 0.38f);
            ent.unseen = entVisible ? 0 : ent.unseen + dt * (crouchCur > 0.7f ? 1.7f : 1.0f);
            if (ent.unseen > 6 && entDist > 14) ent.st = EState::Hidden, ent.nextSpawn = now + 25 + grng.f01() * 40;
            if (entDist < 1.25f) {   // caught
                PlaySound(sndScare);
                caughtT = 2.4f; caughtCount++;
                saveBest();
                float a = grng.f01() * 6.2831853f;
                Vector2 spot = world.findOpenSpot(px + cosf(a) * 800, pz + sinf(a) * 800);
                px = spot.x; pz = spot.y; velx = velz = 0;
                ent.st = EState::Hidden; ent.nextSpawn = now + 30 + grng.f01() * 30;
                entDist = 1e9f;
            }
        }
        if (ent.st == EState::Flee) {   // bolts away from the burning flare
            fearT = 0.18f;
            ent.life += dt;
            float rx = ent.x - (flare.active ? flare.x : px), rz = ent.z - (flare.active ? flare.z : pz);
            float rl = sqrtf(rx * rx + rz * rz);
            if (rl > 0.01f) { ent.x += rx / rl * 6.5f * dt; ent.z += rz / rl * 6.5f * dt; }
            world.collideCircle(ent.x, ent.z, 0.38f);
            if (ent.life > 3.0f) { ent.st = EState::Hidden; ent.nextSpawn = now + 25 + grng.f01() * 35; }
        }
        if (ent.st == EState::Die) {   // shot down: crumples, gone a long while
            fearT = 0.10f;
            ent.life += dt;
            if (ent.life > 1.2f) { ent.st = EState::Hidden; ent.nextSpawn = now + 90 + grng.f01() * 60; }
        }
    }
    if (ent.st != EState::Hidden) {   // he takes the stairs too, smoothly
        float egt = world.floorY(cellOf(ent.x), cellOf(ent.z));
        ent.dispY += (egt - ent.dispY) * fminf(1, 10 * dt);
    }
    fear += (fearT - fear) * fminf(1, 2.2f * dt);
    if (whisperT > 0) fear = fmaxf(fear, 0.22f);
    synth.growlTarget = (ent.st == EState::Chase) ? 0.75f * clampf(1 - entDist / 35.0f, 0.1f, 1.0f)
                      : (ent.st == EState::Stalk && entVisible) ? 0.22f * clampf(1 - entDist / 35.0f, 0, 1) : 0.0f;
    synth.update();
}

void Game::updateExits(double now) {
    // ---- exit doors
    if (escapeT <= 0) {
        int ci = cellOf(px), ck = cellOf(pz);
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            int i = ci + dx, k = ck + dz;
            float doorX = -1, doorZ = -1;
            if (world.wallNVal(i, k) == 2) { doorX = i * CELL + 1.0f; doorZ = k * CELL; }
            else if (world.wallWVal(i, k) == 2) { doorX = i * CELL; doorZ = k * CELL + 1.0f; }
            else continue;
            float ddx = px - doorX, ddz = pz - doorZ;
            if (ddx * ddx + ddz * ddz < 0.72f * 0.72f) {
                PlaySound(sndWin);
                escapeT = 6.0f; escapeCount++;
                saveBest();
                applyLevel(EXIT_NEXT[level]);     // the exit leads deeper
                Vector2 spot = world.findOpenSpot(px, pz);
                px = spot.x; pz = spot.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
                ent.st = EState::Hidden; ent.nextSpawn = now + 30;
            }
        }
    }
}

void Game::streamChunks() {
    // ---- chunk streaming
    int pcx = fdiv(cellOf(px), CCELLS), pcz = fdiv(cellOf(pz), CCELLS);
    int budget = (frame < 3) ? 64 : 4;
    for (int r = 0; r <= 2 && budget > 0; r++)
        for (int dx = -r; dx <= r && budget > 0; dx++)
            for (int dz = -r; dz <= r && budget > 0; dz++) {
                if (std::max(abs(dx), abs(dz)) != r) continue;
                ChunkData &d = world.data(pcx + dx, pcz + dz);
                if (!d.built) { world.ensureMesh(pcx + dx, pcz + dz); budget--; }
            }
    if (frame % 90 == 0) world.unloadFar(pcx, pcz, 5);
}
