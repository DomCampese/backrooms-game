#pragma once
// The game's state and rules, apart from how they are drawn and heard. The
// platform (Game, src/game.cpp) owns one Sim: each tick it fills an
// InputFrame, calls step() with the clamped frame time and the clock, plays
// `audio` in order, and draws from the state here. See docs/migration.md.
#include "sim_math.h"
#include "input_frame.h"
#include "audio_events.h"
#include "entity.h"
#include "weapon_timing.h"
#include "../core/hash.h"
#include "../core/level_rules.h"
#include "../core/world.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

// A thrown flare: arcs, clatters off walls, burns on the floor.
struct FlareProj {
    bool active = false, flying = false;
    bool onStorey = true;    // burning on the storey you are on, not one seen through an opening
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, burn = 0;
};

// The tape player. Playing a tape restores sanity, and a running deck is a
// noise the Red Halls pack hunts; set down still playing, it draws them to it.
struct TapeDeck {
    bool carried = true;      // in your coat, or lying where you set it down
    bool playing = false;
    float t = 0;              // seconds of tape left to run
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    bool flying = false;      // still in the air after you tossed it
    float yaw = 0;            // heading it was set down at
    float reel = 0;           // hub rotation, radians
};

// What is in your hands. Keys 1/2/4 pick one; the wheel cycles in this order.
enum Weapon {
    WEAPON_REVOLVER = 0,
    WEAPON_FLARE,
    WEAPON_DECK,
    WEAPON_COUNT,
};

// A loose item lying in a cell. Which one a cell holds is a pure function of
// the cell and the seed (Sim::pickupAt), so drawing and collecting agree
// without storing anything.
enum class Pickup { None, AlmondWater, Doubloon, Battery, Tape, Key };

// Balloon and confetti colours are indices into Level 4's palette, which the
// platform draws with (PARTY, src/util.h, must have this many entries).
constexpr int PARTY_COLOURS = 5;

// The Manila Room's notes: pages of up to four lines.
constexpr int MANILA_NOTE_COUNT = 4;
extern const char *const MANILA_NOTES[MANILA_NOTE_COUNT][4];

// A chalk arrow. `mine` is false for the two a stranger left on each level;
// `storey` is the floor it was drawn on.
struct ChalkMark { Vec3 pos; float yaw; bool mine; int storey = 0; };

// The player's bests. They outlive every descent; the platform loads and
// saves them.
struct Records {
    int escapes = 0, kills = 0, metres = 0, wins = 0, tapes = 0;
    int deepest = 0;          // deepest level reached
    int longestRun = 0;       // seconds
};

// Where a ray first meets solid level geometry. The level's triangles belong
// to the platform, so it answers (MeshTracer, game.cpp).
struct SolidTracer {
    virtual ~SolidTracer() = default;
    // Shortens `nearest` to the first hit no farther than it and sets `normal`;
    // false if nothing solid is that close.
    virtual bool nearestSolid(const Ray3 &ray, float &nearest, Vec3 &normal) = 0;
};

struct Sim {
    // ---- tuning
    static constexpr float PR = 0.34f;          // player radius
    static constexpr float SQUEEZE_R = 0.12f;   // player radius while squeezing
    static constexpr float EYE_H = 1.62f;       // eye above the feet, standing
    static constexpr float FLOAT_PY = WATER_Y - 1.35f;   // feet of a swimmer floating at the surface
    static constexpr int   MAXFLARES = 3;
    static constexpr float FLAREBURN = 9.0f;    // s
    static constexpr float FLAREFADE = 1.5f;    // s of guttering at the end of a burn
    static constexpr float FLAREFALL = 0.05f;   // how fast a fire's presence falls off with distance
    static constexpr double FLARE_REGEN = 75;   // s to scavenge a fresh flare
    static constexpr int   MAXAMMO = 6;
    static constexpr float MUZZLE_FLASH = 0.09f;   // s the muzzle flash lights the room
    // Hiding blinds the hunt, blocks the catch and runs ent.unseen at 2.4x, so
    // it needs stillness. Two thresholds, so the spot does not flicker while
    // you settle into it.
    static constexpr float HIDE_ENTER = 0.4f;   // m/s to be under to tuck in
    static constexpr float HIDE_BREAK = 1.2f;   // m/s that gives you away
    static constexpr float HIDE_GRACE = 0.35f;  // s above that before you lose the spot
    static constexpr float TAPE_RUN = 26.0f;    // s: one side of a tape
    static constexpr float TAPE_NOISE = 32.0f;  // m a playing deck carries to the pack
    static constexpr int   ESCAPE_COST = 12;    // doubloons that buy the way out
    // The catch: he commits from LUNGE_REACH, the commit lasts LUNGE_TIME and
    // is announced, and only a running commit takes you inside CATCH_REACH.
    static constexpr float LUNGE_REACH = 2.5f;
    static constexpr float LUNGE_TIME = 0.6f;
    static constexpr float CATCH_REACH = 1.25f;
    static constexpr float DEATH_CARD = 7.0f;   // s the death card holds the title screen
    static constexpr float DEATH_CARD_HOLD = 1.6f;   // s of it no key can dismiss
    // Health. One hit is survivable and a second before healing is not: two
    // hits are past zero, and REGEN_DELAY + 0.6 / REGEN_RATE (16 s) untouched
    // earns the spare hit back.
    static constexpr float ENTITY_HIT = 0.6f;
    static constexpr float PACK_BITE = 0.6f;
    static constexpr float HURT_GRACE = 1.5f;   // s of immunity after a hit
    static constexpr float REGEN_DELAY = 6.0f;
    static constexpr float REGEN_RATE = 0.06f;  // meter-fraction per second
    // Where the hunter arrives: the near band is inside the fog and out of
    // sight; the far band is out in the fog.
    static constexpr float SPAWN_NEAR_MIN = 6.0f;
    static constexpr float SPAWN_NEAR_MAX = 17.0f;
    static constexpr float SPAWN_FAR_MIN = 20.0f;
    static constexpr float SPAWN_FAR_SPAN = 10.0f;
    static constexpr float W_TAP = 0.3f;        // s: double-tap window for W-to-run
    static constexpr float ENT_STRIDE = 1.05f;  // m per hunter footfall
    static constexpr float DOG_STRIDE = 0.85f;  // m per dog footfall
    // Cells the room-size probe marches each way: 20 m, past anything on Level
    // 0 and short of the Level 1 halls, so a corridor reads near 0 and a hall 1.
    static constexpr int   ROOM_PROBE = 10;
    static constexpr float SLIDE_FROM = 0.10f;  // the fraction of the sanity meter that is the terminal slide
    // Walls shift within this many cells of you, at gaps that shorten as the
    // slide deepens.
    static constexpr int   SHIFT_RING = 8;
    static constexpr double SHIFT_GAP_MIN = 9.0;
    static constexpr double SHIFT_GAP_SPAN = 22.0;
    static constexpr double BLACKOUT_NEVER = 1e18;   // a schedule that never fires
    static constexpr int   MAXDOGS = 3;
    static constexpr int   MAXCHALK = 128;      // marks per level
    static constexpr float DRINK_TIME = 1.75f;  // s the can is up
    static constexpr int   VALVES_NEEDED = 3;

    // ---- configuration, set by the platform before the first step
    bool noBlackout = false;     // never schedule a blackout (headless captures)
    bool fixedSeed = false;      // a new descent uses seed 1337 (headless captures)
    bool keepRecords = true;     // automated runs must not change the player's records
    uint32_t clockSeed = 0;      // wall-clock seconds, mixed into a new descent's seed; set each tick
    SolidTracer *tracer = nullptr;

    // ---- what the platform reads after a step; it clears each flag it acts on
    std::vector<AudioEvent> audio;   // in order; the platform plays and clears it
    AmbienceMix ambience;            // what the synth chases, as last decided
    bool dropAimLatch = false;       // an aim was refused this tick: release a latched touch AIM
    bool shadowsStale = false;       // walls changed: rebuild the light-occlusion grid
    unsigned levelEntries = 0;       // counts applyLevel calls, so a new level's look is applied once
    bool recordsChanged = false;     // `best` improved: save it

    World world;
    Rng grng{1};

    // ---- player
    float px = 0, pz = 0;
    float yaw = 0.8f, pitch = 0.0f;
    float velx = 0, velz = 0;
    float py = 0, vy = 0;            // feet above the floor of the storey you are on
    bool storeyNoted = false;        // said "another floor" once this descent
    float fallFrom = 0;              // highest py since you last stood on something
    bool grounded = true;
    bool swimming = false;
    float swimPhase = 0, swimClimb = 0;
    float floatT = 0, floatRoll = 0; // surface swell and camera roll; visual only
    float wTapT = 0; bool wSprint = false;
    float health = 1.0f, hurtT = 0, sinceHurt = 0;   // hurtT: immunity left after a hit
    float stamina = 1.0f;
    float fov = 70.0f;               // camera fovy, deg, eased toward the window's base plus the action pulls
    float bobPhase = 0;              // counts footfalls: an integer is a foot landing
    bool flashOn = false;
    float flashCur = 0;
    float battery = 1.0f;            // flashlight charge, 0..1
    Vec3 fwd{ 1, 0, 0 };
    float f2x = 1, f2z = 0, r2x = 0, r2z = 1;   // ground-plane forward and right
    bool sprinting = false, sprintExhausted = false;
    float bobAmt = 0, eyeY = EYE_H;
    float leanCur = 0, landDip = 0;  // camera lean into a strafe, dip after a landing
    float strafeInput = 0;
    float crouchCur = 0;
    bool squeezing = false;
    float squeezeBlend = 0;
    float softTimer = 0;             // s stood on a rotten patch
    float softSag = 0;               // how far it has let you down
    double nextGroan = 0;
    float distWalked = 0;
    bool still = false;              // under HIDE_ENTER this tick: what the pack listens for
    bool hidden = false;             // crouched beside cover and still
    bool nearCover = false;
    float hideBreakT = 0;            // s moving too fast to hold the spot

    // ---- hands
    int weapon = WEAPON_REVOLVER;
    int ammo = MAXAMMO;
    bool aiming = false;
    float aimBlend = 0;
    float reloadT = 0, gunCd = 0, muzzleT = 0, recoil = 0, wheelCd = 0;
    float muzzleSmoke = 0;           // powder haze after a shot
    struct Bullet { Vec3 pos, tail, direction; float remaining, fade; };
    struct BulletImpact { Vec3 pos, normal; float life; bool body; };
    std::vector<Bullet> bullets;
    std::vector<BulletImpact> bulletImpacts;
    int flares = MAXFLARES;          // in your coat
    double nextFlareRegen = 0;
    FlareProj litFlares[MAXFLARES];  // one slot per flare you can carry
    TapeDeck deck;
    float drinkT = 0;                // counts down from DRINK_TIME while the can is up
    bool drinkLanded = false;        // this drink has paid out

    // ---- the run
    int level = 0;
    Entity ent;
    Dog dogs[MAXDOGS];
    double nextPack = 0, nextHowl = 0;
    float entDist = 1e9f;            // to the hunter, this tick
    float entPrevX = 0, entPrevZ = 0;   // his position last tick, for his velocity
    float entDarkCur = 0;            // how hard he smothers the lights
    double nextBlackout = 0, blackoutEnd = -1;
    float blackoutCur = 1.0f, fear = 0.0f;
    float whisperT = 0;
    double nextWhisper = 0;
    float boostT = 0;                // s of almond-water speed left
    float sanity = 1.0f;
    int sanityStage = 0;             // deepest threshold crossed, so each warning fires once
    double nextHeartbeat = 0;
    float slide = 0;                 // how far into the terminal slide, 0..1
    double nextShift = 0;            // when a wall next shifts
    float migraine = 0;              // builds under Level 0's tubes, outlasts leaving it
    bool migraineWarned = false;
    int visits[NLEVELS] = { 0 };     // entries to each level this descent
    double runStart = 0;
    bool inMenu = false;             // title screen up: the run waits for `begin`
    bool paused = false;
    double pausedAt = 0;

    // Level 0's Manila Room. manilaNear while one is in the chunks round you,
    // inManila while you are inside its walls.
    bool manilaNear = false, inManila = false, manilaSeen = false, notesRead = false;
    float manilaX = 0, manilaZ = 0;
    int notePage = 0;
    // Level 1's supply crates stand where a hash of the cell and an epoch puts
    // them; the epoch moves on when a blackout ends.
    uint32_t crateEpoch = 0;
    std::unordered_set<uint64_t> cratesOpened;   // this epoch's, by cell
    bool crateWasDark = false;
    // The Red Halls standpipes.
    std::unordered_set<uint64_t> valvesTurned;
    bool pipesShut = false;          // all closed on this visit
    bool pipesPaid = false;          // their cache pays once a descent

    // ---- carried and collected
    int almond = 0, coins = 0, tapes = 0, keys = 0;
    std::unordered_set<uint64_t> taken;   // world pickups already collected, this level
    std::vector<Vec3> coinsWorld;      // doubloons spilled on the floor
    std::vector<ChalkMark> chalk[NLEVELS];   // per level, cleared by beginDescent
    bool chalkSeeded[NLEVELS]{};
    bool chalkSeedPending = false;        // lay the stranger's marks next tick, once px/pz are real
    std::unordered_set<uint64_t> poppedBalloons, poppedTableBunches;
    struct Confetti { Vec3 pos, vel; float life; uint8_t colour; };   // colour: index into the party palette
    std::vector<Confetti> confetti;

    // ---- tallies and what the HUD shows
    int deathCount = 0, escapeCount = 0, killCount = 0, winCount = 0;
    int deepest = 0;                 // deepest level this descent
    Records best;
    const char *deathBy = "";
    const char *deathTitle = "YOU DID NOT GET OUT";
    float deathTime = 0; int deathM = 0, deathLevel = 0, deathKills = 0;
    float winTime = 0; int winM = 0, winKills = 0;
    // Overlay timers, seconds left.
    float deathT = 0, escapeT = 0, killT = 0, fellT = 0, winT = 0;
    float closeCallT = 0, tapeFoundT = 0, valveT = 0, sanityWarnT = 0;
    float noteT = 0, manilaCardT = 0, deckNoteT = 0;
    const char *tapeLine = "";       // the recovered tape's line
    const char *sanityLine = "";     // the last sanity (or migraine) warning
    const char *deckNote = "";       // one-line notes: the deck, keys, crates, a new floor

    // ---- the tick. step() runs the updates below in this order; each may
    // rely on the ones before it (a shot lands before the hunter reacts).
    void step(const InputFrame &in, float dt, double now);
    // Title screen: the camera drifts and the world waits. menuDrift runs
    // before the platform streams chunks, menuBegin after.
    void menuDrift(float dt, double now);
    void menuBegin(const InputFrame &in, double now);
    void setPaused(bool on, double now);

    void updateFlashlight(const InputFrame &in, float dt);
    void updateLook(const InputFrame &in);
    void updateMovement(const InputFrame &in, float dt, double now);
    void updateDevKeys(const DevKeys &dev, double now);
    void updateWeapons(const InputFrame &in, float dt, double now);
    void updateBullets(float dt);
    void updateFlare(const InputFrame &in, float dt, double now);
    void updateTapeDeck(const InputFrame &in, float dt, double now);
    void updateInteraction(const InputFrame &in);
    void updateDrink(float dt);
    void updateAmbience(float dt, double now);
    void updateManila(float dt, double now);
    void updateCrates(double now);
    void updateEntity(float dt, double now, bool forceSpawn = false);
    void updateDogs(float dt, double now);
    void updateExits(double now);
    void updateTimers(float dt);
    void updateHealth(float dt);

    // ---- movement pieces
    void updateSprint(bool requested, bool moving, bool crouched, float dt);
    bool updateSwimming(float dt, bool rise);
    void updateSqueeze(bool held, float dt);
    void updateSoftFloor(float dt, double now);
    void updateGait(float spd, float speed, bool inWater, float dt);
    void updateHiding(float speed, float dt);
    void landFrom(float drop, double now);
    // Climb or drop a whole storey: the floating origin moves by one pitch,
    // and everything with a position moves with it (see World::storeyH).
    void changeStorey(int dir, double now);

    // ---- hands
    void updateAim(bool held, float dt);
    bool canReload() const;
    void fireBullet();
    void bulletHitsDog(int i);
    void bulletHitsHunter();
    bool anyFlareLit() const;
    const FlareProj *nearestLitFlare(float x, float z) const;   // for warding: plain distance
    // How much of a fire reaches a floor point: burn left against distance.
    // The point light and the hiss both follow the fire with most presence.
    static float flarePresence(const FlareProj &f, float x, float z);
    const FlareProj *dominantFlare(float x, float z) const;
    bool flyFlare(FlareProj &f, float dt);   // false if it fell in water and went out
    void flyDeck(float dt);

    // ---- pickups and use
    static uint64_t cellKey2(int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; }
    // A cell on the storey you are on. Storey 0 keys as cellKey2.
    uint64_t cellKey(int a, int b) const {
        return cellKey2(a, b) ^ ((uint64_t)(uint32_t)world.storey * 0x9E3779B97F4A7C15ULL);
    }
    // Salt for the loose-item hashes: seed, level, visit and storey.
    uint32_t pickupSalt() const;
    // The one item a cell holds, by fixed priority. Drawing and collecting
    // both come through here.
    Pickup pickupAt(int a, int b);
    bool bottleAt(int a, int b);
    // The surface height a carton stands on in this cell, or -1 for a prop
    // nobody would set a drink on. Matches the prop AABB tops in gatherCellAABBs.
    float bottleShelfY(int a, int b);
    Vec2 pickupSpot(int a, int b);
    bool coinAt(int a, int b);
    bool batteryAt(int a, int b);
    bool tapeAt(int a, int b);
    bool hideSpotAt(int a, int b);            // furniture big enough to tuck in beside
    bool balloonAt(int a, int b, Vec3 &out);
    // A party table's balloon bunch: fills pos[]/colours[] (up to 4) and the
    // knot, returns the count. Drawing and aiming both come through here.
    int tableBalloonBunch(int a, int b, Vec3 *pos, uint8_t *colours, Vec3 &tie);
    bool popBalloonAt(Vec3 point);
    void collectPickups();
    void useWhatIsNear();
    void markChalk();
    bool crateAt(int a, int b);
    void openCrate(int a, int b);
    void markWayOut();               // chalk the way from the Manila Room to the nearest exit

    // ---- the place
    static float sanityDrain(int lv);   // meter-fraction per second
    bool updateSanity(float dt, bool blackout, double now);   // true if it ended the run
    void updateRoomSound(bool blackout);
    bool shiftAWall();               // wall off one doorway you cannot see
    bool packDeaf() const;           // quiet enough for the pack to lose you
    bool clarkLevel() const { return level == 0; }
    const char *hunterName() const { return level == 4 ? "THE PARTYGOER" : level == 0 ? "PIRATE CLARK" : "A SMILER"; }
    bool wayOpen() const { return coins >= ESCAPE_COST; }
    void spawnHunter();
    float packNoise(float &x, float &z) const;   // how far the loudest noise carries, and where
    bool updateDog(int i, float dt, double now, float noise, float nsx, float nsz);   // true if it ended the run
    bool hunterChase(float dt, double now, bool entVisible, float &fearT);   // true if it ended the run

    // ---- the run's shape
    void applyLevel(int lv, double now);
    void seedStrangerChalk();
    // A fresh descent from Level 0: new maze, gear and tallies reset. Records,
    // deaths and wins belong to the player and survive it.
    void beginDescent(double now);
    void startRun(double now);
    void winRun(double now);
    // End the run and return to the title. `title` is the card's headline.
    void dieRun(double now, const char *by, const char *title = "YOU DID NOT GET OUT");
    // A hit from something at (fromX, fromZ). True if it ended the run, in
    // which case beginDescent has already replaced the world.
    bool hurtPlayer(double now, float dmg, const char *by, float fromX, float fromZ);
    // The next blackout, or never. The draw happens either way, so grng stays
    // aligned with a normal run's.
    double blackoutIn(double now, double lead, double span);
    void bankRecords();              // raise `best` to this run's tallies
    LoopCue loopCue();

    // ---- audio out
    AudioEvent &audioEvent(AudioEvent::Kind kind);
    AudioEvent &play(Sfx sfx, int variant = 0);
    void stopVoice();
};
