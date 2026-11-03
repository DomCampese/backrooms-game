#pragma once
// The level's hunter (Clark, a Smiler or the Partygoer). The state machine is
// Sim::updateEntity.

enum class EState { Hidden, Stalk, Chase, Flee, Die };

struct Entity {
    float vx = 0, vz = 0;        // world velocity, for the sprite's lean
    EState st = EState::Hidden;
    float x = 0, z = 0;
    double nextSpawn = 12.0;
    float gaze = 0, life = 0, unseen = 0;
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
