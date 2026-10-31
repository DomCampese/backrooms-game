#pragma once
// The level's hunter (Clark, a Smiler or the Partygoer) and the Red Halls
// dogs. The state machines are Sim::updateEntity and Sim::updateDog.

enum class EState { Hidden, Stalk, Chase, Flee, Die };

// The Red Halls pack hunts by sound. Dogs are faster than the player, so they
// cannot be outrun; a flare, a shot or staying quiet long enough breaks off a
// chase.
enum class DState { Gone, Prowl, Charge, Yelp };
struct Dog {
    DState st = DState::Gone;
    float x = 0, z = 0, dispY = 0;
    float wpx = 0, wpz = 0;      // next cell on its route
    float roamX = 0, roamZ = 0;  // wander target while it has not heard the player
    double nextRoam = 0;
    double repathT = 0;
    float life = 0;              // time in the current state
    float lost = 0;              // s since it last heard the player
    double nextBark = 0;
    float gait = 0;              // footfall count: +1 per DOG_STRIDE metres, drives the walk cycle
    int hp = 2;
};
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
    float wpx = 0, wpz = 0;   // waypoint: next cell centre on the route to the player or a noise
    double repathT = 0;       // when to recompute the route
    // While stalking, he walks toward the last noise he heard (Sim::hunterListen).
    bool tracking = false;
    float heardX = 0, heardZ = 0;
    // A new stalk starts with no noise heard and no route.
    void forgetTrack() { tracking = false; repathT = 0; }
};
