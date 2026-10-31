#pragma once
// raylib handles for the game's sound (port/sounds.h).
#include "raylib.h"
#include "port/sounds.h"
#include <cstdint>

// A stereo position as a bearing: -1 hard left, 0 centre, +1 hard right. Every
// `SetSoundPan` call goes through this, because raylib 6.0 changed what the
// argument means without renaming it:
//
//   5.5 and earlier:  left = pan,             right = 1 - pan   (0 = hard RIGHT)
//   6.0 and later:    right = (pan + 1) / 2,  left  = 1 - right (-1 = hard left)
//
// With 5.5 values on 6.0, 0.5 "centre" plays 75% right and hard left plays hard
// right, so nothing reaches the left channel.
//
// If the headers have no version macros, assume 6.0 or later.
static inline float panFor(float bearing) {
#if !defined(RAYLIB_VERSION_MAJOR) || RAYLIB_VERSION_MAJOR >= 6
    return bearing;
#else
    return 0.5f - bearing * 0.5f;
#endif
}

// The clips themselves are synthesized in port/sounds.cpp; these load them,
// and the recordings embedded from assets/sounds, into raylib.
Sound loadPcmSound(const Pcm &pcm);
Sound loadEmbeddedSound(const char *key);   // a recording from assets/sounds, e.g. "sounds/water/swim_1.ogg"
Music loadEmbeddedMusic(const char *key);   // the same, streamed and looping
