#pragma once
// One-shot sound effects, all synthesized at startup — no audio assets.
#include "raylib.h"
#include <cstdint>

Sound makeFootstep(uint32_t seed);
Sound makeJumpscare();
Sound makeSplash(uint32_t seed, bool big);
Sound makeClick();
Sound makeBalloonPop();
Sound makeFlareStrike();
Sound makeGunshot();
Sound makeWinChime();
Sound makeHeartbeat();
Sound makeTapeChime();
Sound makeValveTurn();             // seized iron giving way, then the clunk of it seating
Sound makeDogBark(uint32_t seed);  // the pack, somewhere in the red dark
Sound makeDogHowl();
