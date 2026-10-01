#pragma once
// Platform helpers shared by every module: core's math and hashes, plus the
// pixel helpers that only the raylib side uses.
#include "raylib.h"
#include "core/vec.h"
#include "core/hash.h"
#include <cstdint>

inline unsigned char cl8(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// LEVEL FUN =) — the party decorations share one faded palette
extern const Color PARTY[5];
