#pragma once
// PIRATE CLARK: the thing in the fog. State machine lives in Game::updateEntity.

enum class EState { Hidden, Stalk, Chase, Flee, Die };

// The Red Halls have their own residents: a pack that hunts by sound. They are
// faster than you and cannot be outrun, only broken off — by fire, by a bullet,
// or by going quiet long enough that they lose the thread.
enum class DState { Gone, Prowl, Charge, Yelp };
struct Dog {
    DState st = DState::Gone;
    float x = 0, z = 0, dispY = 0;
    float wpx = 0, wpz = 0;      // next cell on the route to wherever it's headed
    float roamX = 0, roamZ = 0;  // where it's nosing about while it hasn't heard you
    double nextRoam = 0;
    double repathT = 0;
    float life = 0;              // time in the current state
    float lost = 0;              // how long it has been off your scent
    double nextBark = 0;
    int hp = 2;
};
struct Entity {
    EState st = EState::Hidden;
    float x = 0, z = 0;
    double nextSpawn = 12.0;
    float gaze = 0, life = 0, unseen = 0;
    float dispY = 0;   // smoothed floor height under him, so he doesn't pop on stairs
    float lunge = 0;   // mid-chase burst of speed
    int hp = 3;
    float wpx = 0, wpz = 0;   // current chase waypoint (next cell centre on the path to you)
    double repathT = 0;       // when to recompute the route
};
