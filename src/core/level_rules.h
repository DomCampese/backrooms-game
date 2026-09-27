#pragma once
// Per-level numbers the generator and the game rules read. How a level looks
// (light colour and output, fog, gloss) is platform data in src/levels.h, which
// extends this table rather than repeating it.

struct LevelRules {
    float wallH;        // floor to ceiling, metres
    float ls;           // light pitch: ceiling fittings sit on a grid this many metres apart
    float storeyH;      // floor-to-floor pitch, metres; 0 for a level that is one floorplan
    const char *name;   // intro card and window title (UTF-8)
};

constexpr int NLEVELS = 5;

// ls must match the panel mesher, the shader's uLS and the CPU light mirror,
// which all read it from here. Level 1 stays at 12 m: at 8 m nearly all nine
// summed fittings fall inside shadow range and the frame costs 58% more.
// Level 0's storeyH is 24 risers of 180 mm.
inline constexpr LevelRules LEVEL_RULES[NLEVELS] = {
    { 3.0f,  4.0f, 4.32f, "LEVEL 0 · THRESHOLD" },
    { 4.2f, 12.0f, 0.0f,  "LEVEL 1 · HABITABLE ZONE" },
    { 7.5f,  8.0f, 0.0f,  "THE POOLROOMS" },
    { 3.0f,  8.0f, 0.0f,  "THE RED HALLS" },
    { 3.0f,  8.0f, 0.0f,  "LEVEL FUN =)" },
};

// Where each level's exit door leads. The Red Halls and the party lead back to
// Level 0.
inline constexpr int EXIT_NEXT[NLEVELS] = { 1, 2, 4, 0, 0 };
