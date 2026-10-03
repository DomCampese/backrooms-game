#include "game.h"
#include "textures.h"
#include "shaders.h"
#include "input.h"
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
    // Headless captures schedule no blackouts: blackouts run on the wall clock,
    // and at a software rasteriser's 2 fps a capture lands inside one and comes
    // out as black as a failed shader. BACKROOMS_NOBLACKOUT=0 puts them back.
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
    // raylib's web resize callback clamps the canvas to screenMin before it
    // sizes it, so a 640x400 floor renders a 412 px phone at 640 wide and lets
    // CSS squash the result back down: the whole world comes out horizontally
    // compressed, which reads as a bad FOV rather than as a window-size bug.
    // The browser is the window manager here; it will not hand us anything
    // absurd.
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
    // Alpha 250, not 255: the shader takes a detail alpha above 0.99 to mean
    // "a tiled world surface" and lays its world-space variation (uMacro,
    // uBoard) over only those. Object maps are 128; this neutral one serves the
    // decals, fixtures and held objects, and must stay out of that test while
    // still reading as level-relative gloss (anything over 0.75).
    Image neutral = GenImageColor(1, 1, {128, 128, 255, 250});
    neutralDetail = LoadTextureFromImage(neutral);
    UnloadImage(neutral);

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
    // unbound sampler reads as white, the occlusion code takes that for "wall
    // everywhere", and the object turns black. Count comes from the enum, so a
    // new slot cannot be added without being initialised here.
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
    // The torch is the one light you aim, so a fixed-position capture needs it switched on.
    start.flash = getenv("BACKROOMS_FLASH") != nullptr;
    everFlashed = start.flash;
    start.menu = (shotPath == nullptr) || getenv("BACKROOMS_MENU") != nullptr;   // headless shots skip the title
    // From the window in hand, so a phone's first frame does not open at the
    // desktop's angle and slide.
    start.fov = baseFov();

    snprintf(bestPath, sizeof(bestPath), "%s/.backrooms_best", getenv("HOME") ? getenv("HOME") : ".");
    if (FILE *bf = fopen(bestPath, "r")) {
        Records &b = start.best;
        // Fields were appended over time; a file missing the later ones reads them as 0.
        if (fscanf(bf, "%d %d %d", &b.escapes, &b.kills, &b.metres) != 3) b.escapes = b.kills = b.metres = 0;
        if (fscanf(bf, "%d", &b.wins) != 1) b.wins = 0;
        if (fscanf(bf, "%d", &b.tapes) != 1) b.tapes = 0;
        if (fscanf(bf, "%d %d", &b.deepest, &b.longestRun) != 2) b.deepest = b.longestRun = 0;
        fclose(bf);
    }

    if (const char *recEnv = getenv("BACKROOMS_RECORD")) {
        if (trace.open(recEnv)) sim.tracer = &recordingTracer;
        else fprintf(stderr, "BACKROOMS_RECORD: cannot write %s\n", recEnv);
    }
    double now = GetTime();
    if (trace) trace.start(start, now);
    simBegin(sim, start, now);
    rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);   // post CA/bloom sample past the edges: clamp, don't wrap

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
    const Records &b = sim.best;
    if (FILE *bf = fopen(bestPath, "w")) {
        fprintf(bf, "%d %d %d %d %d\n%d %d\n", b.escapes, b.kills, b.metres, b.wins, b.tapes,
                b.deepest, b.longestRun);
        fclose(bf);
    }
#ifdef PLATFORM_WEB
    // A tab's filesystem dies with the page: flush to the IndexedDB that
    // web/shell.html mounted over $HOME. shutdown() never runs on the web, so
    // this is the only place records are persisted there.
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
        sim.fov = baseFov();   // a phone turned on its side is framed from the first frame
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
    // A click that captures the mouse is spoken for: it must not also fire.
    bool captureClick = !inCursorHidden() && !shotPath && inMousePressed(MOUSE_BUTTON_LEFT);
    if (captureClick) DisableCursor();
    InputFrame in = readInput(captureClick);
    if (trace) trace.step(in, dt, now, sim.clockSeed);
    sim.step(in, dt, now);
    if (trace) trace.digest(sim);
    finishStep(false);
    streamChunks();
    updateOccupancy();

    renderScene(now);
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
    // A tap on open screen or a stick push begins too: a phone has no key.
    int key = GetKeyPressed();   // F11 is the window's, not a request to begin
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
    return in;
}

// raylib derives the horizontal angle from fovy times the aspect, so a fixed
// 70 deg vertical was right only at the 1440x850 window (99.7 deg across) and
// left a portrait phone a 34 deg keyhole. This locks the horizontal view
// instead, clamped where a fixed horizontal would go fisheye (a square window)
// or binoculars (a landscape phone). `aim` widens the top of the band so the
// sights still narrow the view where the clamp pins the base.
float Game::baseFov() const {
    return windowFovY(GetScreenWidth(), GetScreenHeight(), 0);
}

void Game::streamChunks() {
    World &world = sim.world;
    int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
    int budget = (frame < 3) ? 64 : 4;
    for (int r = 0; r <= 2 && budget > 0; r++)
        for (int dx = -r; dx <= r && budget > 0; dx++)
            for (int dz = -r; dz <= r && budget > 0; dz++) {
                if (std::max(abs(dx), abs(dz)) != r) continue;
                if (chunkMeshes.ensure(world, pcx + dx, pcz + dz)) budget--;
            }
    // The storeys above and below, where you can see them: through the
    // openings in the chunks round you. A feature's own chunk is where the
    // opening is, and its neighbours are what you see past the edges of it.
    if (world.storeyH > 0.0f && budget > 0) {
        for (int rel = -1; rel <= 1 && budget > 0; rel += 2)
            for (int dx = -1; dx <= 1 && budget > 0; dx++)
                for (int dz = -1; dz <= 1 && budget > 0; dz++) {
                    if (!world.linksStorey(pcx + dx, pcz + dz, rel)) continue;
                    StoreyScope sc(world, world.storey + rel);
                    for (int ex = -1; ex <= 1 && budget > 0; ex++)
                        for (int ez = -1; ez <= 1 && budget > 0; ez++) {
                            if (abs(dx + ex) > 2 || abs(dz + ez) > 2) continue;
                            // The shadow grid carries this storey too (bytes 1
                            // and 2), read from whatever is loaded when it is
                            // built — so a chunk arriving here means rebuilding
                            // it, or the light that should fall down this
                            // opening waits until you have walked 12 m.
                            if (chunkMeshes.ensure(world, pcx + dx + ex, pcz + dz + ez)) { budget--; occValid = false; }
                            // Follow visible chains through tall courts, one chunk
                            // per layer; normal rooms stop the chain immediately.
                            for(int depth=2;depth<=STOREY_REACH && budget>0;++depth) {
                                StoreyScope prev(world,world.storey+(depth-1)*rel);
                                VertFeat link;
                                if(!world.pairFeature(pcx+dx+ex,pcz+dz+ez,
                                    world.storey+(rel>0 ? depth-1 : -depth),link)) break;
                                StoreyScope next(world,world.storey+depth*rel);
                                if(chunkMeshes.ensure(world,pcx+dx+ex,pcz+dz+ez)) --budget;
                            }
                        }
                }
    }
    if (frame % 90 == 0) world.unloadFar(pcx, pcz, 5);
}

// Keep the shader's light-occlusion grid centred on the player. Rebuilding is a
// few thousand cell lookups, so only redo it once you've walked far enough that
// the window is worth moving, or the walls have changed.
void Game::updateOccupancy() {
    World &world = sim.world;
    if (sim.shadowsStale) { occValid = false; sim.shadowsStale = false; }
    int wantI = cellOf(sim.px) - OCC_N / 2, wantK = cellOf(sim.pz) - OCC_N / 2;
    if (occValid && abs(wantI - occOriginI) < 6 && abs(wantK - occOriginK) < 6) return;
    occOriginI = wantI; occOriginK = wantK;
    world.buildOccupancy(occOriginI, occOriginK, OCC_N, occBuf.data());
    UpdateTexture(texOcc, occBuf.data());
    occValid = true;
    // The CPU half of the lighting reads the same snapshot, so a sprite on a
    // flight is lit by the same tubes the treads under it are.
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
