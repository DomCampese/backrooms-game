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

// found cassette tapes: someone else's days down here, in fragments
static const char *TAPE_LINES[] = {
    "day 12. the walls hum in b-flat. i've started humming back.",
    "if you find this, don't answer when it says your name.",
    "the vending machines take doubloons. i don't know why i know that.",
    "someone wrote NO CLIP on level 0. i wrote it. i don't remember writing it.",
    "three knocks means it's not him. two knocks means run.",
    "the almond water tastes like almonds. that's the whole warning.",
    "i counted eleven exits today. only one was real.",
    "he's slower than he looks. i am not fast enough anyway.",
};
static constexpr int TAPE_LINE_COUNT = sizeof(TAPE_LINES) / sizeof(TAPE_LINES[0]);

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
    texPartygoer = makePartygoerTex();
    texProps = makePropsTex();
    texScrawl = makeScrawlTex();
    texDog = makeDogTex();
    // per-level surface sets: [floor, ceiling, walls]
    floorTexs[0] = makeCarpetTex(); floorTexs[1] = makeConcreteFloorTex();
    floorTexs[2] = makeTileTex();   floorTexs[4] = makePartyCarpetTex();
    ceilTexs[0] = makeCeilingTex(); ceilTexs[1] = makeConcreteCeilTex(); ceilTexs[2] = floorTexs[2];
    wallTexs[0] = makeWallpaperTex(); wallTexs[1] = makeConcreteWallTex();
    wallTexs[2] = floorTexs[2];       wallTexs[3] = makeRedBrickTex();
    wallTexs[4] = makePartyWallTex();
    floorTexs[3] = floorTexs[1];   // red halls reuse the concrete floor/ceiling in red light
    ceilTexs[3] = ceilTexs[1];
    ceilTexs[4] = makePartyCeilTex();   // the party hall's ceiling has gone dark

    worldShader = LoadShaderFromMemory(WORLD_VS, WORLD_FS);
    // A failed compile silently falls back to raylib's default shader, which
    // renders a plausible-looking but completely wrong scene. Say so instead.
    if (worldShader.id == rlGetShaderIdDefault())
        TraceLog(LOG_ERROR, "world shader failed to compile - see the SHADER lines above");
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
    locEntPos = GetShaderLocation(worldShader, "uEntPos");
    locEntDark = GetShaderLocation(worldShader, "uEntDark");
    locOccOrigin = GetShaderLocation(worldShader, "uOccOrigin");
    locOccN = GetShaderLocation(worldShader, "uOccN");
    locEntBlock = GetShaderLocation(worldShader, "uEntBlock");
    locGloss = GetShaderLocation(worldShader, "uGloss");
    postShader = LoadShaderFromMemory(NULL, POST_FS);
    locPTime = GetShaderLocation(postShader, "uTime");
    locPFear = GetShaderLocation(postShader, "uFear");

    texAO = makeAOStripTex();
    {   // light-occlusion grid: one byte per cell, point-sampled, never filtered
        occBuf.assign(OCC_N * OCC_N, 0);
        Image occImg = { occBuf.data(), OCC_N, OCC_N, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
        texOcc = LoadTextureFromImage(occImg);
        SetTextureFilter(texOcc, TEXTURE_FILTER_POINT);
        SetTextureWrap(texOcc, TEXTURE_WRAP_CLAMP);
    }
    for (int i = 0; i < 6; i++) {
        mats[i] = LoadMaterialDefault();
        mats[i].shader = worldShader;
        // rides the normal-map slot, which DrawMesh binds as "texture2"
        mats[i].maps[MATERIAL_MAP_NORMAL].texture = texOcc;
    }
    mats[3].maps[MATERIAL_MAP_DIFFUSE].texture = texProps;
    mats[4].maps[MATERIAL_MAP_DIFFUSE].texture = texScrawl;   // wall scrawl decals
    mats[5].maps[MATERIAL_MAP_DIFFUSE].texture = texAO;       // baked contact-shadow gradients

    for (int i = 0; i < 4; i++) steps[i] = makeFootstep(100 + i * 17);
    for (int i = 0; i < 4; i++) entSteps[i] = makeFootstep(300 + i * 23);   // heavier, its own gait
    splashes[0] = makeSplash(1, false); splashes[1] = makeSplash(2, false);
    sndBigSplash = makeSplash(3, true);
    sndClick = makeClick();
    sndScare = makeJumpscare();
    sndWin = makeWinChime();
    sndFlare = makeFlareStrike();
    sndShot = makeGunshot();
    sndPop = makeBalloonPop();
    sndHit = makeJumpscare();  SetSoundPitch(sndHit, 1.7f);  SetSoundVolume(sndHit, 0.40f);
    sndKill = makeJumpscare(); SetSoundPitch(sndKill, 0.55f); SetSoundVolume(sndKill, 0.80f);
    sndHeartbeat = makeHeartbeat(); SetSoundVolume(sndHeartbeat, 0.55f);
    sndTape = makeTapeChime();     SetSoundVolume(sndTape, 0.6f);
    sndValve = makeValveTurn();    SetSoundVolume(sndValve, 0.7f);
    sndHowl = makeDogHowl();       SetSoundVolume(sndHowl, 0.5f);
    for (int i = 0; i < 3; i++) sndBarks[i] = makeDogBark(400 + i * 31);

    synth.init();

    world.seed = shotPath ? 1337u : (unsigned)time(nullptr);
    if (const char *seedEnv = getenv("BACKROOMS_SEED")) world.seed = (unsigned)strtoul(seedEnv, nullptr, 10);
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
    SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);   // post CA/bloom sample past the edges: clamp, don't wrap

    nextWhisper = runStart + 45 + grng.f01() * 60;
    snprintf(bestPath, sizeof(bestPath), "%s/.backrooms_best", getenv("HOME") ? getenv("HOME") : ".");
    if (FILE *bf = fopen(bestPath, "r")) {
        if (fscanf(bf, "%d %d %d", &bestEsc, &bestKill, &bestM) != 3) bestEsc = bestKill = bestM = 0;
        if (fscanf(bf, "%d", &bestWins) != 1) bestWins = 0;   // 4th field added later; old files lack it
        if (fscanf(bf, "%d", &bestTapes) != 1) bestTapes = 0; // 5th field, same story
        fclose(bf);
    }

    applyLevel(0);
    if (const char *lvEnv = getenv("BACKROOMS_LEVEL")) applyLevel(atoi(lvEnv) % NLEVELS);   // testing

    inMenu = (shotPath == nullptr) || getenv("BACKROOMS_MENU") != nullptr;   // headless shots skip straight into the run
    if (!shotPath && !inMenu) DisableCursor();
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
    if (winCount > bestWins) { bestWins = winCount; up = true; }
    if (tapes > bestTapes) { bestTapes = tapes; up = true; }
    if (up) if (FILE *bf = fopen(bestPath, "w"))
        { fprintf(bf, "%d %d %d %d %d\n", bestEsc, bestKill, bestM, bestWins, bestTapes); fclose(bf); }
}

// You've banked enough doubloons and found a real door: escape the backrooms.
// Freeze the run's numbers for the win screen, then start a fresh descent.
void Game::winRun(double now) {
    winTime = (float)(now - runStart);
    winM = (int)distWalked; winKills = killCount;
    winCount++; winT = 8.0f;
    PlaySound(sndWin);
    saveBest();
    // a clean new maze, from the top, gear and tallies reset — records persist
    world.seed = shotPath ? 1337u : (unsigned)time(nullptr) ^ (unsigned)(now * 977.0);
    grng = Rng(hash64(world.seed ^ 0xABCDEF));
    applyLevel(0);
    Vector2 sp = world.findOpenSpot(15, 15);
    px = sp.x; pz = sp.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
    yaw = 0.8f; pitch = 0.0f;
    coins = 0; almond = 0; tapes = 0; flares = MAXFLARES; ammo = MAXAMMO; reloadT = 0; battery = 1.0f;
    caughtCount = 0; escapeCount = 0; killCount = 0; distWalked = 0;
    fear = 0; boostT = 0;
    ent.st = EState::Hidden; ent.nextSpawn = now + 30;
    runStart = now;
}

// Title screen: the world drifts by behind the card until any key drops you in.
void Game::updateMenu(double now) {
    float dt = fminf(GetFrameTime(), 0.05f);
    yaw += dt * 0.085f;                         // slow pan across the hall
    pitch = sinf((float)now * 0.22f) * 0.045f;  // faint breathing tilt
    fwd = { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) };
    f2x = cosf(yaw); f2z = sinf(yaw);
    r2x = -sinf(yaw); r2z = cosf(yaw);
    eyeY = 1.62f; bobAmt = 0; leanCur = 0; landDip = 0;
    flashOn = false; flashCur = 0;
    ent.st = EState::Hidden; entDist = 1e9f; entDarkCur = 0;
    fear = 0.0f; blackoutCur = 1.0f;
    streamChunks();
    updateOccupancy();
    int k = GetKeyPressed();                    // F11 (fullscreen) shouldn't count as "begin"
    if ((k != 0 && k != KEY_F11) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) startRun(now);
}

// Leave the title screen and drop into a fresh descent from the top.
void Game::startRun(double now) {
    inMenu = false;
    if (!shotPath) DisableCursor();
    world.seed = shotPath ? 1337u : (unsigned)time(nullptr) ^ (unsigned)(now * 977.0);
    grng = Rng(hash64(world.seed ^ 0xABCDEF));
    applyLevel(0);
    Vector2 sp = world.findOpenSpot(15, 15);
    px = sp.x; pz = sp.y; velx = velz = 0; py = 0; vy = 0; grounded = true;
    yaw = 0.8f; pitch = 0.0f;
    coins = 0; almond = 0; tapes = 0; flares = MAXFLARES; ammo = MAXAMMO; reloadT = 0; battery = 1.0f;
    caughtCount = 0; escapeCount = 0; killCount = 0; distWalked = 0;
    fear = 0; boostT = 0; blackoutCur = 1.0f; blackoutEnd = -1;
    ent.st = EState::Hidden; ent.nextSpawn = now + 30;
    nextBlackout = now + 40 + grng.f01() * 60;
    nextWhisper = now + 45 + grng.f01() * 60;
    nextFlareRegen = now + 75;
    runStart = now;
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
    occValid = false;                                   // different floorplan, different shadows
    for (auto &d : dogs) d.st = DState::Gone;           // the pack doesn't follow you out
    nextPack = GetTime() + (lv == 3 ? 8 + grng.f01() * 8 : 1e9);
    nextHowl = GetTime() + 12 + grng.f01() * 20;
    valvesTurned.clear(); pipesShut = false; valveT = 0;   // a fresh set of standpipes
    taken.clear(); coinsWorld.clear(); chalk.clear();   // it's a different maze down here
    poppedBalloons.clear(); poppedTableBunches.clear(); confetti.clear();
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

bool Game::batteryAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, (uint32_t)world.seed ^ 0xBA77u) % 379 == 0;
}

bool Game::tapeAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, (uint32_t)world.seed ^ 0x7A9Eu) % 401 == 0;
}

// Furniture with enough bulk to tuck in beside: crouch within reach of one of
// these and the hunt loses you, however close it gets.
bool Game::hideSpotAt(int a, int b) {
    switch (world.propAt(a, b)) {
    case 1: case 2: case 5: case 6: case 8: case 9: case 11: case 12: case 13: return true;
    default: return false;
    }
}

// A balloon floats in this cell on LEVEL FUN — mirrors the placement in render.
bool Game::balloonAt(int a, int b, Vector3 &out) {
    if (level != 4) return false;
    if (poppedBalloons.count(cellKey2(a, b))) return false;
    uint32_t h = ih(a, b, (uint32_t)world.seed ^ 0xBA11u);
    if (h % 17 != 0 || world.pillarAt(a, b)) return false;
    out = { a * CELL + 1.0f + (((h >> 4) & 7) / 7.0f - 0.5f) * 0.9f,
            world.wallH - 0.21f,
            b * CELL + 1.0f + (((h >> 7) & 7) / 7.0f - 0.5f) * 0.9f };
    return true;
}

// Balloon bunch knotted to the party table in this cell (the sway the renderer
// adds is left off — it's tiny next to the hit radius, so aim stays honest).
int Game::tableBalloonBunch(int a, int b, Vector3 *pos, Color *cols, Vector3 &tie) {
    if (level != 4 || world.propAt(a, b) != 11) return 0;
    uint32_t h = ih(a, b, (uint32_t)world.seed ^ 0x8A11u);
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

// Fire the revolver in LEVEL FUN and the first balloon on the aim line bursts
// into confetti — a ceiling balloon on its own, or a whole table bunch at once.
void Game::popBalloonsAlongAim() {
    if (level != 4) return;
    auto burst = [&](Vector3 at, Color base, int n) {
        for (int c2 = 0; c2 < n; c2++) {
            float aa = grng.f01() * 6.2831853f, sp = 1.2f + grng.f01() * 2.2f;
            confetti.push_back({ at, { cosf(aa) * sp, 0.6f + grng.f01() * 1.6f, sinf(aa) * sp },
                                 1.3f + grng.f01() * 0.9f,
                                 grng.f01() < 0.5f ? base : PARTY[c2 % 5] });
        }
    };
    for (float d = 0.6f; d < 22.0f; d += 0.35f) {
        float wx = px + fwd.x * d, wy = eyeY + fwd.y * d, wz = pz + fwd.z * d;
        int a = cellOf(wx), b = cellOf(wz);
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            int ca = a + dx, cb = b + dz;
            Vector3 bp;
            if (balloonAt(ca, cb, bp)) {   // a lone ceiling balloon
                float ex = bp.x - wx, ey = bp.y - wy, ez = bp.z - wz;
                if (ex * ex + ey * ey + ez * ez <= 0.24f * 0.24f) {
                    poppedBalloons.insert(cellKey2(ca, cb));
                    SetSoundPitch(sndPop, 0.9f + grng.f01() * 0.3f); SetSoundPan(sndPop, 0.5f); PlaySound(sndPop);
                    burst(bp, PARTY[(ih(ca, cb, (uint32_t)world.seed ^ 0xBA11u) >> 10) % 5], 16);
                    return;
                }
            }
            if (!poppedTableBunches.count(cellKey2(ca, cb))) {   // a table bunch: all of it goes
                Vector3 bpos[4], tie; Color bcol[4];
                int nb = tableBalloonBunch(ca, cb, bpos, bcol, tie);
                for (int k = 0; k < nb; k++) {
                    float ex = bpos[k].x - wx, ey = bpos[k].y - wy, ez = bpos[k].z - wz;
                    if (ex * ex + ey * ey + ez * ez > 0.26f * 0.26f) continue;
                    poppedTableBunches.insert(cellKey2(ca, cb));
                    SetSoundPitch(sndPop, 0.95f + grng.f01() * 0.3f); SetSoundPan(sndPop, 0.5f); PlaySound(sndPop);
                    for (int j = 0; j < nb; j++) burst(bpos[j], bcol[j], 11);
                    return;
                }
            }
        }
    }
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
        SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);   // post CA/bloom sample past the edges: clamp, don't wrap
    }
    if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

    if (inMenu) {                              // title screen: world drifts, any key begins
        updateMenu(now);
        renderScene(now);
        renderUI(now);
        if (shotPath && frame == 600) { TakeScreenshot(shotPath); return false; }   // testing
        return true;
    }

    if (IsKeyPressed(KEY_P) && !shotPath) {
        paused = !paused;
        if (paused) {
            pausedAt = now;
            if (IsCursorHidden()) EnableCursor();
        } else {
            // Every schedule in this game is an absolute timestamp. Left alone,
            // a minute paused would dump a blackout, a whisper and a spawn all
            // at once on resume — so slide them by however long we were away.
            double held = now - pausedAt;
            nextFlareRegen += held;
            nextBlackout += held;
            if (blackoutEnd > 0) blackoutEnd += held;   // -1 is the "no blackout" sentinel
            nextWhisper += held;
            runStart += held;                            // the clock shouldn't count the pause
            ent.nextSpawn += held;
            nextPack += held;                            // ...and the pack keeps its own clocks
            nextHowl += held;
            for (auto &d : dogs) { d.nextBark += held; d.nextRoam += held; }
            DisableCursor();
        }
        SetSoundPitch(sndClick, paused ? 0.7f : 1.1f);
        PlaySound(sndClick);
    }
    if (paused) {
        // frozen, but the ambience stream still has to be fed or it underruns
        synth.growlTarget = 0; synth.hissTarget = 0; synth.whisperTarget = 0;
        synth.update();
        renderScene(now);
        renderUI(now);
        if (shotPath && frame == 600) { TakeScreenshot(shotPath); return false; }   // testing
        return true;
    }

    if (IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_L)) {
        if (flashOn || battery > 0.001f) {
            flashOn = !flashOn;
            SetSoundPitch(sndClick, flashOn ? 1.0f : 0.85f);
            PlaySound(sndClick);
        } else { SetSoundPitch(sndClick, 0.55f); PlaySound(sndClick); }   // dead battery: just the switch
    }
    flashCur += ((flashOn ? 1.0f : 0.0f) - flashCur) * fminf(1, 25 * dt);
    if (flashOn) {
        battery = fmaxf(0.0f, battery - dt / 100.0f);
        if (battery <= 0.0f) flashOn = false;   // it just dies
    }
    if (IsKeyPressed(KEY_F3)) debugHud = !debugHud;
    if (IsKeyPressed(KEY_ESCAPE) && IsCursorHidden()) EnableCursor();
    captureClick = false;
    if (!IsCursorHidden() && !shotPath && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        DisableCursor();
        captureClick = true;   // this click is spoken for — no accidental discharge
    }

    updateLook();
    updateMovement(dt);
    updateDevKeys(now);
    updateWeapons(dt, now);
    updateFlare(dt, now);
    updateInteraction();
    updateAmbience(dt, now);
    updateEntity(dt, now);
    updateDogs(dt, now);
    updateExits(now);
    caughtT = fmaxf(0, caughtT - dt);
    escapeT = fmaxf(0, escapeT - dt);
    killT = fmaxf(0, killT - dt);
    fellT = fmaxf(0, fellT - dt);
    winT = fmaxf(0, winT - dt);
    closeCallT = fmaxf(0, closeCallT - dt);
    tapeFoundT = fmaxf(0, tapeFoundT - dt);
    valveT = fmaxf(0, valveT - dt);
    streamChunks();
    updateOccupancy();

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

    // camera feel: lean into the direction you strafe, ease back when you don't
    strafeInput = clampf((velx * r2x + velz * r2z) / 6.0f, -1.0f, 1.0f);
    leanCur += (strafeInput - leanCur) * fminf(1, 6 * dt);
    landDip = fmaxf(0.0f, landDip - dt * 2.4f);   // the knees straightening after a landing

    // the floor is a lie: linger on a soft patch and it gives way to Level 1
    if (level == 0 && grounded && py > -0.05f && world.softAt(cellOf(px), cellOf(pz))) {
        softTimer += dt;
        if (softTimer > 0.9f && fellT <= 0 && escapeT <= 0 && caughtT <= 0) {
            fellT = 4.0f;
            SetSoundVolume(sndBigSplash, 0.5f); SetSoundPitch(sndBigSplash, 0.5f);
            PlaySound(sndBigSplash);
            applyLevel(1);
            Vector2 spot = world.findOpenSpot(px, pz);
            px = spot.x; pz = spot.y; velx = velz = 0; py = 0.6f; vy = 0; grounded = false;
            ent.st = EState::Hidden; ent.nextSpawn = GetTime() + 20;
        }
    } else softTimer = fmaxf(0.0f, softTimer - dt * 2.0f);

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
            landDip = clampf(-vy * 0.028f, 0.0f, 0.22f);   // knees absorb the drop
            vy = 0;
        }
    }

    // head bob + footsteps (grounded only)
    bobAmt = clampf(spd / 5.3f, 0, 1) * (grounded ? 1.0f : 0.0f);
    stepAcc += spd * dt * (grounded ? 1.0f : 0.0f);
    bobPhase += spd * dt * 1.65f;
    eyeY = 1.62f - 0.55f * crouchCur - landDip + py + sinf(bobPhase * 3.14159f) * 0.045f * bobAmt;
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

    // hiding: crouched and close enough to real cover — checked after collision
    // settles px/pz, so this frame's position is final
    hidden = false;
    if (crouchCur > 0.75f) {
        int hci = cellOf(px), hck = cellOf(pz);
        for (int dx = -1; dx <= 1 && !hidden; dx++) for (int dz = -1; dz <= 1 && !hidden; dz++) {
            int a = hci + dx, b = hck + dz;
            if (!hideSpotAt(a, b)) continue;
            float hx = a * CELL + 1.0f, hz = b * CELL + 1.0f;
            float ddx = px - hx, ddz = pz - hz;
            if (ddx * ddx + ddz * ddz < 1.35f * 1.35f) hidden = true;
        }
    }
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
            ent.st = EState::Chase; ent.gaze = 0; ent.life = 0; ent.unseen = 0; ent.repathT = 0;
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
    muzzleSmoke = fmaxf(0, muzzleSmoke - dt * 0.7f);   // powder haze drifts and thins
    recoil += (0 - recoil) * fminf(1, 10 * dt);
    if (reloadT > 0) {
        reloadT -= dt;
        if (reloadT <= 0) { ammo = MAXAMMO; SetSoundPitch(sndClick, 1.15f); PlaySound(sndClick); }
    }
    if (weapon == 1 && IsCursorHidden() && !captureClick && caughtT <= 0 && reloadT <= 0 && gunCd <= 0 &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (ammo <= 0) { SetSoundPitch(sndClick, 0.7f); PlaySound(sndClick); gunCd = 0.25f; }  // dry fire
        else {
            ammo--; gunCd = 0.42f; muzzleT = 0.09f; recoil = 1.0f; muzzleSmoke = 1.0f;
            PlaySound(sndShot);
            popBalloonsAlongAim();   // in LEVEL FUN, the party takes hits too
            for (int i = 0; i < MAXDOGS; i++) {   // and in the Red Halls, the pack does
                Dog &d = dogs[i];
                if (d.st == DState::Gone || d.st == DState::Yelp) continue;
                float ex = d.x - px, ez = d.z - pz;
                float along = ex * f2x + ez * f2z, perp = fabsf(ex * r2x + ez * r2z);
                if (along <= 0 || perp > 0.6f || along > 30.0f) continue;
                if (!world.lineOfSight(px, pz, d.x, d.z)) continue;
                if (--d.hp <= 0) {
                    d.st = DState::Yelp; d.life = 0;
                    SetSoundPitch(sndBarks[i % 3], 1.6f); SetSoundVolume(sndBarks[i % 3], 0.9f);
                    PlaySound(sndBarks[i % 3]);
                } else {
                    PlaySound(sndHit);
                    float dl = sqrtf(ex * ex + ez * ez) + 1e-4f;
                    d.x += ex / dl * 1.6f; d.z += ez / dl * 1.6f;
                    world.collideCircle(d.x, d.z, 0.3f);
                }
                break;   // one round, one dog
            }
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
        (IsKeyPressed(KEY_Q) || (weapon == 0 && !captureClick && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
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
        bool isBat = !isB && !isC && batteryAt(a, b);
        bool isT = !isB && !isC && !isBat && tapeAt(a, b);
        if (!isB && !isC && !isBat && !isT) continue;
        float bxx = a * CELL + 1.0f, bzz = b * CELL + 1.0f;
        float ddx = px - bxx, ddz = pz - bzz;
        if (ddx * ddx + ddz * ddz < 0.8f * 0.8f) {
            taken.insert(ky);
            if (isB) almond++;
            else if (isC) coins++;
            else if (isBat) {
                battery = clampf(battery + 0.45f, 0, 1);
                SetSoundPitch(sndClick, 0.85f); PlaySound(sndClick);
            } else {
                tapes++;
                tapeFoundT = 3.2f;
                tapeLine = TAPE_LINES[grng.ri(0, TAPE_LINE_COUNT - 1)];
                PlaySound(sndTape);
            }
            if (isB || isC) { SetSoundPitch(sndClick, isB ? 1.3f : 1.6f); PlaySound(sndClick); }
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
    // Red Halls: close a standpipe. A valve cell never holds a prop, so this can
    // never be the same cell as a vending machine — both may read the same press.
    if (IsKeyPressed(KEY_E) && level == 3) {
        bool turned = false;
        for (int dx = -1; dx <= 1 && !turned; dx++) for (int dz = -1; dz <= 1 && !turned; dz++) {
            int a = pci + dx, b = pck + dz;
            if (!world.valveAt(a, b)) continue;
            uint64_t ky = cellKey2(a, b);
            if (valvesTurned.count(ky)) continue;
            float vx = a * CELL + 1.0f, vz = b * CELL + 1.0f;
            float ddx = px - vx, ddz = pz - vz;
            if (ddx * ddx + ddz * ddz > 1.7f * 1.7f) continue;
            valvesTurned.insert(ky);
            valveT = 3.4f;
            turned = true;
            PlaySound(sndValve);
            if ((int)valvesTurned.size() >= VALVES_NEEDED && !pipesShut) {
                pipesShut = true;
                PlaySound(sndWin);
                for (int c2 = 0; c2 < 9; c2++) {   // the pipes give up their cache
                    float aa = c2 * 0.698f + grng.f01();
                    float rr = 1.2f + grng.f01() * 1.1f;
                    coinsWorld.push_back({ px + cosf(aa) * rr, 0, pz + sinf(aa) * rr });
                }
            }
        }
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

    // confetti from popped balloons: drift down, tumble, fade out
    for (size_t i = 0; i < confetti.size();) {
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
        if (hidden) entVisible = false;                            // tucked away — it can walk right past you
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
            if (ent.gaze > 1.6f || (entVisible && entDist < 8) || (entDist < 3.0f && !hidden)) { ent.st = EState::Chase; ent.repathT = 0; }
            else if (ent.life > 24 && !entVisible) ent.st = EState::Hidden, ent.nextSpawn = now + 18 + grng.f01() * 35;
        }
        if (ent.st == EState::Chase) {
            fearT = entDist < 8 ? 1.0f : 0.8f;
            ent.lunge = fmaxf(0, ent.lunge - dt);
            if (entDist < 5.5f && entDist > 1.6f && ent.lunge <= 0 && grng.f01() < dt * 0.5f)
                ent.lunge = 0.55f;   // sudden burst — don't let him get close
            float chaseSpd = 3.3f + 1.0f * clampf(1 - entDist / 25.0f, 0, 1) + (ent.lunge > 0 ? 3.2f : 0.0f);
            // beeline when it can see you; otherwise route around walls on the cell grid
            ent.repathT -= dt;
            float wdx = ent.wpx - ent.x, wdz = ent.wpz - ent.z;
            if (ent.repathT <= 0 || wdx * wdx + wdz * wdz < 0.20f) {
                ent.repathT = 0.25;
                int eci = cellOf(ent.x), eck = cellOf(ent.z);
                int oi, ok;
                if (entDist > 2.5f && !world.lineOfSight(ent.x, ent.z, px, pz) &&
                    world.pathStep(eci, eck, cellOf(px), cellOf(pz), oi, ok) && !(oi == eci && ok == eck)) {
                    ent.wpx = oi * CELL + 1.0f; ent.wpz = ok * CELL + 1.0f;   // head to the next cell on the route
                } else {
                    ent.wpx = px; ent.wpz = pz;   // in sight (or right on top of you): come straight
                }
            }
            float sx = ent.wpx - ent.x, sz = ent.wpz - ent.z, sl = sqrtf(sx * sx + sz * sz) + 1e-4f;
            ent.x += sx / sl * chaseSpd * dt;
            ent.z += sz / sl * chaseSpd * dt;
            world.collideCircle(ent.x, ent.z, 0.38f);
            // you hear him coming: footfalls panned to his bearing, fading with range
            entStepAcc += chaseSpd * dt;
            if (entStepAcc > 1.05f && entDist < 22.0f && caughtT <= 0) {
                entStepAcc -= 1.05f;
                float inv = entDist > 0.01f ? 1.0f / entDist : 0.0f;
                float sd = clampf((ex * inv) * r2x + (ez * inv) * r2z, -1.0f, 1.0f);   // + = to your right
                Sound &s = entSteps[grng.ri(0, 3)];
                SetSoundPan(s, 0.5f - sd * 0.5f);   // raylib pan: 0 right .. 1 left, 0.5 centre
                SetSoundPitch(s, 0.66f + grng.f01() * 0.08f);   // heavy, unhurried
                SetSoundVolume(s, clampf(1.4f / (1.0f + 0.07f * entDist * entDist), 0.0f, 0.9f));
                PlaySound(s);
            }
            ent.unseen = entVisible ? 0 : ent.unseen + dt * (hidden ? 2.4f : (crouchCur > 0.7f ? 1.7f : 1.0f));
            if (ent.unseen > 6 && (entDist > 14 || hidden)) ent.st = EState::Hidden, ent.nextSpawn = now + 25 + grng.f01() * 40;
            if (hidden && entDist < 2.2f && closeCallT <= 0) {   // it's right there and doesn't know
                closeCallT = 3.0f;
                PlaySound(sndHeartbeat);
            }
            if (entDist < 1.25f && !hidden) {   // caught
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
    // it brings the dark: lights die in a pool around it, worse the closer the hunt
    float darkT = (ent.st == EState::Chase) ? 1.0f
                : (ent.st == EState::Stalk)  ? 0.45f
                : (ent.st == EState::Flee)   ? 0.2f : 0.0f;
    if (level == 2) darkT *= 0.5f;   // the poolrooms never fully go out
    entDarkCur += (darkT - entDarkCur) * fminf(1, 1.6f * dt);
    synth.growlTarget = (ent.st == EState::Chase) ? 0.75f * clampf(1 - entDist / 35.0f, 0.1f, 1.0f)
                      : (ent.st == EState::Stalk && entVisible) ? 0.22f * clampf(1 - entDist / 35.0f, 0, 1) : 0.0f;
    synth.update();
}

// The Red Halls pack. Where Clark hunts by sight — stare at him and he comes —
// the dogs hunt by *sound*. Sprinting, firing, striking a flare all carry; going
// still and quiet lets them lose the thread. They're faster than you, so running
// is never the answer: fire turns them, a bullet puts one down, and silence
// eventually bores them.
void Game::updateDogs(float dt, double now) {
    if (level != 3) {                       // they only live here
        for (auto &d : dogs) d.st = DState::Gone;
        return;
    }
    // how much noise you're making right now, in metres of audible radius
    float noise = 5.0f;
    if (sprinting) noise = 20.0f;
    else if (velx * velx + velz * velz > 1.0f) noise = 11.0f;
    if (crouchCur > 0.7f) noise *= 0.45f;
    if (muzzleT > 0 || gunCd > 0.35f) noise = 45.0f;         // a shot in here carries
    if (flare.active && flare.burn > FLAREBURN - 0.6f) noise = 30.0f;

    if (now > nextHowl && caughtT <= 0) {    // the pack calling across the halls
        nextHowl = now + 26 + grng.f01() * 34;
        int live = 0;
        for (auto &d : dogs) if (d.st != DState::Gone) live++;
        if (live > 0) { SetSoundPan(sndHowl, 0.5f); PlaySound(sndHowl); }
    }
    // send them in one at a time so the hall fills up rather than swarming
    if (now > nextPack) {
        nextPack = now + 14 + grng.f01() * 16;
        for (auto &d : dogs) {
            if (d.st != DState::Gone) continue;
            float a = grng.f01() * 6.2831853f, dist = 17 + grng.f01() * 9;
            Vector2 spot = world.findOpenSpot(px + cosf(a) * dist, pz + sinf(a) * dist);
            d.x = spot.x; d.z = spot.y;
            d.st = DState::Prowl; d.life = 0; d.lost = 0; d.hp = 2;
            d.dispY = world.floorY(cellOf(d.x), cellOf(d.z));
            d.repathT = 0; d.wpx = d.x; d.wpz = d.z;
            d.nextBark = now + grng.f01() * 3.0;
            break;
        }
    }

    for (int i = 0; i < MAXDOGS; i++) {
        Dog &d = dogs[i];
        if (d.st == DState::Gone) continue;
        float ddx = px - d.x, ddz = pz - d.z;
        float dist = sqrtf(ddx * ddx + ddz * ddz) + 1e-4f;
        d.life += dt;

        if (d.st == DState::Yelp) {          // shot or burned: bolts, then gone
            float rx = d.x - px, rz = d.z - pz, rl = sqrtf(rx * rx + rz * rz) + 1e-4f;
            d.x += rx / rl * 7.5f * dt; d.z += rz / rl * 7.5f * dt;
            world.collideCircle(d.x, d.z, 0.3f);
            if (d.life > 2.6f) d.st = DState::Gone;
        } else {
            // fire is the one thing that turns them, same as it turns Clark
            if (flare.active) {
                float fx = d.x - flare.x, fz = d.z - flare.z;
                if (fx * fx + fz * fz < 5.5f * 5.5f) {
                    d.st = DState::Yelp; d.life = 0;
                    SetSoundPitch(sndBarks[i % 3], 1.5f); PlaySound(sndBarks[i % 3]);
                }
            }
            bool heard = dist < noise;
            if (d.st == DState::Prowl) {
                if (heard) { d.st = DState::Charge; d.life = 0; d.lost = 0; d.repathT = 0; }
                else if (d.life > 55) d.st = DState::Gone;      // wandered off
            } else if (d.st == DState::Charge) {
                d.lost = heard ? 0.0f : d.lost + dt;
                if (d.lost > 6.0f) { d.st = DState::Prowl; d.life = 0; }
            }
            // Prowling, it noses about the halls on its own business — only a
            // charge comes for you. Homing in while "prowling" would make the
            // noise rules meaningless, since it would arrive either way.
            float spd = (d.st == DState::Charge) ? 5.6f : 2.1f;
            float tgx, tgz;
            if (d.st == DState::Charge) { tgx = px; tgz = pz; }
            else {
                float rdx = d.roamX - d.x, rdz = d.roamZ - d.z;
                if (now > d.nextRoam || rdx * rdx + rdz * rdz < 1.4f * 1.4f) {
                    float a = grng.f01() * 6.2831853f, r = 9 + grng.f01() * 11;
                    Vector2 sp = world.findOpenSpot(d.x + cosf(a) * r, d.z + sinf(a) * r);
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
            world.collideCircle(d.x, d.z, 0.3f);

            if (now > d.nextBark && dist < 26.0f && caughtT <= 0) {
                d.nextBark = now + (d.st == DState::Charge ? 0.7 + grng.f01() * 0.6
                                                          : 3.5 + grng.f01() * 4.0);
                Sound &s = sndBarks[i % 3];
                float sd = clampf((ddx / dist) * r2x + (ddz / dist) * r2z, -1.0f, 1.0f);
                SetSoundPan(s, 0.5f + sd * 0.5f);   // + is to your right; pan 0 = right
                SetSoundPitch(s, 0.92f + grng.f01() * 0.2f);
                SetSoundVolume(s, clampf(1.5f / (1.0f + 0.05f * dist * dist), 0.0f, 0.95f));
                PlaySound(s);
            }
            if (dist < 1.15f && caughtT <= 0 && !hidden) {   // they have you
                PlaySound(sndScare);
                caughtT = 2.4f; caughtCount++;
                saveBest();
                float a = grng.f01() * 6.2831853f;
                Vector2 spot = world.findOpenSpot(px + cosf(a) * 800, pz + sinf(a) * 800);
                px = spot.x; pz = spot.y; velx = velz = 0;
                for (auto &o : dogs) o.st = DState::Gone;
                nextPack = now + 25 + grng.f01() * 20;
            }
        }
        float gy = world.floorY(cellOf(d.x), cellOf(d.z));
        d.dispY += (gy - d.dispY) * fminf(1, 10 * dt);
    }
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
                bool cursed = world.cursedExit(i, k);
                if (wayOpen() && !cursed) { winRun(now); return; }   // the true way out
                // otherwise a normal door: cursed ones drop you into the Red Halls
                PlaySound(sndWin);
                escapeT = 6.0f; escapeCount++;
                saveBest();
                applyLevel(cursed ? 3 : EXIT_NEXT[level]);
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

// Keep the shader's light-occlusion grid centred on the player. Rebuilding is a
// few thousand cell lookups, so only redo it once you've walked far enough that
// the window is worth moving — it stays valid well past fog range either way.
void Game::updateOccupancy() {
    int wantI = cellOf(px) - OCC_N / 2, wantK = cellOf(pz) - OCC_N / 2;
    if (occValid && abs(wantI - occOriginI) < 6 && abs(wantK - occOriginK) < 6) return;
    occOriginI = wantI; occOriginK = wantK;
    world.buildOccupancy(occOriginI, occOriginK, OCC_N, occBuf.data());
    UpdateTexture(texOcc, occBuf.data());
    occValid = true;
}
