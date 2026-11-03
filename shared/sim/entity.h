#pragma once
#include <cstdint>
// The level's hunter (Clark, a Smiler or the Partygoer). The state machine is
// Sim::updateEntity.

enum class EState { Hidden, Stalk, Chase, Flee, Die };

struct Entity {
    float vx = 0, vz = 0;        // world velocity, for the sprite's lean
    EState st = EState::Hidden;
    float x = 0, z = 0;
    double nextSpawn = 12.0;
    float gaze = 0, life = 0;
    // Where he last saw or heard the player, and s since then. In a chase
    // without contact he walks there and searches (Sim::hunterChase).
    float lastX = 0, lastZ = 0, lostT = 0;
    float doorT = 0;   // s he has stood at the shut door in front of him
    // His search: the centre (the last place he saw or heard the player), the
    // cover he has already checked there (cell keys, oldest overwritten), and
    // s left standing at the cover he is checking now.
    float searchX = 0, searchZ = 0;
    uint64_t checked[6] = {};
    int nChecked = 0;
    float checkT = 0;
    bool atCover = false;
    float dispY = 0;   // smoothed floor height, so he does not jump on stairs
    float lunge = 0;     // s left in a lunge (LUNGE_TIME); only a lunge can catch the player
    float lungeCd = 0;   // s before he can lunge again
    float stagger = 0; // s of slowdown after being shot; he does not flee
    // Footfall count: +1 per stride, so an integer is a footfall. Drives both the
    // walk-cycle frame and the footstep sound, so they stay in step. Same
    // scheme as the player's bobPhase.
    float gait = 0;
    int hp = 3;
    float wpx = 0, wpz = 0;   // chase waypoint: next cell centre on the path to the player
    double repathT = 0;       // when to recompute the route
};
