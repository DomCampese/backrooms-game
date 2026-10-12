#pragma once
// Platform helpers shared by every module: core's math and hashes, plus the
// pixel helpers that only the raylib side uses.
#include "raylib.h"
#include "core/vec.h"
#include "core/hash.h"
#include "port/palette.h"
#include <cstdint>

// port/palette.h's PARTY_RGBA, as raylib colours
extern const Color PARTY[5];
