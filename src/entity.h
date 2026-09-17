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
    float gait = 0;              // metres run, for the walk cycle — one stride per DOG_STRIDE
    int hp = 2;
};
struct Entity {
    float vx = 0, vz = 0;        // world velocity, for the lean — he tips into where he is going
    EState st = EState::Hidden;
    float x = 0, z = 0;
    double nextSpawn = 12.0;
    float gaze = 0, life = 0, unseen = 0;
    float dispY = 0;   // smoothed floor height under him, so he doesn't pop on stairs
    float lunge = 0;     // the committed lunge: a telegraphed 0.6 s burst, and the only way he lands a catch
    float lungeCd = 0;   // and the breath between attempts, so a missed lunge is a real escape
    float stagger = 0; // brief hitch after taking a round — he slows, he doesn't leave
    // Gait phase, counting footfalls: it gains 1 per stride, so an integer is a
    // foot hitting the floor. It drives the walk-cycle frame AND the footfall
    // sound off the same number, which is the whole reason the animation and
    // the audio cannot drift apart. Same scheme as the player's bobPhase.
    float gait = 0;
    int hp = 3;
    float wpx = 0, wpz = 0;   // current chase waypoint (next cell centre on the path to you)
    double repathT = 0;       // when to recompute the route
};
