#pragma once
// How a run begins. The platform decides a SimStart before the first tick
// (the Web build from its BACKROOMS_* knobs, a replay from a trace) and
// sets the sim up in this order:
//
//   simBegin(sim, start, now);
//   sim.applyLevel(0, now);                  // after the level's look is made
//   if (start.level >= 0) sim.applyLevel(start.level, now);
//   simPlace(sim, start);
//
// The order keeps grng's draws where they have always been.
#include "sim.h"

struct SimStart {
    unsigned seed = 1337;
    bool exitTest = false;        // exit doors everywhere
    bool manilaTest = false;      // a Manila Room in the chunk east of spawn
    bool noBlackout = false, fixedSeed = false, keepRecords = true;
    bool placed = false;          // start beside (x, z) facing yaw, not beside (15, 15)
    float x = 15, z = 15, yaw = 0;
    bool pitched = false;         // and at this pitch, up positive
    float pitch = 0;
    int level = -1;               // enter this level after Level 0; -1 stays on Level 0
    bool storeyed = false;        // move to `storey` on a storeyed level
    int storey = 0;
    bool flash = false;           // torch on
    bool menu = true;             // open on the title screen
    float fov = 70;               // the window's base vertical FOV, deg
    Records best;                 // the player's records, as loaded
};

// Seed, spawn point and the run's first schedules. `now` is the clock once
// the platform has loaded its assets.
void simBegin(Sim &sim, const SimStart &s, double now);
// After the levels are entered: the storey, the torch and the title screen.
void simPlace(Sim &sim, const SimStart &s);
