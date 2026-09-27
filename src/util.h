#pragma once
// Platform helpers shared by every module: core's math and hashes, plus the
// pixel and audio constants that only the raylib side uses.
#include "raylib.h"
#include "core/vec.h"
#include "core/hash.h"
#include <cstdint>

// Everything audible is generated at this rate: the one-shot Waves in sfx.cpp
// and the ambience stream in audio.cpp both run on it.
constexpr int SAMPLE_RATE = 44100;

inline unsigned char cl8(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// LEVEL FUN =) — the party decorations share one faded palette
extern const Color PARTY[5];
