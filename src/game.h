#pragma once
// All run state plus the per-frame update/render orchestration. One Game
// instance owns the world, the player, the weapons, and PIRATE CLARK.
#include "raylib.h"
#include "util.h"
#include "world.h"
#include "levels.h"
#include "entity.h"
#include "audio.h"
#include "revolver.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

// one thrown flare: arcs, clatters off walls, burns on the floor
struct FlareProj {
    bool active = false, flying = false;
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, burn = 0;
};

// The tape player, and what the cassettes you find are actually for. Play one
// in your hand and a voice that isn't the building's puts some of your grip
// back — but a running deck is a noise source, and the Red Halls pack hunts by
// noise. Set it down still playing and the sound is *there* instead of here,
// which is the only lever you have on them that isn't fire or a bullet.
struct TapeDeck {
    bool carried = true;      // in your coat, or lying wherever you set it down
    bool playing = false;
    float t = 0;              // seconds of tape left to run
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    bool flying = false;      // still in the air after you tossed it
    float yaw = 0;            // heading it was set down at, so it doesn't snap when it lands
    float reel = 0;           // hub rotation, so the spin says the tape is moving
};

// Which of the three things you can hold is in your hands. Keys 1/2/4 pick one
// directly; the mouse wheel cycles through them in this order.
enum Weapon {
    WEAPON_REVOLVER = 0,
    WEAPON_FLARE,
    WEAPON_DECK,
    WEAPON_COUNT,
};

// A loose item lying in a cell, waiting to be walked over. Which one a cell
// holds is a pure function of the cell and the world seed — see Game::pickupAt
// — so the renderer and the pickup test always agree without storing anything.
enum class Pickup { None, AlmondWater, Doubloon, Battery, Tape, Key };

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

// The Manila Room's notes: pages of up to four lines (game.cpp).
constexpr int MANILA_NOTE_COUNT = 4;
extern const char *const MANILA_NOTES[MANILA_NOTE_COUNT][4];

// `mine` separates the marks you drew from the ones that were already there.
// They are the same arrow; only the chalk has aged.
struct ChalkMark { Vector3 pos; float yaw; bool mine; };

struct Game {
    // tuning
    static constexpr float PR = 0.34f;        // player radius
    static constexpr int   MAXFLARES = 3;
    static constexpr float FLAREBURN = 9.0f;  // seconds
    static constexpr float FLAREFADE = 1.5f;  // seconds of guttering at the end of a burn
    static constexpr float FLAREFALL = 0.05f; // how fast a fire's presence drops off with distance
    static constexpr int   MAXAMMO = 6;
    // Hiding is worth a lot — it blinds the hunt, blocks the catch, and runs
    // ent.unseen at 2.4x — so it is priced in stillness. Two thresholds, not
    // one, so the spot doesn't flicker on and off while you settle into it.
    static constexpr float HIDE_ENTER = 0.4f;   // m/s you must be under to tuck in
    static constexpr float HIDE_BREAK = 1.2f;   // m/s that gives you away again
    static constexpr float HIDE_GRACE = 0.35f;  // s above that before it costs you the spot
    static constexpr float TAPE_RUN = 26.0f;    // one side of a tape, as far as you'll listen
    static constexpr float TAPE_NOISE = 32.0f;  // how far a playing deck carries, in metres
    static constexpr int   ESCAPE_COST = 12;  // doubloons that buy your way out for good
    // The catch, and the windup you get to react to. He commits from LUNGE_REACH
    // and can only take you inside CATCH_REACH while that commit is still
    // running, which is LUNGE_TIME long and announced when it starts.
    static constexpr float LUNGE_REACH = 2.5f;
    static constexpr float LUNGE_TIME = 0.6f;
    static constexpr float CATCH_REACH = 1.25f;
    static constexpr float DEATH_CARD = 7.0f;   // seconds the death card holds the title screen
    // Health. A landed lunge or a bite takes a chunk instead of the run; after
    // a hit you get HURT_GRACE of immunity and are shoved clear, and health
    // creeps back once nothing has touched you for REGEN_DELAY.
    // One hit is survivable, a second before you have healed is not: 0.6 twice
    // is past zero, and it takes REGEN_DELAY + 0.6/REGEN_RATE (16 s) untouched
    // to earn the spare hit back.
    static constexpr float ENTITY_HIT = 0.6f;
    static constexpr float PACK_BITE = 0.6f;
    static constexpr float HURT_GRACE = 1.5f;
    static constexpr float REGEN_DELAY = 6.0f;
    static constexpr float REGEN_RATE = 0.06f;   // meter-fraction per second
    // Where he arrives from. The near band is inside the fog and close enough
    // to matter; the far band is the old behaviour, kept in the mix because
    // replacing one fixed ritual with another buys nothing.
    static constexpr float SPAWN_NEAR_MIN = 6.0f;
    static constexpr float SPAWN_NEAR_MAX = 17.0f;
    static constexpr float SPAWN_FAR_MIN = 20.0f;
    static constexpr float SPAWN_FAR_SPAN = 10.0f;

    // env/test knobs (BACKROOMS_* — see README)
    bool benchmark = false, cleanShot = false;
    float captureTime = -1;
    std::vector<float> frameSamples;
    const char *shotPath = nullptr;
    int shotFrame = 600;                      // BACKROOMS_SHOTFRAME: capture earlier, for quick looks
    bool noBlackout = false;                  // BACKROOMS_NOBLACKOUT: suppress the random schedule

    // resources
    Texture2D texClark{}, texEntity{}, texEntityGlow{}, texPartygoer{}, texProps{}, texScrawl{}, texFixtures{}, texAO{}, texOcc{}, texDog{},
              texAlmondWrap{}, texDeck{}, texParticle{};
    Revolver revolver;
    Mesh flareMesh{};
    Mesh canMesh{};                            // the almond water can, real geometry
    Mesh deckMesh{}, reelMesh{}, deckLampMesh{};   // the tape player, its reels, its record lamp
    // light-occlusion grid: the floorplan around you, uploaded for the shader to
    // march. Recentred as you walk; OCC_N cells wide, so it always covers more
    // than the fog can show you.
    static constexpr int OCC_N = 64;
    std::vector<unsigned char> occBuf;
    int occOriginI = 0, occOriginK = 0;
    bool occValid = false;
    Texture2D floorTexs[NLEVELS]{}, ceilTexs[NLEVELS]{}, wallTexs[NLEVELS]{};   // per-level surface sets
    Texture2D floorDetails[NLEVELS]{}, ceilDetails[NLEVELS]{}, wallDetails[NLEVELS]{};
    std::vector<Texture2D> surfaceDetails; // owns unique maps; levels may share them
    Texture2D neutralDetail{}, propDetail{};
    Shader worldShader{}, postShader{};
    int locTime = -1, locBlackout = -1, locViewPos = -1, locFlash = -1, locFlashDir = -1,
        locAmb = -1, locFogCol = -1, locFogDen = -1, locLightCol = -1, locLS = -1, locLY = -1,
        locDead = -1, locLightMul = -1, locFlarePos = -1, locFlareInt = -1, locGloss = -1,
        locEntPos = -1, locEntDark = -1, locOccOrigin = -1, locOccN = -1, locEntBlock = -1,
        locVary = -1, locFaulty = -1, locWet = -1, locWetFrom = -1, locRoomMask = -1, locLamp = -1;
    int locPTime = -1, locPFear = -1, locPWater = -1, locPMigraine = -1;
    Material mats[MAT_COUNT]{};
    Sound steps[4]{}, sndNoclip{}, splashIn[3]{}, splashOut[3]{}, swimStrokes[4]{}, sndClick{}, sndScare{}, sndWin{},
          sndFlare{}, sndShot{}, sndHit{}, sndKill{}, sndPop{}, sndHeartbeat{}, sndTape{},
          sndValve{}, sndHowl{}, sndGulp{}, sndVoice{}, sndGroan{};
    static constexpr int NBARKS = 3;
    Sound sndBarks[NBARKS]{};                   // the pack, panned to whichever one spoke
    Sound entSteps[4]{};                        // the thing's own footfalls, panned + attenuated
    // The same two sets again, as heard through geometry. Picked on
    // lineOfSight at the moment of playback — see AUD-02. Knowing a thing is
    // near is worth much less than knowing where it is, and a game that plays
    // the open-corridor sample through two walls is telling you the second
    // thing when it only knows the first.
    Sound sndBarksThrough[NBARKS]{};
    Sound entStepsThrough[4]{};
    AudioSynth synth;
    World world;
    Rng grng{1};
    RenderTexture2D rt{};

    // player
    float px = 0, pz = 0;
    float yaw = 0.8f, pitch = 0.0f;
    float velx = 0, velz = 0;
    float py = 0, vy = 0;                     // feet height relative to the dry deck
    bool grounded = true;
    bool swimming = false;
    float swimPhase = 0, swimClimb = 0;
    // Surface float: the view rides slow overlapping swells and rolls with them,
    // fading out as you go under. Visual only; buoyancy physics are unchanged.
    float floatT = 0, floatRoll = 0;
    // Looped recordings: the muffled water while your head is under, and LEVEL
    // FUN's music. Both stream and loop; volumes are eased.
    Music musUnderwater{}, musParty{};
    float underwaterVol = 0, partyVol = 0, loopT = 0;
    static constexpr float W_TAP = 0.3f;      // double-tap window for W-to-run
    float wTapT = 0; bool wSprint = false;
    float health = 1.0f, hurtT = 0, sinceHurt = 0;   // hurtT: immunity left after a hit
    float stamina = 1.0f, fov = 70.0f;        // camera fovy, deg — the action pull;
                                              // screen shape rides on top in baseFov()
    float bobPhase = 0;                       // counts footfalls: an integer is a foot landing
    bool flashOn = false;
    float flashCur = 0;
    float battery = 1.0f;                     // flashlight charge, 0..1 — drains while on, dead at 0

    // per-frame derived (look/movement feeds weapons, entity, and render)
    Vector3 fwd{ 1, 0, 0 };
    float f2x = 1, f2z = 0, r2x = 0, r2z = 1;
    bool sprinting = false, sprintExhausted = false;
    float bobAmt = 0, eyeY = 1.62f;
    bool captureClick = false;                // this click grabbed the mouse; don't also fire
    float leanCur = 0, landDip = 0;           // camera feel: strafe lean + landing dip
    float strafeInput = 0;                    // -1..1, set by movement, read by render lean
    float entPrevX = 0, entPrevZ = 0;         // last frame's position, to derive his velocity
    static constexpr float ENT_STRIDE = 1.05f;   // metres per step — also the footfall spacing
    static constexpr float DOG_STRIDE = 0.85f;   // the pack's, which is quicker and shorter
    // How far the reverb's room-size probe marches in each of four directions.
    // 10 cells is 20 m, which is past anything Level 0 has and short of the
    // Level 1 halls — so a corridor reads near 0 and a warehouse near 1.
    static constexpr int ROOM_PROBE = 10;
    // STK-03: the fraction of the grip meter that is the terminal slide.
    static constexpr float SLIDE_FROM = 0.10f;
    // PAC-03: how far out the building is allowed to rearrange itself, and how
    // often. The gap shortens as the slide deepens, so it starts as something
    // you are not sure happened and ends as something you cannot keep up with.
    static constexpr int   SHIFT_RING = 8;
    static constexpr double SHIFT_GAP_MIN = 9.0;
    static constexpr double SHIFT_GAP_SPAN = 22.0;
    struct Bullet { Vector3 pos, tail, direction; float remaining, fade; };
    struct BulletImpact { Vector3 pos, normal; float life; bool body; };
    std::vector<Bullet> bullets;
    std::vector<BulletImpact> bulletImpacts;
    void fireBullet();
    void updateBullets(float dt);
    bool squeezing = false;
    float squeezeBlend = 0;
    void updateSqueeze(bool held, float dt);
    float muzzleSmoke = 0;                    // powder haze lingering after a shot

    // flare weapon: thrown, burns orange, Pirate Clark won't go near one.
    // `flares` is the count in your coat; `litFlares` is what is burning out
    // there. One slot per flare you can carry: this was a single FlareProj
    // once, so throwing a second one overwrote the first mid-burn and that
    // fire simply ceased to exist — no light, no hiss, no smoke.
    int flares = MAXFLARES;
    double nextFlareRegen = 0;
    FlareProj litFlares[MAXFLARES];

    // the tape player: a voice for your grip, or a noise to send them somewhere else
    TapeDeck deck;
    float deckNoteT = 0;                      // brief line when you thread or set down a tape
    const char *deckNote = "";

    // revolver: traveling rounds, six rounds, three hits put Clark down
    int weapon = WEAPON_REVOLVER;                // see enum Weapon — keys 1/2/4, or the wheel
    int ammo = MAXAMMO;
    bool aiming = false;
    float aimBlend = 0;
    void updateAim(bool held, float dt);
    bool canReload() const;
    float reloadT = 0, gunCd = 0, muzzleT = 0, recoil = 0, wheelCd = 0;

    // run state
    int level = 0;
    Entity ent;
    static constexpr int MAXDOGS = 3;
    Dog dogs[MAXDOGS];                        // the Red Halls pack
    double nextPack = 0;                      // when the halls send the next one
    double nextHowl = 0;
    float entDist = 1e9f;                     // distance to Clark this frame
    float entDarkCur = 0;                     // how hard it's smothering the lights (ramps with the hunt)
    // 1e18 is "never": far enough out that the schedule can never fire. The
    // poolrooms have always used it; headless captures now borrow it too.
    static constexpr double BLACKOUT_NEVER = 1e18;
    double nextBlackout = 0, blackoutEnd = -1;
    float blackoutCur = 1.0f, fear = 0.0f;
    float deathT = 0, escapeT = 0, killT = 0, fellT = 0, winT = 0;
    bool noclipped = false;                   // the last exit was a Level 0 wall, not a door
    // ---- the Manila Room (Level 0). manilaNear is true while one is in the
    // chunks round you, which is what points the shader's uRoomMask/uLamp at
    // it; inManila while you are inside its four walls.
    bool manilaNear = false, inManila = false, manilaSeen = false, notesRead = false;
    float manilaX = 0, manilaZ = 0;
    float noteT = 0; int notePage = 0;        // the note being read, and how long it stays up
    float manilaCardT = 0;                    // the name card the first time you walk in
    // ---- the hum's migraine. The lore is specific: the buzz "tends to induce
    // throbbing migraines in most individuals, which persist for an extended
    // period of time even after one has exited the level." So it builds while
    // you are on Level 0 under the tubes, eases off slowly anywhere else (it
    // follows you through a noclip), and fastest in the Manila Room's quiet.
    float migraine = 0;
    bool migraineWarned = false;
    // ---- Level 1's supply crates. "Crates of supplies appear and disappear
    // randomly within the Level", and during a blackout "supplies are liable to
    // vanish inexplicably". So where they stand is a hash of the cell and an
    // epoch, and the epoch moves on every time a blackout ends: the dark comes
    // down, and when the tubes come back the crates are somewhere else.
    Mesh crateMesh{}, crateLidMesh{};
    uint32_t crateEpoch = 0;
    std::unordered_set<uint64_t> cratesOpened;   // this epoch's, by cell
    bool crateWasDark = false;
    bool crateAt(int a, int b);
    void updateCrates(float dt, double now);
    void openCrate(int a, int b);
    void updateManila(float dt, double now);
    void markWayOut();                        // chalk the way from the Manila Room to the nearest noclip wall
    // Stats frozen for the death card. Being caught used to cost nothing at all
    // — it teleported you 800 m and you kept every item — so there was nothing
    // in the game that could be lost, which is most of why none of it was
    // frightening. Now it ends the run, and the card says what took you.
    const char *deathBy = "";
    const char *deathTitle = "YOU DID NOT GET OUT";
    float deathTime = 0; int deathM = 0, deathLevel = 0, deathKills = 0;
    float softTimer = 0;                      // how long you've stood on a soft patch
    float softSag = 0;                        // ...and how far it has let you down while you did
    double nextGroan = 0;                     // the subfloor complaining, re-triggered as it worsens
    int deathCount = 0, escapeCount = 0, killCount = 0, winCount = 0;
    int deepest = 0;                          // deepest level this descent reached
    bool still = false;                       // under HIDE_ENTER this frame — what the pack listens for
    float slide = 0;                          // STK-03: how far into the terminal slide, 0..1
    double nextShift = 0;                     // PAC-03: when the building next moves on you
    int visits[NLEVELS] = { 0 };              // how many times this descent has entered each level
    // Shutting the standpipes pays out a cache of doubloons. That used to be
    // per visit, and the cursed exit (1 in 6) drops you straight back into the
    // Red Halls — 9 doubloons a lap against an ESCAPE_COST of 12, so you could
    // bank your way out without ever meeting Clark. Once per descent.
    bool pipesPaid = false;
    float winTime = 0; int winM = 0, winKills = 0;   // stats frozen for the escape screen
    float distWalked = 0;
    double runStart = 0;
    bool wayOpen() const { return coins >= ESCAPE_COST; }   // enough doubloons to leave for good
    bool debugHud = false;
    int frame = 0;

    // pickups, currency, chalk, ambient events, records
    std::unordered_set<uint64_t> taken;       // world pickups already grabbed (reset per level)
    std::vector<Vector3> coinsWorld;          // doubloons Clark spills when he goes down
    // Navigation marks, kept per level for the whole descent. A mark is the only
    // counter-play the game offers to not knowing where you are, and finding one
    // of your own again is the good moment; clearing them at every doorway threw
    // that away. Cleared by beginDescent, not by applyLevel.
    std::vector<ChalkMark> chalk[NLEVELS];
    bool chalkSeeded[NLEVELS]{};              // the stranger's marks are laid once per level per descent
    bool chalkSeedPending = false;            // ...and on the first frame after arrival, once px/pz are real
    static constexpr int MAXCHALK = 128;      // per level
    std::unordered_set<uint64_t> poppedBalloons;     // LEVEL FUN ceiling balloons already shot
    std::unordered_set<uint64_t> poppedTableBunches; // and party-table balloon bunches
    struct Confetti { Vector3 pos, vel; float life; Color col; };
    std::vector<Confetti> confetti;           // bursts from popped balloons
    int almond = 0, coins = 0, tapes = 0;
    // Keys open the locked doors the generator leaves about one chunk in
    // three. They are per descent, like everything else you are carrying.
    int keys = 0;
    float boostT = 0, crouchCur = 0, whisperT = 0;

    // ---- your grip on the place. Drains the whole time you're down here, faster
    // the deeper you go and faster still in the dark or while something is
    // hunting you. Almond water puts it back in one go; a tape playing where you
    // can hear it puts it back slowly, for as long as you let it run.
    float sanity = 1.0f;
    int sanityStage = 0;            // deepest threshold crossed, so each warning fires once
    float sanityWarnT = 0;          // brief overlay when it slips a notch
    const char *sanityLine = "";
    double nextHeartbeat = 0;       // low sanity: you start hearing yourself
    static float sanityDrain(int lv);   // per-level base drain, meter-fraction per second

    // ---- drinking: a scripted little animation, not an instant stat bump
    static constexpr float DRINK_TIME = 1.75f;
    float drinkT = 0;               // counts DOWN from DRINK_TIME while the can is up
    bool drinkLanded = false;       // the swallow already paid out this time
    double nextWhisper = 0;
    bool hidden = false;                      // crouched, tucked beside cover and still — the hunt can't find you
    bool nearCover = false;                   // cover is right there; whether you are using it is a question of speed
    float hideBreakT = 0;                     // how long you have been moving too fast to hold the spot
    float closeCallT = 0, tapeFoundT = 0;      // brief overlays: it stood right there / a tape found
    const char *tapeLine = "";                 // which recovered-tape line to show
    char bestPath[512] = {};
    int bestEsc = 0, bestKill = 0, bestM = 0, bestWins = 0, bestTapes = 0;
    int bestDeep = 0, bestRun = 0;            // deepest level reached, longest run in seconds
    bool everFlashed = false;                 // HUD: flashlight reminder until first use
    bool inMenu = false;                      // title screen up, world drifting behind it
    bool paused = false;                      // P: the world holds its breath
    double pausedAt = 0;                      // when it stopped, so schedules can be slid on resume

    // Red Halls puzzle: shut every standpipe and the halls give up what they hold
    static constexpr int VALVES_NEEDED = 3;
    std::unordered_set<uint64_t> valvesTurned;
    float valveT = 0;                         // brief note after closing one
    bool pipesShut = false;                   // all three closed on this descent

    void winRun(double now);                  // stepped through the true way out — reset the descent
    // Something got you: end the run and go back to the title. `title` is the
    // card's headline — the place taking you is not the same ending as being
    // caught, and it should not use the same words.
    void dieRun(double now, const char *by, const char *title = "YOU DID NOT GET OUT");
    // Take a hit from something at (fromX, fromZ). Returns true if it ended the
    // run (dieRun has then already replaced the world), false if you survived it.
    bool hurtPlayer(double now, float dmg, const char *by, float fromX, float fromZ);
    // Who hunts this level: Pirate Clark on Level 0, the Partygoer in LEVEL FUN,
    // a Smiler everywhere else. One entity, one AI; only the look and name change.
    bool clarkLevel() const { return level == 0; }
    const char *hunterName() const { return level == 4 ? "THE PARTYGOER" : level == 0 ? "PIRATE CLARK" : "A SMILER"; }
    void updateHealth(float dt);              // grace countdown and regeneration
    void updateLoopAudio(float dt);          // feed and fade the looping recordings (water, LEVEL FUN music)
    bool shiftAWall();                        // PAC-03: wall off one doorway you cannot see
    bool packDeaf() const;                    // ENT-04: are you quiet enough for the pack to lose you
    void updateMenu(double now);              // drift the title-screen camera; any key begins
    void startRun(double now);                // leave the menu and start a fresh descent
    // Throw away the current descent and set up a fresh one from Level 0: a new
    // maze, you back at the start of it, gear and tallies reset. Records and the
    // win count survive, because those belong to the player rather than the run.
    void beginDescent(double now);
    // Salt for the loose-item hashes: the seed, plus which level and which
    // visit. It was world.seed alone, so every level put its cartons and
    // doubloons in the same cells and every revisit put them all back.
    uint32_t pickupSalt() const;
    double blackoutIn(double now, double lead, double span);   // next blackout, or never

    void init();
    bool tick();                              // one frame; false = run ended (headless shot taken)
    void shutdown();

    void applyLevel(int lv);
    void seedStrangerChalk();
    void saveBest();

    // deterministic world pickups, keyed by cell
    static uint64_t cellKey2(int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; }
    // Which loose item, if any, this cell holds. One cell can only offer one
    // thing, so the checks run in a fixed priority order; both the renderer and
    // the pickup test go through here so they can never disagree.
    Pickup pickupAt(int a, int b);
    bool bottleAt(int a, int b);              // almond water, left out for whoever needs it
    // A carton sitting on furniture rather than the floor: returns the surface
    // height to stand it on, or -1 if this cell's prop is nothing you'd set a
    // drink down on. Shared by the mesher-side render and the pickup test.
    float bottleShelfY(int a, int b);
    Vector2 pickupSpot(int a, int b);         // where in the cell the item stands (render + pickup test)
    void updateDrink(float dt, double now); // run the drinking animation
    void drawHeldWeapon(const Camera3D &cam);
    void drawCan(Matrix xf);                // one can, lit by the room like anything else
    void drawDrinkCan(const Camera3D &cam); // the can in your hand, mid-drink
    void updateTapeDeck(float dt, double now);       // thread a tape, set the deck down, run the reels
    void drawDeck(Matrix xf, bool lamp);             // one tape player, reels and all
    void drawHeldDeck(const Camera3D &cam);          // the deck in your hand, as real geometry
    bool coinAt(int a, int b);                // a doubloon he dropped on his rounds
    bool batteryAt(int a, int b);             // a spare battery, tucked somewhere
    bool tapeAt(int a, int b);                // a cassette tape, someone else's recovered days
    bool hideSpotAt(int a, int b);            // furniture big enough to tuck in beside
    bool balloonAt(int a, int b, Vector3 &out);   // LEVEL FUN ceiling balloon centre, if one floats here
    // party-table balloon bunch in this cell: fills pos[]/cols[] (up to 4), the
    // knot point, and returns the count (0 = no bunch). Shared by render + aim.
    int tableBalloonBunch(int a, int b, Vector3 *pos, Color *cols, Vector3 &tie);
    bool popBalloonAt(Vector3 point);

    // update, in frame order (game.cpp)
    void updateLook();
    // Base camera fovy for the current window: locks the horizontal view so a
    // narrow portrait phone does not play through a 34 deg keyhole. Called
    // once a frame from the FOV smoothing in updateMovement — read `fov`.
    float baseFov() const;
    // The same mapping as a pure function of window size, for the harness.
    static float fovForWindow(int w, int h, float aim);
    void updateMovement(float dt);
    bool updateSwimming(float dt, bool rise);
    void updateSprint(bool requested, bool moving, bool crouched, float dt);
    void updateDevKeys(double now);
    void updateWeapons(float dt, double now);
    void updateFlare(float dt, double now);
    // The questions the rest of the game asks about burning flares, rather than
    // the whole array. Clark and the pack want plain distance — they turn at a
    // radius, and a fire two metres away is two metres away however low it has
    // burned. The one point light and the one hiss channel instead want the
    // fire that is actually doing the lighting, which is `flarePresence`.
    bool anyFlareLit() const;
    const FlareProj *nearestLitFlare(float x, float z) const;   // null if none are burning
    // How much of a fire reaches a point on the floor: how hard it is still
    // burning, against how far off it is. The hiss scales its volume by this
    // and the renderer picks the point light by it, so the loudest flare and
    // the one lighting the room are always the same flare.
    static float flarePresence(const FlareProj &f, float x, float z);
    const FlareProj *dominantFlare(float x, float z) const;     // null if none are burning
    void updateInteraction();
    void updateAmbience(float dt, double now);
    void updateEntity(float dt, double now);
    void updateDogs(float dt, double now);    // the Red Halls pack: hunts by sound
    void updateExits(double now);
    void streamChunks();
    void updateOccupancy();                   // recentre + re-upload the light-occlusion grid

    // render (render.cpp)
    void renderScene(double now);             // 3D world into the offscreen target
    void renderUI(double now);                // post pass, viewmodel, HUD, overlays
};
