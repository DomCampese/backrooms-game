#pragma once
// PIRATE CLARK: the thing in the fog. State machine lives in Game::updateEntity.

enum class EState { Hidden, Stalk, Chase, Flee, Die };
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
