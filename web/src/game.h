#pragma once
// The platform half of the game: the window, input, assets, audio and
// rendering around one Sim. Each tick reads an InputFrame, steps the sim,
// plays its audio events and draws its state (docs/migration.md).
#include "raylib.h"
#include "sim/sim.h"
#include "sim/trace.h"
#include "port/view.h"
#include "levels.h"
#include "util.h"
#include "vec_rl.h"
#include "game_audio.h"
#include "world_mesh.h"
#include "object_meshes.h"
#include "revolver.h"
#include "textures.h"
#include <vector>

// Slots in Game::mats. The first four line up with the chunk mesh slots of the
// same name (MESH_FLOOR..MESH_PROPS), which is what lets renderScene draw them
// in one loop.
enum MatSlot {
    MAT_FLOOR = 0,
    MAT_CEILING,
    MAT_WALLS,
    MAT_PROPS,
    MAT_SCRAWL,
    MAT_FIXTURES,
    MAT_AO,
    MAT_CAN,
    MAT_DECK,
    MAT_COUNT,
};

// Shots against the level's triangles: the chunk meshes of your storey and the
// ones above and below, so a round fired down a stairwell hits the flight.
struct MeshTracer : SolidTracer {
    World &world;
    ChunkMeshCache &meshes;
    MeshTracer(World &w, ChunkMeshCache &m) : world(w), meshes(m) {}
    bool nearestSolid(const Ray3 &shot, float &nearest, Vec3 &normal) override;
};

struct Game {
    Sim sim;
    ChunkMeshCache chunkMeshes;   // the world's chunks, baked
    MeshTracer tracer{ sim.world, chunkMeshes };
    // BACKROOMS_RECORD: every call on the sim, for tools/replay (shared/sim/trace.h).
    TraceWriter trace;
    RecordingTracer recordingTracer{ tracer, trace };
    Game() { sim.tracer = &tracer; }
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    // env/test knobs (BACKROOMS_*, see README)
    bool benchmark = false, cleanShot = false;
    float captureTime = -1;                   // BACKROOMS_TIME: pins the time rendering sees
    std::vector<float> frameSamples;
    const char *shotPath = nullptr;
    int shotFrame = 600;                      // BACKROOMS_SHOTFRAME
    int frame = 0;
    bool debugHud = false;                    // F3; the dev keys act only while it is up
    bool everFlashed = false;                 // HUD: the flashlight hint shows until first use
    char bestPath[512] = {};                  // where the records live

    // resources
    Texture2D texClark{}, texEntity{}, texEntityGlow{}, texPartygoer{}, texProps{}, texScrawl{}, texFixtures{}, texAO{}, texOcc{}, texDog{},
              texAlmondWrap{}, texDeck{}, texParticle{};
    Revolver revolver;
    Mesh flareMesh{};
    Mesh canMesh{};                            // the almond water can, real geometry
    Mesh deckMesh{}, reelMesh{}, deckLampMesh{};   // the tape player, its reels, its record lamp
    Mesh crateMesh{}, crateLidMesh{};
    // The light-occlusion grid the shader marches: the floorplan round you,
    // OCC_N cells wide, recentred as you walk. Four bytes a cell
    // (World::buildOccupancy): your storey, the one below, the one above.
    static constexpr int OCC_N = 64;
    std::vector<unsigned char> occBuf;
    int occOriginI = 0, occOriginK = 0;
    bool occValid = false;
    // World surfaces (surfaces.cpp), made the first time a level that uses one
    // is entered: a level's set takes a few hundred milliseconds, and only
    // Level 0's is paid before the first frame. Levels share some, so they are
    // slots rather than a per-level array.
    Surface surfaces[SURF_COUNT]{};
    const Surface &surface(int slot);
    unsigned lookEntries = 0;                 // sim.levelEntries when the level's look was last applied
    Texture2D neutralDetail{}, propDetail{};
    Shader worldShader{}, postShader{};
    int locTime = -1, locBlackout = -1, locViewPos = -1, locFlash = -1, locFlashDir = -1,
        locAmb = -1, locFogCol = -1, locFogDen = -1, locLightCol = -1, locLS = -1, locLY = -1,
        locDead = -1, locLightMul = -1, locFlarePos = -1, locFlareInt = -1, locGloss = -1,
        locEntPos = -1, locEntDark = -1, locOccOrigin = -1, locOccN = -1, locEntBlock = -1,
        locVary = -1, locFaulty = -1, locWet = -1, locWetFrom = -1, locRoomMask = -1, locLamp = -1,
        locStoreyH = -1, locStorey = -1, locLampCol = -1, locMacro = -1, locBoard = -1, locObjRefl = -1, locDrawRel = -1;
    int locPTime = -1, locPFear = -1, locPWater = -1, locPMigraine = -1;
    Material mats[MAT_COUNT]{};
    GameAudio audio;
    RenderTexture2D rt{};

    void init();
    bool tick();                              // one frame; false = run ended (headless shot taken)
    void shutdown();

    // Read after the cursor is captured or released for the tick, so
    // `playing` is what the rest of the tick sees.
    InputFrame readInput(bool captureClick);
    // What a step asks of the platform: the cursor, the touch aim latch,
    // sound, the level's look, the records file.
    void finishStep(bool wasInMenu);
    void playAudio();
    // Materials, shader uniforms and window title for level lv.
    void applyLevelLook(int lv);
    void enterLevel(int lv);   // its look, then the rules
    void syncLevelLook();                     // applyLevelLook if the sim has entered a level since
    void saveRecords();
    // Base camera fovy for the window, locking the horizontal view so a
    // portrait phone does not play through a 34 deg keyhole.
    float baseFov() const;
    void streamChunks();
    void updateOccupancy();                   // recentre and re-upload the light-occlusion grid

    // render (render.cpp)
    void renderScene(double now);             // 3D world into the offscreen target
    void renderUI(double now);                // post pass, HUD, overlays
    void drawHeldWeapon(const Camera3D &cam);
    void drawCan(Matrix xf);
    void drawDeck(Matrix xf, bool lamp);
};
