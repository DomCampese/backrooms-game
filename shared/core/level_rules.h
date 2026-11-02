#pragma once
// Per-level numbers the generator and the game rules read. How a level looks
// (light colour and output, fog, gloss) is platform data in shared/port/levels.h, which
// extends this table rather than repeating it.

// Floor material, which picks the footstep clip (Sim::stepSfx). Must match the
// floor surface in shared/port/levels.cpp's LEVEL_SURFACES. Wood is used only
// inside the Manila Room.
enum class Footing : unsigned char { Carpet, Concrete, Tile, Wood };

// Which distant one-shot a level plays (Sim::updateDistant): a door closing,
// a metal clang, pipe knocking, a balloon pop, a film's low boom through a
// wall, or none.
enum class Distant : unsigned char { None, Door, Clang, Knock, Pop, Boom };

struct LevelRules {
    float wallH;        // floor to ceiling, metres
    float ls;           // light pitch: ceiling fittings sit on a grid this many metres apart
    float storeyH;      // floor-to-floor pitch, metres; 0 for a level that is one floorplan
    const char *name;   // intro card and window title (UTF-8)
    // Which fittings work, from the tube hash (chunkLayout, the shader's
    // lightState, lightAtCPU): `dead` is the share that are out, `vary` how much
    // dimmer the worst working tube is than the best, `faulty` the share that
    // stutter.
    float dead;
    float vary;
    float faulty;
    Footing footing;
    Distant distant;
};

constexpr int NLEVELS = 6;

// ls must match the panel mesher, the shader's uLS and the CPU light mirror,
// which all read it from here. Level 1 stays at 12 m: at 8 m nearly all nine
// summed fittings fall inside shadow range and the frame costs 58% more.
// Level 0's storeyH is 24 risers of 180 mm.
inline constexpr LevelRules LEVEL_RULES[NLEVELS] = {
    //  wallH  ls      storeyH  name                          dead   vary   faulty  footing           distant
    { 3.0f,  4.0f, 4.32f, "LEVEL 0 · THRESHOLD",      0.20f, 0.42f, 0.16f, Footing::Carpet,   Distant::Door },
    { 4.2f, 12.0f, 0.0f,  "LEVEL 1 · HABITABLE ZONE", 0.30f, 0.35f, 0.20f, Footing::Concrete, Distant::Clang },
    { 7.5f,  8.0f, 0.0f,  "THE POOLROOMS",            0.06f, 0.0f,  0.07f, Footing::Tile,     Distant::None },
    { 3.0f,  8.0f, 0.0f,  "THE RED HALLS",            0.45f, 0.0f,  0.07f, Footing::Concrete, Distant::Knock },
    { 3.0f,  8.0f, 0.0f,  "LEVEL FUN =)",             0.10f, 0.0f,  0.07f, Footing::Carpet,   Distant::Pop },
    { 5.0f,  8.0f, 0.0f,  "THE MULTIPLEX",            0.25f, 0.30f, 0.10f, Footing::Carpet,   Distant::Boom },
};

// The level whose chunks World::generate fills with auditoriums.
constexpr int MULTIPLEX = 5;

// Where each level's exit door leads: Level 0, the multiplex, Level 1, the
// Poolrooms, LEVEL FUN, and back to Level 0. A cursed exit leads to the Red
// Halls, which lead back to Level 0.
inline constexpr int EXIT_NEXT[NLEVELS] = { MULTIPLEX, 2, 4, 0, 0, 1 };

// How deep each level is, for the records' "deepest": the order the exits
// reach them, with the Red Halls where their number put them before the
// multiplex was added. The records file stores the level number, not this.
inline constexpr int LEVEL_DEPTH[NLEVELS] = { 0, 2, 3, 4, 5, 1 };
