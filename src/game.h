#pragma once
// All run state plus the per-frame update/render orchestration. One Game
// instance owns the world, the player, the weapons, and PIRATE CLARK.
#include "raylib.h"
#include "util.h"
#include "world.h"
#include "levels.h"
#include "entity.h"
#include "audio.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

// one thrown flare: arcs, clatters off walls, burns on the floor
struct FlareProj {
    bool active = false, flying = false;
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, burn = 0;
};

struct Game {
    // tuning
    static constexpr float PR = 0.34f;        // player radius
    static constexpr int   MAXFLARES = 3;
    static constexpr float FLAREBURN = 9.0f;  // seconds
    static constexpr int   MAXAMMO = 6;
    static constexpr int   ESCAPE_COST = 12;  // doubloons that buy your way out for good

    // env/test knobs (BACKROOMS_* — see README)
    const char *shotPath = nullptr;

    // resources
    Texture2D texEntity{}, texPartygoer{}, texProps{}, texScrawl{}, texAO{};
    Texture2D floorTexs[NLEVELS]{}, ceilTexs[NLEVELS]{}, wallTexs[NLEVELS]{};   // per-level surface sets
    Shader worldShader{}, postShader{};
    int locTime = -1, locBlackout = -1, locViewPos = -1, locFlash = -1, locFlashDir = -1,
        locAmb = -1, locFogCol = -1, locFogDen = -1, locLightCol = -1, locLS = -1, locLY = -1,
        locDead = -1, locLightMul = -1, locFlarePos = -1, locFlareInt = -1, locGloss = -1,
        locEntPos = -1, locEntDark = -1;
    int locPTime = -1, locPFear = -1;
    Material mats[6]{};                        // 0 floor, 1 ceiling, 2 walls, 3 props, 4 scrawl, 5 baked AO
    Sound steps[4]{}, splashes[2]{}, sndBigSplash{}, sndClick{}, sndScare{}, sndWin{},
          sndFlare{}, sndShot{}, sndHit{}, sndKill{}, sndPop{};
    Sound entSteps[4]{};                        // the thing's own footfalls, panned + attenuated
    AudioSynth synth;
    World world;
    Rng grng{1};
    RenderTexture2D rt{};

    // player
    float px = 0, pz = 0;
    float yaw = 0.8f, pitch = 0.0f;
    float velx = 0, velz = 0;
    float py = 0, vy = 0;                     // feet height (0 = dry floor, -0.6 = pool bottom)
    bool grounded = true;
    float stamina = 1.0f, fov = 70.0f, stepAcc = 0, bobPhase = 0;
    bool flashOn = false;
    float flashCur = 0;

    // per-frame derived (look/movement feeds weapons, entity, and render)
    Vector3 fwd{ 1, 0, 0 };
    float f2x = 1, f2z = 0, r2x = 0, r2z = 1;
    bool sprinting = false;
    float bobAmt = 0, eyeY = 1.62f;
    bool captureClick = false;                // this click grabbed the mouse; don't also fire
    float leanCur = 0, landDip = 0;           // camera feel: strafe lean + landing dip
    float strafeInput = 0;                    // -1..1, set by movement, read by render lean
    double entStepAcc = 0;                    // spacing of the thing's audible footfalls
    float muzzleSmoke = 0;                    // powder haze lingering after a shot

    // flare weapon: thrown, burns orange, Pirate Clark won't go near one
    int flares = MAXFLARES;
    double nextFlareRegen = 0;
    FlareProj flare;

    // revolver: hitscan, six rounds, three hits put Clark down
    int weapon = 0;                           // 0 flare, 1 revolver — keys 1/2 or mouse wheel
    int ammo = MAXAMMO;
    float reloadT = 0, gunCd = 0, muzzleT = 0, recoil = 0, wheelCd = 0;

    // run state
    int level = 0;
    Entity ent;
    float entDist = 1e9f;                     // distance to Clark this frame
    float entDarkCur = 0;                     // how hard it's smothering the lights (ramps with the hunt)
    double nextBlackout = 0, blackoutEnd = -1;
    float blackoutCur = 1.0f, fear = 0.0f;
    float caughtT = 0, escapeT = 0, killT = 0, fellT = 0, winT = 0;
    float softTimer = 0;                      // how long you've stood on a soft patch
    int caughtCount = 0, escapeCount = 0, killCount = 0, winCount = 0;
    float winTime = 0; int winM = 0, winKills = 0;   // stats frozen for the escape screen
    float distWalked = 0;
    double runStart = 0;
    bool wayOpen() const { return coins >= ESCAPE_COST; }   // enough doubloons to leave for good
    bool debugHud = false;
    int frame = 0;

    // pickups, currency, chalk, ambient events, records
    std::unordered_set<uint64_t> taken;       // world pickups already grabbed (reset per level)
    std::vector<Vector3> coinsWorld;          // doubloons Clark spills when he goes down
    std::vector<Vector3> chalk;               // navigation marks
    std::unordered_set<uint64_t> poppedBalloons;     // LEVEL FUN ceiling balloons already shot
    std::unordered_set<uint64_t> poppedTableBunches; // and party-table balloon bunches
    struct Confetti { Vector3 pos, vel; float life; Color col; };
    std::vector<Confetti> confetti;           // bursts from popped balloons
    int almond = 0, coins = 0;
    float boostT = 0, crouchCur = 0, whisperT = 0;
    double nextWhisper = 0;
    char bestPath[512] = {};
    int bestEsc = 0, bestKill = 0, bestM = 0, bestWins = 0;
    bool everFlashed = false;                 // HUD: flashlight reminder until first use
    bool inMenu = false;                      // title screen up, world drifting behind it

    void winRun(double now);                  // stepped through the true way out — reset the descent
    void updateMenu(double now);              // drift the title-screen camera; any key begins
    void startRun(double now);                // leave the menu and start a fresh descent

    void init();
    bool tick();                              // one frame; false = run ended (headless shot taken)
    void shutdown();

    void applyLevel(int lv);
    void saveBest();

    // deterministic world pickups, keyed by cell
    static uint64_t cellKey2(int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; }
    bool bottleAt(int a, int b);              // almond water, left out for whoever needs it
    bool coinAt(int a, int b);                // a doubloon he dropped on his rounds
    bool balloonAt(int a, int b, Vector3 &out);   // LEVEL FUN ceiling balloon centre, if one floats here
    // party-table balloon bunch in this cell: fills pos[]/cols[] (up to 4), the
    // knot point, and returns the count (0 = no bunch). Shared by render + aim.
    int tableBalloonBunch(int a, int b, Vector3 *pos, Color *cols, Vector3 &tie);
    void popBalloonsAlongAim();               // revolver vs. balloons, when you fire in LEVEL FUN

    // update, in frame order (game.cpp)
    void updateLook();
    void updateMovement(float dt);
    void updateDevKeys(double now);
    void updateWeapons(float dt, double now);
    void updateFlare(float dt, double now);
    void updateInteraction();
    void updateAmbience(float dt, double now);
    void updateEntity(float dt, double now);
    void updateExits(double now);
    void streamChunks();

    // render (render.cpp)
    void renderScene(double now);             // 3D world into the offscreen target
    void renderUI(double now);                // post pass, viewmodel, HUD, overlays
};
