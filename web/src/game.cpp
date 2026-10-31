#include "game.h"
#include "textures.h"
#include "shaders.h"
#include "input.h"
#include "port/stream.h"
#include "sim/records.h"
#include "rlgl.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#ifdef __APPLE__
#include <CoreGraphics/CGDisplayConfiguration.h>
#endif
#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

void Game::init() {
    shotPath = getenv("BACKROOMS_SHOT");
    benchmark = getenv("BACKROOMS_BENCH") != nullptr;
    cleanShot = getenv("BACKROOMS_CLEAN") != nullptr;
    if (const char *t = getenv("BACKROOMS_TIME")) captureTime = (float)atof(t);
    if (const char *sf = getenv("BACKROOMS_SHOTFRAME")) shotFrame = atoi(sf);
    // Headless captures schedule no blackouts. Blackouts run on the wall clock,
    // and at 2 fps a capture often lands in one and comes out black.
    // BACKROOMS_NOBLACKOUT=0 turns them back on.
    SimStart start;
    start.noBlackout = shotPath != nullptr;
    if (const char *nb = getenv("BACKROOMS_NOBLACKOUT")) start.noBlackout = atoi(nb) != 0;
    start.fixedSeed = shotPath != nullptr;
    start.keepRecords = !shotPath && !benchmark;   // automated runs must not change the player's records
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags((benchmark ? 0 : FLAG_VSYNC_HINT) | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
#ifdef __APPLE__
    // raylib 5.5 can call a null GL function inside InitWindow when macOS has
    // no active display (e.g. a closed laptop used remotely). Check beforehand.
    uint32_t displays=0;
    if (CGGetActiveDisplayList(0,nullptr,&displays)!=kCGErrorSuccess || displays==0) {
        fprintf(stderr,"Cannot start the game: macOS has no active display. Open the lid or connect a monitor.\n");
        std::exit(EXIT_FAILURE);
    }
#endif
    InitWindow(1440, 850, "THE BACKROOMS");
    SetExitKey(KEY_NULL);
#ifdef PLATFORM_WEB
    // raylib's web resize callback clamps the canvas to the minimum size first,
    // so a 640x400 minimum rendered a 412 px phone at 640 wide and CSS squashed
    // it horizontally. In a browser a small minimum is safe.
    SetWindowMinSize(240, 240);
#else
    SetWindowMinSize(640, 400);
#endif
    InitAudioDevice();
    rlDisableBackfaceCulling();

    texParticle = makeParticleTex();
    texClark = makeClarkTex();
    texEntity = makeSmilerTex(false); texEntityGlow = makeSmilerTex(true);
    texPartygoer = makePartygoerTex();
    texProps = makePropsTex();
    texScrawl = makeScrawlTex();
    texFixtures = makeFixturesTex();
    texDog = makeDogTex();
    texAlmondWrap = makeAlmondWrapTex();
    canMesh = buildCanMesh();
    crateMesh = buildCrateMesh();
    crateLidMesh = buildCrateLidMesh();
    revolver.load();
    flareMesh = buildFlareMesh();
    texDeck = makeDeckTex();
    deckMesh = buildDeckMesh();
    reelMesh = buildReelMesh();
    deckLampMesh = buildDeckLampMesh();
    // Alpha 250, not 255: the shader applies its world-space variation (uMacro,
    // uBoard) only where detail alpha is above 0.99 (tiled world surfaces).
    // Object maps are 128. This neutral map is used by decals, fixtures and
    // held objects, so it must fail that test but still count as
    // level-relative gloss (above 0.75).
    Image neutral = GenImageColor(1, 1, {128, 128, 255, 250});
    neutralDetail = LoadTextureFromImage(neutral);
    UnloadImage(neutral);

    worldShader = LoadShaderFromMemory(WORLD_VS, worldFragmentShader().c_str());
    // A failed compile falls back to raylib's default shader without an error,
    // so log one.
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
    locVary = GetShaderLocation(worldShader, "uVary");
    locFaulty = GetShaderLocation(worldShader, "uFaulty");
    locWet = GetShaderLocation(worldShader, "uWet");
    locWetFrom = GetShaderLocation(worldShader, "uWetFrom");
    locRoomMask = GetShaderLocation(worldShader, "uRoomMask");
    locLamp = GetShaderLocation(worldShader, "uLamp");
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
    locStoreyH = GetShaderLocation(worldShader, "uStoreyH");
    locStorey = GetShaderLocation(worldShader, "uStorey");
    locDrawRel = GetShaderLocation(worldShader, "uDrawRel");
    locLampCol = GetShaderLocation(worldShader, "uLampCol");
    locMacro = GetShaderLocation(worldShader, "uMacro");
    locBoard = GetShaderLocation(worldShader, "uBoard");
    locObjRefl = GetShaderLocation(worldShader, "uObjRefl");
    locIR = GetShaderLocation(worldShader, "uIR");
    postShader = LoadShaderFromMemory(NULL, POST_FS);
    if (postShader.id == rlGetShaderIdDefault())
        TraceLog(LOG_ERROR, "post shader failed to compile - see the SHADER lines above");
    locPTime = GetShaderLocation(postShader, "uTime");
    locPFear = GetShaderLocation(postShader, "uFear");
    locPWater = GetShaderLocation(postShader, "uWater");
    locPMigraine = GetShaderLocation(postShader, "uMigraine");

    texAO = makeAOStripTex();
    {   // light-occlusion grid: four bytes per cell (this storey, below, above,
        // spare), then as many rows again of fitting masks (World::buildOccupancy);
        // point-sampled, never filtered
        occBuf.assign(OCC_N * OCC_N * 4 * 2, 0);
        Image occImg = { occBuf.data(), OCC_N, OCC_N * 2, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        texOcc = LoadTextureFromImage(occImg);
        SetTextureFilter(texOcc, TEXTURE_FILTER_POINT);
        SetTextureWrap(texOcc, TEXTURE_WRAP_CLAMP);
    }
    // Every material needs the occupancy grid, or its shadows are wrong: an
    // unbound sampler reads as white, which the shader treats as walls
    // everywhere, and the object turns black. The count comes from the enum, so
    // a new slot is always initialised here.
    for (int i = 0; i < MAT_COUNT; i++) {
        mats[i] = LoadMaterialDefault();
        mats[i].shader = worldShader;
        // rides the normal-map slot, which DrawMesh binds as "texture2"
        mats[i].maps[MATERIAL_MAP_NORMAL].texture = texOcc;
        // texture1 is free; texture2 must remain the occupancy grid.
        mats[i].maps[MATERIAL_MAP_SPECULAR].texture = neutralDetail;
    }
    // The floor, ceiling and wall diffuse maps change per level (applyLevelLook).
    mats[MAT_PROPS].maps[MATERIAL_MAP_DIFFUSE].texture = texProps;
    propDetail=makePropDetail(texProps);
    mats[MAT_PROPS].maps[MATERIAL_MAP_SPECULAR].texture=propDetail;
    mats[MAT_SCRAWL].maps[MATERIAL_MAP_DIFFUSE].texture = texScrawl;   // wall scrawl decals
    mats[MAT_FIXTURES].maps[MATERIAL_MAP_DIFFUSE].texture = texFixtures;   // outlets, grilles, conduit
    mats[MAT_AO].maps[MATERIAL_MAP_DIFFUSE].texture = texAO;           // baked contact-shadow gradients
    mats[MAT_CAN].maps[MATERIAL_MAP_DIFFUSE].texture = texAlmondWrap;
    mats[MAT_DECK].maps[MATERIAL_MAP_DIFFUSE].texture = texDeck;

    audio.load();

    start.seed = shotPath ? 1337u : (unsigned)time(nullptr);
    if (const char *seedEnv = getenv("BACKROOMS_SEED")) start.seed = (unsigned)strtoul(seedEnv, nullptr, 10);
    start.exitTest = getenv("BACKROOMS_EXITS") != nullptr;
    start.manilaTest = getenv("BACKROOMS_MANILA") != nullptr;
    if (const char *posEnv = getenv("BACKROOMS_POS")) {   // "x,z,yaw[,pitch]", pitch up positive
        int n = sscanf(posEnv, "%f,%f,%f,%f", &start.x, &start.z, &start.yaw, &start.pitch);
        start.placed = n >= 3;
        start.pitched = n == 4;
    }
    if (const char *lvEnv = getenv("BACKROOMS_LEVEL")) start.level = atoi(lvEnv) % NLEVELS;
    if (const char *stEnv = getenv("BACKROOMS_STOREY")) { start.storeyed = true; start.storey = atoi(stEnv); }
    // The flashlight is off by default; captures of it need it switched on.
    start.flash = getenv("BACKROOMS_FLASH") != nullptr;
    everFlashed = start.flash;
    start.menu = (shotPath == nullptr) || getenv("BACKROOMS_MENU") != nullptr;   // headless shots skip the title
    // From the current window, so a phone's first frame does not start at the
    // desktop FOV and then change.
    start.fov = baseFov();

    snprintf(bestPath, sizeof(bestPath), "%s/.backrooms_best", getenv("HOME") ? getenv("HOME") : ".");
    start.best = loadRecords(bestPath);

    if (const char *recEnv = getenv("BACKROOMS_RECORD")) {
        if (trace.open(recEnv)) sim.tracer = &recordingTracer;
        else fprintf(stderr, "BACKROOMS_RECORD: cannot write %s\n", recEnv);
    }
    double now = GetTime();
    if (trace) trace.start(start, now);
    simBegin(sim, start, now);
    rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);   // post CA/bloom sample past the edges: clamp, don't wrap
    camReel.load();
    camcorder.fitReel(GetScreenWidth(), GetScreenHeight());
    camReel.fit(camcorder);
    if (const char *camEnv = getenv("BACKROOMS_CAM")) camKnob = atoi(camEnv);
    chaseKnob = getenv("BACKROOMS_CHASE") != nullptr;

    // The look first: applyLevel's schedules start from after its surfaces are made.
    enterLevel(0);
    if (start.level >= 0) enterLevel(start.level);
    lookEntries = sim.levelEntries;
    if (trace) trace.place();
    simPlace(sim, start);
    if (!shotPath && !sim.inMenu) DisableCursor();
}

void Game::enterLevel(int lv) {
    applyLevelLook(lv);
    double now = GetTime();
    if (trace) trace.level(lv, now);
    sim.applyLevel(lv, now);
}

void Game::shutdown() {
    if (benchmark && !frameSamples.empty()) {
        std::sort(frameSamples.begin(), frameSamples.end());
        double sum = 0;
        for (float ms : frameSamples) sum += ms;
        printf("BENCH frames=%zu mean_ms=%.3f median_ms=%.3f p95_ms=%.3f\n",
               frameSamples.size(), sum / frameSamples.size(), frameSamples[frameSamples.size()/2],
               frameSamples[(frameSamples.size()-1)*95/100]);
    }
    trace.close();
    sim.bankRecords();
    if (sim.recordsChanged) saveRecords();
    UnloadTexture(texParticle);
    revolver.unload();
    UnloadTexture(propDetail);
    UnloadMesh(flareMesh);
    for (Surface &sf : surfaces) if (sf.albedo.id) { UnloadTexture(sf.albedo); UnloadTexture(sf.detail); }
    UnloadTexture(neutralDetail);
    audio.unload();
    CloseAudioDevice();
    CloseWindow();
}

void Game::saveRecords() {
    sim.recordsChanged = false;
    ::saveRecords(bestPath, sim.best);
#ifdef PLATFORM_WEB
    // A tab's filesystem is lost with the page: flush to the IndexedDB that
    // web/shell.html mounted over $HOME. shutdown() never runs on the web, so
    // this is the only place records are saved there.
    EM_ASM({
        FS.syncfs(false, function (err) { if (err) console.warn("records not saved:", err); });
    });
#endif
}

void Game::playAudio() {
    audio.play(sim.audio);
    sim.audio.clear();
}

void Game::finishStep(bool wasInMenu) {
    if (sim.dropAimLatch) { webReleaseAim(); sim.dropAimLatch = false; }
    if (sim.inMenu != wasInMenu && !shotPath) {
        if (sim.inMenu) EnableCursor(); else DisableCursor();
    }
    playAudio();
    syncLevelLook();
    if (sim.recordsChanged) saveRecords();
}

const Surface &Game::surface(int slot) {
    Surface &sf = surfaces[slot];
    if (!sf.albedo.id) sf = makeSurface((SurfSlot)slot);
    return sf;
}

// uStorey is not set here: the level change marks the shadow grid stale, and
// updateOccupancy sets it when it rebuilds, before anything is drawn.
void Game::applyLevelLook(int lv) {
    const LevelCfg &c = LEVELS[lv];
    SetShaderValue(worldShader, locStoreyH, &c.storeyH, SHADER_UNIFORM_FLOAT);
    const LevelSurfaces &set = LEVEL_SURFACES[lv];
    const SurfSlot slots[3] = { set.floor, set.ceiling, set.walls };
    const int mat[3] = { MAT_FLOOR, MAT_CEILING, MAT_WALLS };
    for (int k = 0; k < 3; k++) {
        const Surface &sf = surface(slots[k]);
        mats[mat[k]].maps[MATERIAL_MAP_DIFFUSE].texture = sf.albedo;
        mats[mat[k]].maps[MATERIAL_MAP_SPECULAR].texture = sf.detail;
    }
    float ly = c.wallH - LIGHT_DROP;
    SetShaderValue(worldShader, locAmb, &c.amb, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFogCol, &c.fogCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFogDen, &c.fogDen, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locLightCol, &c.lightCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locLS, &c.ls, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locLY, &ly, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locDead, &c.dead, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locLightMul, &c.lightMul, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locGloss, &c.gloss, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locVary, &c.vary, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locFaulty, &c.faulty, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locWet, &c.wet, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locWetFrom, &c.wetFrom, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locMacro, &SURF_MACRO[lv], SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locBoard, &CEIL_BOARD[lv], SHADER_UNIFORM_FLOAT);
    SetWindowTitle(TextFormat("THE BACKROOMS — %s", c.name));
}

void Game::syncLevelLook() {
    if (lookEntries == sim.levelEntries) return;
    lookEntries = sim.levelEntries;
    applyLevelLook(sim.level);
}

// One frame: read input, step the sim, carry out what it asked for, draw.
// Returns false when the run should end (headless screenshot captured).
bool Game::tick() {
    webInputPoll();   // one snapshot of the touch controls for the whole frame
    float dt = fminf(GetFrameTime(), 0.05f);
    double now = GetTime();
    frame++;
    sim.clockSeed = (uint32_t)time(nullptr);
    if (benchmark && frame > 60) frameSamples.push_back(GetFrameTime() * 1000.0f);

    if (IsWindowResized()) {
        UnloadRenderTexture(rt);
        rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
        SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);   // post CA/bloom sample past the edges: clamp, don't wrap
        sim.fov = baseFov();   // so a rotated phone gets the right FOV immediately
        camcorder.fitReel(GetScreenWidth(), GetScreenHeight());
        camReel.fit(camcorder);
        if (trace) trace.fov(sim.fov);
    }
    if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

    if (sim.inMenu) {
        InputFrame in = readInput(false);
        if (trace) trace.menu(in, dt, now, sim.clockSeed);
        sim.menuDrift(dt, now);
        webReleaseAim();   // the aim latch must not survive into a new run
        streamChunks();
        updateOccupancy();
        sim.menuBegin(in, now);
        if (trace) trace.digest(sim);
        if (!sim.inMenu || sim.deathT <= 0) camcorder.stopReplay();
        if (!sim.inMenu) camcorder.startTape();
        camcorder.stepReplay(dt);
        camReel.playNoises(camcorder, audio);
        finishStep(true);
        renderScene(now);
        renderUI(now);
        if (shotPath && frame == shotFrame) { TakeScreenshot(shotPath); return false; }
        return true;
    }

    if (inKeyPressed(KEY_P) && !shotPath) {
        bool pause = !sim.paused;
        if (pause && inCursorHidden()) EnableCursor();
        if (trace) trace.pause(pause, now);
        sim.setPaused(pause, now);
        if (trace && pause) trace.digest(sim);
        if (!pause) DisableCursor();
    }
    if (sim.paused) {
        playAudio();
        audio.holdPaused(sim.ambience, sim.loopCue());
        renderScene(now);
        renderUI(now);
        if (shotPath && frame == shotFrame) { TakeScreenshot(shotPath); return false; }
        return true;
    }

    if (inKeyPressed(KEY_F3)) debugHud = !debugHud;
    if (inKeyPressed(KEY_ESCAPE) && inCursorHidden()) EnableCursor();
    // A click that captures the mouse must not also fire.
    bool captureClick = !inCursorHidden() && !shotPath && inMousePressed(MOUSE_BUTTON_LEFT);
    if (captureClick) DisableCursor();
    InputFrame in = readInput(captureClick);
    if (camKnob && frame == 2) { camcorder.up = true; camcorder.night = camKnob > 1; }
    camcorder.control(in, sim, dt, inKeyPressed(KEY_V));   // before the trace: what is recorded is what the sim was given
    webLightCam(camcorder.up);
    if (trace) trace.step(in, dt, now, sim.clockSeed);
    sim.step(in, dt, now);
    if (trace) trace.digest(sim);
    camcorder.logSounds(sim.audio);
    if (sim.inMenu && sim.deathT > 0 && camcorder.beginReplay())   // the player died: the tape plays on the death card
        webLightCam(false);
    camReel.playNoises(camcorder, audio);
    finishStep(false);
    streamChunks();
    updateOccupancy();

    renderScene(now);
    if (!sim.inMenu) {
        int slot = camcorder.record(sim, dt, (float)(now - sim.runStart), camReel.ready(), GetRandomValue);
        if (slot >= 0) camReel.grab(camcorder, slot, rt);
    }
    renderUI(now);

    if (shotPath && frame == shotFrame) {
        TakeScreenshot(shotPath);
        printf("fps=%d chunks=%d\n", GetFPS(), (int)sim.world.chunks.size());
        return false;
    }
    return true;
}

InputFrame Game::readInput(bool captureClick) {
    InputFrame in;
    in.playing = inCursorHidden();
    in.touch = inTouchActive();
    in.forward = inKeyDown(KEY_W);
    in.back = inKeyDown(KEY_S);
    in.left = inKeyDown(KEY_A);
    in.right = inKeyDown(KEY_D);
    in.forwardPressed = inKeyPressed(KEY_W);
    in.moveScale = webMoveScale();
    in.sprint = inKeyDown(KEY_LEFT_SHIFT);
    in.crouch = inKeyDown(KEY_LEFT_CONTROL);
    in.squeeze = inKeyDown(KEY_C);
    in.jumpHeld = inKeyDown(KEY_SPACE);
    in.jumpPressed = inKeyPressed(KEY_SPACE);
    in.look = fromRl(inMouseDelta());
    in.wheel = inWheel();
    in.pickRevolver = inKeyPressed(KEY_ONE);
    in.pickFlare = inKeyPressed(KEY_TWO);
    in.pickDeck = inKeyPressed(KEY_FOUR);
    in.fire = inMousePressed(MOUSE_BUTTON_LEFT) && !captureClick;
    in.aim = inMouseDown(MOUSE_BUTTON_RIGHT);
    in.reload = inKeyPressed(KEY_R);
    in.throwFlare = inKeyPressed(KEY_Q);
    in.flashlight = inKeyPressed(KEY_F) || inKeyPressed(KEY_L);
    in.use = inKeyPressed(KEY_E);
    in.drink = inKeyPressed(KEY_THREE);
    in.chalk = inKeyPressed(KEY_M);
    // On touch, a tap on open screen or a stick push also starts a run.
    int key = GetKeyPressed();   // F11 toggles fullscreen and does not start a run
    in.begin = (key != 0 && key != KEY_F11) || inMousePressed(MOUSE_BUTTON_LEFT) || webStartGesture();
    if (debugHud) {
        in.dev.blackout = inKeyPressed(KEY_B);
        in.dev.spawnAhead = inKeyPressed(KEY_E);
        in.dev.chase = inKeyPressed(KEY_C);
        in.dev.banish = inKeyPressed(KEY_H);
        in.dev.refill = inKeyPressed(KEY_G);
        in.dev.storeyUp = inKeyPressed(KEY_PAGE_UP);
        in.dev.storeyDown = inKeyPressed(KEY_PAGE_DOWN);
        in.dev.nextLevel = inKeyPressed(KEY_N);
    }
    in.screenFov = baseFov();
    in.forceSpawn = shotPath && !benchmark && frame == 300;   // headless: put the hunter in view
    if (chaseKnob && frame == 301) in.dev.chase = true;
    return in;
}

// raylib derives the horizontal angle from fovy and the aspect, so a fixed 70
// deg vertical was right only at 1440x850 (99.7 deg across) and gave a portrait
// phone 34 deg. This fixes the horizontal view instead, clamped so a square
// window is not too wide and a landscape phone not too narrow. `aim` raises the
// top of the clamp so aiming still narrows the view when the base is clamped.
float Game::baseFov() const {
    return windowFovY(GetScreenWidth(), GetScreenHeight(), 0);
}

void Game::streamChunks() {
    World &world = sim.world;
    int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
    int budget = (frame < 3) ? 64 : 4;
    ::streamChunks(world, pcx, pcz, 2, [&](const StreamChunk &c) {
        if (chunkMeshes.ensure(world, c.cx, c.cz)) {
            budget--;
            if (c.inShadowGrid) occValid = false;
        }
        return budget > 0;
    });
    if (frame % 90 == 0) world.unloadFar(pcx, pcz, 5);
}

// Keep the shader's light-occlusion grid centred on the player. Rebuilding is a
// few thousand cell lookups, so redo it only after the player has moved far
// enough or the walls have changed.
void Game::updateOccupancy() {
    World &world = sim.world;
    if (sim.shadowsStale) { occValid = false; sim.shadowsStale = false; }
    int wantI = cellOf(sim.px) - OCC_N / 2, wantK = cellOf(sim.pz) - OCC_N / 2;
    if (occValid && abs(wantI - occOriginI) < OCC_RECENTRE && abs(wantK - occOriginK) < OCC_RECENTRE) return;
    occOriginI = wantI; occOriginK = wantK;
    world.buildOccupancy(occOriginI, occOriginK, OCC_N, occBuf.data());
    UpdateTexture(texOcc, occBuf.data());
    occValid = true;
    // The CPU lighting reads the same snapshot, so a sprite on a flight is lit
    // by the same tubes as the treads under it.
    StoreyLightCPU sl;
    sl.storeyH = world.storeyH; sl.storey = world.storey;
    sl.occ = occBuf.data(); sl.occN = OCC_N; sl.originI = occOriginI; sl.originK = occOriginK;
    setStoreyLightCPU(sl);
    float st = (float)world.storey;
    SetShaderValue(worldShader, locStorey, &st, SHADER_UNIFORM_FLOAT);
}

// Triangles, not bounds, so doorways and the gaps under furniture stay open;
// chunk bounds keep the test local. The storeys above and below are in their
// own frames, drawn a pitch up or down.
bool MeshTracer::nearestSolid(const Ray3 &shot, float &nearest, Vec3 &normal) {
    const Ray ray = toRl(shot);
    const float travel = nearest;
    bool hit = false;
    for (int rel = -1; rel <= 1; rel++) {
        if (rel != 0 && world.storeyH <= 0.0f) continue;
        float oy = rel * world.storeyH;
        for (auto &entry : world.layer(world.storey + rel)) {
            int cx=(int32_t)(entry.first >> 32), cz=(int32_t)entry.first;
            Vector3 end=Vector3Add(ray.position,Vector3Scale(ray.direction,travel));
            if (fmaxf(ray.position.x,end.x)<cx*CHUNK-1 || fminf(ray.position.x,end.x)>(cx+1)*CHUNK+1 ||
                fmaxf(ray.position.z,end.z)<cz*CHUNK-1 || fminf(ray.position.z,end.z)>(cz+1)*CHUNK+1) continue;
            const ChunkMeshes *chunk = meshes.find(world, world.storey + rel, cx, cz);
            if (!chunk) continue;
            for (int m = MESH_FLOOR; m <= MESH_GLASS; ++m) {
                if (m == MESH_SCRAWL || m == MESH_WATER) continue;
                const Mesh &mesh = chunk->meshes[m];
                if (!mesh.vertexCount) continue;
                BoundingBox box=GetMeshBoundingBox(mesh);
                box.min.y += oy; box.max.y += oy;
                // A ray starting inside the bounds is tested even when its exit
                // is further than it travels, or nearby triangles are skipped.
                bool inside=ray.position.x>=box.min.x && ray.position.x<=box.max.x &&
                    ray.position.y>=box.min.y && ray.position.y<=box.max.y &&
                    ray.position.z>=box.min.z && ray.position.z<=box.max.z;
                RayCollision bounds = GetRayCollisionBox(ray, box);
                if (!inside && (!bounds.hit || bounds.distance > travel)) continue;
                RayCollision c = GetRayCollisionMesh(ray, mesh, rel ? MatrixTranslate(0, oy, 0) : MatrixIdentity());
                if (c.hit && c.distance >= 0 && c.distance <= nearest) {
                    nearest = c.distance; normal = fromRl(c.normal); hit = true;
                }
            }
        }
    }
    return hit;
}
