#pragma once
// One-shot sound effects, all synthesized at startup — no audio assets.
#include "raylib.h"
#include <cstdint>

Sound makeFootstep(uint32_t seed);
Sound makeJumpscare();
Sound makeSplash(uint32_t seed, bool big);
Sound makeClick();
Sound makeFlareStrike();
Sound makeGunshot();
Sound makeWinChime();
