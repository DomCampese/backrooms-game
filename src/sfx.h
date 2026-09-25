#pragma once
// One-shot sound effects, all synthesized at startup — no audio assets.
#include "raylib.h"
#include <cstdint>

// Where a sound sits across the stereo field, as a bearing the game can reason
// about: -1 hard left, 0 centre, +1 hard right. Every `SetSoundPan` call goes
// through this, because raylib changed what the argument means and did it
// silently — no rename, no deprecation, and the mixer takes any float.
//
//   5.5 and earlier:  left = pan,             right = 1 - pan   (0 = hard RIGHT)
//   6.0 and later:    right = (pan + 1) / 2,  left  = 1 - right (-1 = hard left)
//
// Feed 6.0 the old 0..1 values and 0.5 "centre" plays 75% right, hard left
// (1.0) plays hard right, and nothing in the game ever reaches the left
// channel at all. It sounds like a mix decision, not a bug, which is why it
// survived: the pack calling from your left simply answers from your right.
//
// The sandbox's cffi headers have no version macros at all (the setup script
// puts back only the #defines the game needs), so an unknown version is
// treated as current rather than as 5.5 — a wrong guess there would fail the
// same silent way, and every raylib from 6.0 on works this way.
static inline float panFor(float bearing) {
#if !defined(RAYLIB_VERSION_MAJOR) || RAYLIB_VERSION_MAJOR >= 6
    return bearing;
#else
    return 0.5f - bearing * 0.5f;
#endif
}

// `through` builds the same one-shot as heard through geometry: two poles of
// low-pass, a softened transient and a level drop. Every positional sound in
// the game was pitch, volume and pan only, so a footfall 20 m away through two
// walls sounded exactly like one 20 m away down an open corridor — and on
// Level 0 a point 20 m out is occluded 83% of the time, so that was most of
// what the player ever heard. Two sample sets pick up most of what a real
// filter bus would, and fit the synthesize-everything-at-startup design.
Sound makeFootstep(uint32_t seed, bool through = false);
Sound makeJumpscare();
Sound loadEmbeddedSound(const char *key);   // a recording from assets/sounds, e.g. "sounds/water/swim_1.ogg"
Music loadEmbeddedMusic(const char *key);   // the same, streamed and looping
Sound makeClick();
Sound makeBalloonPop();
Sound makeFlareStrike();
Sound makeGunshot();
Sound makeWinChime();
Sound makeHeartbeat();
Sound makeTapeChime();
Sound makeValveTurn();             // seized iron giving way, then the clunk of it seating
Sound makeDogBark(uint32_t seed, bool through = false);  // the pack, somewhere in the red dark
Sound makeDogHowl();
Wave makeGulpWave();               // deterministic PCM, also used by audio regression
Sound makeGulp();                   // three soft swallows of almond water
Sound makeTapeVoice();              // a voice off a worn cassette — garbled, hissing, and looping
Sound makeFloorGroan();             // rotten subfloor taking your weight, and not liking it
Sound makeNoclip();                 // passing through a wall that was not quite there
