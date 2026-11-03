#pragma once
#include <cstdint>
// The level's hunter (Clark, a Smiler or the Partygoer) and the theater's
// Usher. The state machines are Sim::updateEntity and Sim::updateUsher.

enum class EState { Hidden, Stalk, Chase, Flee, Die };

// The theater's Usher patrols the auditoriums with a torch. It never attacks:
// when its beam holds the player it blows a whistle that brings the hunter
// running. Sim::updateUsher has the rules.
enum class UState { Gone, Patrol, Spot, Down };
struct Usher {
    UState st = UState::Gone;
    float x = 0, z = 0, dispY = 0;
    float vx = 0, vz = 0;        // world velocity, for its heading
    float beamYaw = 0;           // where the torch points: (cos, sin) on the ground plane
    float wpx = 0, wpz = 0;      // next cell on its route
    float roamX = 0, roamZ = 0;  // where it is walking to
    double nextRoam = 0, repathT = 0, nextSpawn = 0;
    float life = 0;              // s in the current state
    float caught = 0;            // s the player has been held in the beam
    float whistleCd = 0;         // s before it can whistle again
    float gait = 0;              // footfall count, as Entity::gait
    int hp = 2;
};
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
