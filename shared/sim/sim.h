#pragma once
// The game's state and rules, apart from how they are drawn and heard. The
// platform (Game, web/src/game.cpp) owns one Sim: each tick it fills an
// InputFrame, calls step() with the clamped frame time and the clock, plays
// `audio` in order, and draws from the state here. See docs/migration.md.
#include "sim_math.h"
#include "input_frame.h"
#include "audio_events.h"
#include "entity.h"
#include "weapon_timing.h"
#include "../core/hash.h"
#include "../core/portable_math.h"
#include "../core/level_rules.h"
#include "../core/world.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

// A thrown flare: flies, bounces off walls, then burns on the floor.
struct FlareProj {
    bool active = false, flying = false;
    bool onStorey = true;    // burning on the storey you are on, not one seen through an opening
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, burn = 0;
};

// The tape player. Playing a tape restores sanity. A playing deck is a noise
// the hunter hears (TAPE_NOISE), so one set down while playing draws him to it.
struct TapeDeck {
    bool carried = true;      // carried, or lying where it was set down
    bool playing = false;
    float t = 0;              // seconds of tape left to run
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    bool flying = false;      // still in the air after being thrown
    float yaw = 0;            // heading it was set down at
    float reel = 0;           // hub rotation, radians
};

// The held item. Keys 1/2/4/5/6 pick one; the wheel cycles in this order. A
// run starts empty-handed, the revolver is skipped until it is found, and the
// cigarettes until they are found and everywhere but the casino.
enum Weapon {
    WEAPON_HANDS = 0,
    WEAPON_REVOLVER,
    WEAPON_FLARE,
    WEAPON_DECK,
    WEAPON_CIGS,
    WEAPON_COUNT,
};

// A loose item lying in a cell. Which one a cell holds is a pure function of
// the cell and the seed (Sim::pickupAt), so drawing and collecting agree
// without storing anything.
enum class Pickup { None, AlmondWater, Doubloon, Battery, Tape, Key, Revolver, Cigarettes };

// Balloon and confetti colours are indices into LEVEL FUN's palette, which the
// platform draws with (PARTY, web/src/util.h, must have this many entries).
constexpr int PARTY_COLOURS = 5;

// The Manila Room's notes: pages of up to four lines.
constexpr int MANILA_NOTE_COUNT = 3;
extern const char *const MANILA_NOTES[MANILA_NOTE_COUNT][4];

// A chalk arrow. `mine` is false for the two pre-placed on each level;
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
    struct Mannequin {
        bool active = false, pinned = false, warded = false;
        Vec3 pos{};
        int storey = 0, pose = 0;
        float grace = 0, lost = 0, gait = 0, stagger = 0, yaw = 0;
        double nextSpawn = 150;
    } mannequin;
    bool spawnMannequin(double now);
    void updateMannequin(const InputFrame &in, float dt, double now);
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
    // Distant one-shots: first at LEAD..LEAD+SPAN s after entering a level,
    // then every GAP..GAP+SPAN s, scaled by 0.45 + 0.55 * sanity.
    static constexpr double DISTANT_LEAD = 20, DISTANT_GAP = 35, DISTANT_SPAN = 50;
    // Exits can be heard: a tone from the nearest WALL_EXIT edge within
    // EXIT_HEAR_M, at (1 - d / EXIT_HEAR_M)^2, times EXIT_THROUGH with a wall
    // in the way.
    static constexpr float EXIT_HEAR_M = 64.0f;
    static constexpr double EXIT_SCAN_GAP = 0.25;   // s between scans for the nearest exit
    static constexpr float EXIT_THROUGH = 0.4f;
    static constexpr float MUSIC_PLAY_MIN = 20.0f, MUSIC_PLAY_SPAN = 20.0f;
    static constexpr float MUSIC_STOP_MIN = 6.0f, MUSIC_STOP_SPAN = 3.0f;
    static constexpr float MUSIC_FIRST = 25.0f;   // s of music on arrival before the first stop, at least
    static constexpr float MUSIC_GRACE = 0.8f;    // s to stop moving after the music stops
    static constexpr float MUSIC_STILL = 0.6f;    // m/s: slower than this counts as standing still
    static constexpr float MUSIC_MASK = 0.6f;     // footstep noise radius while the music plays
    static constexpr int   MAXAMMO = 6;
    // Breath: BREATH_TIME s with the head under water empties it, and
    // BREATH_REFILL s with the head out fills it. Empty, the player loses
    // DROWN_HIT health every DROWN_TICK s, so drowning kills in about 4 s.
    static constexpr float BREATH_TIME = 18.0f;
    static constexpr float BREATH_REFILL = 3.0f;
    static constexpr float DROWN_TICK = 1.0f;
    static constexpr float DROWN_HIT = 0.26f;
    static constexpr float GASP_BELOW = 0.6f;    // surfacing below this much breath gasps
    // Loot on the floor of the Poolrooms' deep water (floor at SUNKEN_DEPTH or
    // lower): a doubloon in SUNKEN_COIN_RATE cells, a carton of almond water in
    // SUNKEN_ALMOND_RATE. A swimmer floating at the surface is too far above it
    // (SUNKEN_REACH m), so it takes a dive.
    static constexpr float SUNKEN_DEPTH = -2.4f;
    static constexpr uint32_t SUNKEN_COIN_RATE = 23;
    static constexpr uint32_t SUNKEN_ALMOND_RATE = 71;
    static constexpr float SUNKEN_REACH = 0.7f;
    // One empty floor cell in this many holds the revolver: about one every
    // dozen chunks, so most runs find it on Level 0 or the theater.
    static constexpr uint32_t REVOLVER_ODDS = 2003;
    static constexpr float REVOLVER_FOUND = 3.2f;   // s the "revolver" banner shows
    // The casino's gold pack of cigarettes (casino.cpp): one empty floor cell
    // in CIGS_ODDS, in the casino only, about one every three chunks. It holds
    // CIGS_PER_PACK. A lit one burns SMOKE_TIME s, and each drag burns
    // DRAG_BURN s more. A drag takes DRAG_TIME s, inhaling for the first
    // DRAG_IN, and gives DRAG_SANITY. One drag in COUGH_ODDS ends in a cough
    // the hunter hears COUGH_NOISE m away. While it is lit, the ember means the
    // player is never in shadow (inShadow).
    static constexpr uint32_t CIGS_ODDS = 607;
    static constexpr int   CIGS_PER_PACK = 10;
    static constexpr float CIGS_FOUND = 3.2f;      // s the banner shows
    static constexpr float SMOKE_TIME = 45.0f;
    static constexpr float DRAG_BURN = 4.0f;
    static constexpr float DRAG_TIME = 2.4f, DRAG_IN = 1.1f;
    static constexpr float DRAG_SANITY = 0.03f;
    static constexpr uint32_t COUGH_ODDS = 4;
    static constexpr float COUGH_NOISE = 14.0f, COUGH_TIME = 0.7f;
    static constexpr float MUZZLE_FLASH = 0.09f;   // s the muzzle flash lights the room
    // Hiding stops the hunter seeing the player and blocks the catch, so it
    // requires standing still. Two thresholds, so hiding does not flicker on and
    // off while the player slows down.
    static constexpr float HIDE_ENTER = 0.4f;   // m/s to be under to start hiding
    static constexpr float HIDE_BREAK = 1.2f;   // m/s that ends hiding
    static constexpr float HIDE_GRACE = 0.35f;  // s above HIDE_BREAK before hiding ends
    static constexpr float TAPE_RUN = 26.0f;    // s: one side of a tape
    static constexpr float TAPE_NOISE = 32.0f;  // m a playing deck carries to the hunter
    static constexpr int   ESCAPE_COST = 12;    // doubloons that buy the way out
    // The catch: the hunter starts a lunge from LUNGE_REACH, the lunge lasts
    // LUNGE_TIME with a warning, and only a lunge in progress can catch the
    // player, inside CATCH_REACH.
    static constexpr float LUNGE_REACH = 2.5f;
    static constexpr float LUNGE_TIME = 0.6f;
    static constexpr float CATCH_REACH = 1.25f;
    // A chasing hunter sees the player within HUNTER_SIGHT m with line of
    // sight; otherwise he walks at SEARCH_SPEED m/s to where he last saw or
    // heard them, then to spots SEARCH_STEP_MIN..MAX m around it, and gives up
    // after SEARCH_TIME s without contact.
    static constexpr float HUNTER_SIGHT = 30.0f;
    // In shadow (no working fitting lighting the spot, lightAtPlayer under
    // SHADOW_LIT) with the flashlight off, a chasing hunter sees the player
    // only within DARK_SIGHT m.
    static constexpr float DARK_SIGHT = 9.0f;
    static constexpr float SHADOW_LIT = 0.3f;
    static constexpr float SEARCH_SPEED = 2.6f;
    static constexpr float SEARCH_STEP_MIN = 3.0f, SEARCH_STEP_MAX = 7.0f;
    static constexpr float SEARCH_TIME = 10.0f;
    // Searching, he first walks to the cover (hideSpotAt) within SEARCH_COVER_R m
    // of where he last saw or heard the player, nearest first, and stands at
    // each for CHECK_TIME s. A hidden player within FIND_REACH m of him, in
    // line of sight, is found: hiding is lost for FOUND_TIME s.
    static constexpr float SEARCH_COVER_R = 8.0f;
    static constexpr float CHECK_TIME = 1.0f;
    static constexpr float FIND_REACH = 1.4f;
    static constexpr float FOUND_TIME = 2.0f;
    // Doors. The use key shuts or opens a doorway within DOOR_REACH m of its
    // middle that the player faces. Shutting or opening one is heard DOOR_NOISE m
    // away for DOOR_NOISE_TIME s. The hunter opens a shut door in his way after
    // DOOR_OPEN_TIME s, knocking on it every DOOR_KNOCK s.
    static constexpr float DOOR_REACH = 1.6f;
    static constexpr float DOOR_NOISE = 14.0f, DOOR_NOISE_TIME = 0.5f;
    // A tossed doubloon (Sim::tossCoin): fire with empty hands throws one at
    // TOSS_SPEED m/s along the view, plus TOSS_LIFT m/s up, every TOSS_GAP s.
    // Each landing faster than 2 m/s is heard COIN_NOISE m from where it hits
    // for COIN_NOISE_TIME s, so a hunter searching for the player goes there.
    // It comes to rest as a spilled doubloon and can be picked up again.
    static constexpr float TOSS_SPEED = 9.0f;
    static constexpr float TOSS_LIFT = 1.6f;
    static constexpr float TOSS_GAP = 0.6f;
    static constexpr float COIN_NOISE = 16.0f;
    static constexpr float COIN_NOISE_TIME = 0.5f;
    static constexpr float DOOR_OPEN_TIME = 1.6f, DOOR_KNOCK = 0.45f;
    static constexpr float DEATH_CARD = 12.0f;  // s the death card stays up: the tape playback, then the text
    static constexpr float DEATH_CARD_HOLD = 1.6f;   // s of it no key can dismiss
    // Health. One hit is survivable and a second before healing is not: two
    // hits are past zero, and REGEN_DELAY + 0.6 / REGEN_RATE (16 s) untouched
    // earns the spare hit back.
    static constexpr float ENTITY_HIT = 0.6f;
    static constexpr float HURT_GRACE = 1.5f;   // s of immunity after a hit
    static constexpr float REGEN_DELAY = 6.0f;
    static constexpr float REGEN_RATE = 0.06f;  // meter-fraction per second
    // Where the hunter spawns: the near band is inside fog range but out of
    // sight; the far band is beyond fog range.
    static constexpr float SPAWN_NEAR_MIN = 6.0f;
    static constexpr float SPAWN_NEAR_MAX = 17.0f;
    static constexpr float SPAWN_FAR_MIN = 20.0f;
    static constexpr float SPAWN_FAR_SPAN = 10.0f;
    static constexpr float W_TAP = 0.3f;        // s: double-tap window for W-to-run
    static constexpr float ENT_STRIDE = 1.05f;  // m per hunter footfall
    // Cells the room-size probe steps each way: 20 m, longer than any Level 0
    // room and shorter than the Level 1 halls, so a corridor measures near 0
    // and a hall near 1.
    static constexpr int   ROOM_PROBE = 10;
    static constexpr float SLIDE_FROM = 0.10f;  // the fraction of the sanity meter that is the terminal slide
    // Walls shift within this many cells of the player, more often as the slide
    // deepens.
    static constexpr int   SHIFT_RING = 8;
    static constexpr double SHIFT_GAP_MIN = 9.0;
    static constexpr double SHIFT_GAP_SPAN = 22.0;
    static constexpr double BLACKOUT_NEVER = 1e18;   // a schedule that never fires
    static constexpr int   MAXCHALK = 128;      // marks per level
    static constexpr float DRINK_TIME = 1.75f;  // s the can is up
    static constexpr uint32_t BACKSTAGE_COIN_RATE = 12;
    static constexpr uint32_t CASINO_COIN_RATE = 60;
    // Level 94's day (motion.cpp), in sim seconds: DAY_TIME of day, DUSK_TIME
    // of dusk, NIGHT_TIME of night and DAWN_TIME of dawn. The player arrives
    // ARRIVE_AT seconds into a day, so the first dusk is 90 s away. Night
    // dims the sky to NIGHT_LIGHT of the day's light (blackoutCur). The
    // Animation is out only at night, starting NIGHT_SPAWN s after dark, and
    // a player inside a house is hidden from it.
    static constexpr float DAY_TIME = 120.0f, DUSK_TIME = 12.0f, NIGHT_TIME = 100.0f, DAWN_TIME = 12.0f;
    static constexpr float ARRIVE_AT = 50.0f;
    static constexpr float NIGHT_LIGHT = 0.03f;
    static constexpr float NIGHT_SPAWN = 6.0f;
    // At night the Animations are plentiful: one that loses the player is
    // back within NIGHT_RETURN s.
    static constexpr float NIGHT_RETURN = 10.0f;
    // The Animation is drawn STOP_MOTION_FPS poses a second, as a stop-motion
    // figure is shot, each pose nudged up to POSE_JITTER m as a hand-moved
    // model is; its position in the sim moves smoothly. Each new pose clicks
    // like a camera's shutter, heard SHUTTER_HEAR m away.
    static constexpr float STOP_MOTION_FPS = 6.0f;
    static constexpr float POSE_JITTER = 0.05f;
    static constexpr float SHUTTER_HEAR = 18.0f;
    // Waiting outside a house it knocks on the walls every KNOCK_GAP..+0.8 s.
    static constexpr float KNOCK_GAP = 1.1f;
    // A bed (PROP_BED) within BED_REACH m of its cell's centre: E sleeps in it
    // at night, with nothing hunting nearby, fading out over SLEEP_FADE s and
    // waking at dawn, healed, with SLEEP_SANITY back.
    static constexpr float BED_REACH = 1.6f;
    static constexpr float SLEEP_FADE = 2.5f;
    static constexpr float SLEEP_SANITY = 0.3f;
    // An almond water truck's hatch is in reach within TRUCK_REACH m of either
    // of its cells' centres.
    static constexpr float TRUCK_REACH = 2.0f;
    // The casino's slot machines (casino.cpp). A spin costs SLOT_COST, the
    // reels run SLOT_SPIN s, and the hunter hears them SLOT_NOISE m from the
    // machine while they do. The prize is drawn at the pull from slotRng. A
    // jackpot rings bells heard SLOT_BELLS_NOISE m away for SLOT_BELLS s and
    // calls the hunter. On average a spin returns a little less than it costs.
    static constexpr int   SLOT_COST = 1;
    static constexpr float SLOT_SPIN = 1.6f;
    static constexpr float SLOT_NOISE = 16.0f;
    static constexpr float SLOT_REACH = 1.6f;      // m from the cell centre to pull the handle
    static constexpr int   SLOT_JACKPOT = 8, SLOT_SMALL = 2;    // doubloons
    static constexpr float SLOT_JACKPOT_ODDS = 1.0f / 16, SLOT_SMALL_ODDS = 0.2f, SLOT_CAN_ODDS = 1.0f / 12;
    static constexpr float SLOT_BELLS_NOISE = 40.0f, SLOT_BELLS = 3.0f;
    static constexpr float SLOT_JACKPOT_SANITY = 0.15f;
    static constexpr int   SNACK_COST = 2;      // doubloons
    static constexpr float SNACK_SANITY = 0.12f;
    // The Usher's torch: it sees the player inside USHER_BEAM_HALF radians of
    // the beam and USHER_BEAM_M metres (half that crouched), and whistles once
    // they have been held there USHER_SPOT seconds.
    static constexpr float USHER_BEAM_M = 14.0f;
    static constexpr float USHER_BEAM_HALF = 0.30f;
    static constexpr float USHER_SPOT = 0.6f;
    static constexpr float USHER_WALK = 1.15f;     // m/s
    static constexpr float USHER_HOLD = 5.0f;      // s it keeps the beam on the player after whistling
    static constexpr float USHER_REST = 25.0f;     // s before it can whistle again
    static constexpr float USHER_HEAR_M = 12.0f;   // a sprint or a lit torch this close turns its beam
    static constexpr float USHER_TORCH_Y = 1.25f;  // the torch above its feet

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
    float py = 0, vy = 0;            // feet above the floor of the current storey
    bool storeyNoted = false;        // said "another floor" once this descent
    float fallFrom = 0;              // highest py since the player last stood on something
    bool grounded = true;
    bool swimming = false;
    float swimPhase = 0, swimClimb = 0;
    float floatT = 0, floatRoll = 0; // surface swell and camera roll; visual only
    float wTapT = 0; bool wSprint = false;
    float health = 1.0f, hurtT = 0, sinceHurt = 0;   // hurtT: immunity left after a hit
    float stamina = 1.0f;
    float breath = 1.0f, drownT = 0;   // breath 0..1; drownT counts to the next DROWN_TICK
    bool headUnder = false;
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
    float softTimer = 0;             // s spent on a rotten patch
    float softSag = 0;               // how far the patch has sagged under the player, m
    double nextGroan = 0;
    float distWalked = 0;
    bool hidden = false;             // crouched beside cover and still
    bool nearCover = false;
    float hideBreakT = 0;            // s moving too fast to hold the spot
    float foundT = 0;                // s the hunter's find keeps the player from hiding again

    // ---- hands
    int weapon = WEAPON_HANDS;
    bool hasRevolver = false;        // found this run; until then key 5 and the wheel skip it
    bool hasCigs = false;            // the casino's pack, found this run
    int cigs = 0;                    // left in it
    struct Smoke {
        float burnT = 0;             // s the lit one has left; 0: none lit
        float dragT = 0;             // s left of the drag being taken
        bool cough = false;          // this drag ends in a cough
        float coughT = 0;            // s left of the cough's noise
        float exhaleT = 0;           // s since the last breath out, for the cloud; large: none
    } smoke;
    Rng smokeRng{1};                 // its own stream, like slotRng
    int ammo = MAXAMMO;
    bool aiming = false;
    float aimBlend = 0;
    float reloadT = 0, gunCd = 0, muzzleT = 0, recoil = 0, wheelCd = 0;
    float muzzleSmoke = 0;           // powder haze after a shot
    struct Bullet { Vec3 pos, tail, direction; float remaining, fade; };
    struct BulletImpact { Vec3 pos, normal; float life; bool body; };
    std::vector<Bullet> bullets;
    std::vector<BulletImpact> bulletImpacts;
    int flares = MAXFLARES;          // carried
    double nextFlareRegen = 0;
    FlareProj litFlares[MAXFLARES];  // one slot per carried flare
    TapeDeck deck;
    float drinkT = 0;                // counts down from DRINK_TIME while the can is up
    bool drinkLanded = false;        // this drink's sanity has been added

    // ---- the run
    int level = 0;
    Entity ent;
    Usher usher;
    float usherDist = 1e9f;          // to the Usher, this tick
    Rng usherRng{1};                 // its own stream, so it does not shift grng on other levels
    float entDist = 1e9f;            // to the hunter, this tick
    float entPrevX = 0, entPrevZ = 0;   // his position last tick, for his velocity
    float playerLight = 1;           // lightAtPlayer, smoothed; kept up only while a hunter is out
    float entDarkCur = 0;            // how much the hunter dims nearby lights
    double nextBlackout = 0, blackoutEnd = -1;
    float blackoutCur = 1.0f, fear = 0.0f;
    float whisperT = 0;
    double nextWhisper = 0;
    // Separate RNG for distant sounds, so they do not shift grng (blackouts,
    // spawns).
    double nextDistant = 0;
    Rng distantRng{1};
    // LEVEL FUN's music stops for MUSIC_STOP_MIN..+SPAN s every
    // MUSIC_PLAY_MIN..+SPAN s. Moving faster than MUSIC_STILL m/s while it is
    // stopped, after MUSIC_GRACE s, calls the Partygoer. Its own stream, so it
    // does not shift grng.
    struct PartyMusic {
        bool playing = true;
        float next = 0;      // s until it stops or starts again
        float stopped = 0;   // s since it stopped
        bool heard = false;  // the Partygoer was called in this stop
        float heardT = 0;    // s the "it heard you" line shows
    } music;
    Rng musicRng{1};
    enum class SlotPrize : uint8_t { None, Small, Can, Jackpot };
    struct SlotSpin {
        float t = 0;          // s left of the reels turning
        float x = 0, z = 0;   // the machine's cell centre, where the noise comes from
        SlotPrize prize = SlotPrize::None;
        float bellsT = 0;     // s left of a jackpot's bells
    } slotSpin;
    // Level 94's clock, s into the day, and whether it is night (the middle
    // of dusk to the middle of dawn, when the Animation is out).
    struct Sky {
        float t = 0;
        bool night = false;
    } sky;
    // Where the Animation was drawn at its last stop-motion frame.
    struct StopPose {
        float x = 0, z = 0, y = 0, gait = 0;
        float t = 0;   // s since that frame
        uint32_t frame = 0;   // poses taken: the hash of the jitter
    } stopPose;
    float knockT = 0;                // s to the Animation's next knock on a house
    float knockedT = 0;              // s since it last knocked, for the HUD
    float sleepT = 0;                // s into falling asleep; 0 awake
    float wakeT = 0;                 // s of waking up left, for the HUD's fade back in
    bool bedNear = false;            // a bed is in reach of the use key, for the HUD
    // Level 94's almond water trucks, by their cab's cell: each gives one can
    // a visit. truckNear: one with a can left is in reach, for the HUD.
    std::unordered_set<uint64_t> trucksServed;
    bool truckNear = false;
    bool sheltered = false;          // Level 94: inside a house, where the Animation cannot find you
    Rng slotRng{1};                  // its own stream, so the casino does not shift grng
    bool slotNear = false;           // a slot machine is in reach of the use key, for the HUD
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

    // Level 0's Manila Room. manilaNear while one is in the loaded chunks
    // around the player, inManila while the player is inside it.
    bool manilaNear = false, inManila = false, manilaSeen = false, notesRead = false;
    float manilaX = 0, manilaZ = 0;
    int notePage = 0;
    // Level 1's supply crates stand where a hash of the cell and an epoch puts
    // them; the epoch moves on when a blackout ends.
    uint32_t crateEpoch = 0;
    std::unordered_set<uint64_t> cratesOpened;   // this epoch's, by cell
    bool crateWasDark = false;

    // ---- carried and collected
    int almond = 0, coins = 0, tapes = 0, keys = 0;
    std::unordered_set<uint64_t> taken;   // world pickups already collected, this level
    std::vector<Vec3> coinsWorld;      // doubloons spilled on the floor
    // A doubloon tossed with empty hands (Sim::tossCoin), in flight until it
    // comes to rest, then one of coinsWorld.
    struct TossedCoin { float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0; };
    std::vector<TossedCoin> coinsFlying;
    float tossCd = 0;                  // s before the next toss
    float coinNoiseT = 0;              // s left of the noise of a doubloon landing
    float coinNoiseX = 0, coinNoiseZ = 0;
    std::vector<ChalkMark> chalk[NLEVELS];   // per level, cleared by beginDescent
    bool chalkSeeded[NLEVELS]{};
    bool chalkSeedPending = false;        // place the level's two pre-placed marks next tick, once px/pz are set
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
    float closeCallT = 0, tapeFoundT = 0, revolverFoundT = 0, cigsFoundT = 0, sanityWarnT = 0;
    float noteT = 0, manilaCardT = 0, deckNoteT = 0;
    const char *tapeLine = "";       // the recovered tape's line
    const char *sanityLine = "";     // the last sanity (or migraine) warning
    const char *deckNote = "";       // one-line notes: the deck, keys, crates, a new floor
    // The doorway the use key would shut or open, found each tick (findDoor),
    // for the HUD's hint.
    struct DoorReach { bool ok = false; int i = 0, k = 0; bool west = false, shut = false; };
    DoorReach doorReach;
    float doorNoiseT = 0;            // s left of the noise of a door shut or opened

    // ---- the tick. step() runs the updates below in this order; each may
    // rely on the ones before it (a shot lands before the hunter reacts).
    void step(const InputFrame &in, float dt, double now);
    // Title screen: the camera drifts and the sim is paused. menuDrift runs
    // before the platform streams chunks, menuBegin after.
    void menuDrift(float dt, double now);
    void menuBegin(const InputFrame &in, double now);
    void setPaused(bool on, double now);

    void updateFlashlight(const InputFrame &in, float dt);
    void updateLook(const InputFrame &in);
    void updateMovement(const InputFrame &in, float dt, double now);
    // Footstep clip: LevelRules::footing for the level, or wood inside the
    // Manila Room.
    Sfx stepSfx() const;
    // Plays LevelRules::distant's one-shot when nextDistant is due.
    void updateDistant(double now);
    void updatePartyMusic(float dt);
    void updateSky(float dt);
    float daylight() const;          // Level 94: 1 by day, NIGHT_LIGHT by night; 1 on other levels
    bool hasBlackouts() const { return level != 2 && level != MOTION; }
    bool hunterAsleep() const { return level == 2 || (level == MOTION && !sky.night); }
    void updateStopPose(float dt);
    bool bedInReach();
    bool truckInReach(int &cabI, int &cabK);   // the truck in reach of the use key, by its cab's cell
    void takeFromTruck(int cabI, int cabK);
    void trySleep();
    void updateSleep(float dt);
    void updateDevKeys(const DevKeys &dev, double now);
    void updateWeapons(const InputFrame &in, float dt, double now);
    void tossCoin();
    void updateTossedCoins(float dt);
    void updateBullets(float dt);
    void updateFlare(const InputFrame &in, float dt, double now);
    void updateTapeDeck(const InputFrame &in, float dt, double now);
    void updateInteraction(const InputFrame &in);
    void updateDrink(float dt);
    void updateAmbience(float dt, double now);
    void updateManila(float dt, double now);
    void updateCrates(double now);
    void updateEntity(float dt, double now, bool forceSpawn = false);
    void updateUsher(float dt, double now);
    void updateSlots(float dt);
    void updateExits(double now);
    void updateTimers(float dt);
    void updateHealth(float dt);

    // ---- movement pieces
    void updateSprint(bool requested, bool moving, bool crouched, float dt);
    bool updateSwimming(float dt, bool rise);
    void updateBreath(float dt, double now);
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
    // A cell on the current storey. Storey 0 keys as cellKey2.
    uint64_t cellKey(int a, int b) const {
        return cellKey2(a, b) ^ ((uint64_t)(uint32_t)world.storey * 0x9E3779B97F4A7C15ULL);
    }
    // Salt for the loose-item hashes: seed, level, visit and storey.
    uint32_t pickupSalt() const;
    // The one item a cell holds, by fixed priority. Drawing and collecting
    // both come through here.
    Pickup pickupAt(int a, int b);
    bool bottleAt(int a, int b);
    // The surface height a carton stands on in this cell, or -1 for a prop with
    // no shelf. Matches the prop AABB tops in gatherCellAABBs.
    float bottleShelfY(int a, int b);
    Vec2 pickupSpot(int a, int b);
    bool backstageAt(int a, int b);          // behind an auditorium's screen (the theater)
    bool stageTapeAt(int a, int b);          // the tape in the middle of the walkway backstage
    bool coinAt(int a, int b);
    Pickup sunkenAt(int a, int b);           // loot on the floor of deep water (Level 2)
    bool batteryAt(int a, int b);
    bool tapeAt(int a, int b);
    bool revolverAt(int a, int b);
    bool hideSpotAt(int a, int b);            // furniture big enough to hide beside
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
    void buySnack();
    void markWayOut();               // chalk the way from the Manila Room to the nearest exit

    // ---- the place
    static float sanityDrain(int lv);   // meter-fraction per second
    bool updateSanity(float dt, bool blackout, double now);   // true if it ended the run
    void updateRoomSound(bool blackout);
    bool shiftAWall();               // close one doorway the player cannot see
    bool clarkLevel() const { return level == 0; }
    const char *hunterName() const {
        return level == LEVEL_FUN ? "THE PARTYGOER" : level == 0 ? "PIRATE CLARK" : level == MOTION ? "AN ANIMATION" : "A SMILER";
    }
    bool wayOpen() const { return coins >= ESCAPE_COST; }
    void spawnHunter();
    float lightAtPlayer();   // 0 dark .. 1 under a working fitting
    bool inShadow() const {
        return ent.st != EState::Hidden && playerLight < SHADOW_LIT && flashCur < 0.5f && smoke.burnT <= 0;
    }
    float loudestNoise(float &x, float &z) const;   // how far the loudest noise carries, and where
    bool hunterChase(float dt, double now, float &fearT);   // true if it ended the run
    bool findDoor(DoorReach &d);            // the doorway in reach and in front, if any
    bool bodyInDoorway(int ci, int ck, bool west) const;   // anyone standing where its leaf swings
    void useDoor();                               // shut or open doorReach
    bool nextCover(Vec2 &spot);                    // the next cover to check while searching
    bool hunterOpensDoor(float dt);              // true while he stands at a shut door, knocking on it
    void hunterNotice(float x, float z);   // the hunter now knows the player is at (x, z)
    void spawnUsher(double now);
    void usherRoamTarget(double now);
    bool inUsherBeam();        // the player is lit by its torch and it can see them
    void usherWhistle();
    void callHunter();               // spawned if hidden, chasing the player either way
    bool slotInReach(int &a, int &b);   // the slot machine cell the use key would pull
    void pullSlot(int a, int b);
    bool cigsAt(int a, int b);
    bool weaponAvailable(int w) const;   // the wheel and the number keys skip what is not
    void updateSmoking(const InputFrame &in, float dt);
    void putOutCigarette();
    void bulletHitsUsher();

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
    void exitCue(float &level, float &pan);   // the nearest exit within EXIT_HEAR_M, as LoopCue::exit
    void scanExits();                          // finds it; run every EXIT_SCAN_GAP s from updateExits
    struct { bool found = false, los = false; float x = 0, z = 0; } nearExit;
    double nextExitScan = 0;

    // ---- audio out
    AudioEvent &audioEvent(AudioEvent::Kind kind);
    AudioEvent &play(Sfx sfx, int variant = 0);
    void stopVoice();
};
