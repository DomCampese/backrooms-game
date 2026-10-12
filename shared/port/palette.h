#pragma once
// Colours both builds draw with: LEVEL FUN's party palette, which balloons,
// confetti, wrapped cartons, table cloths and the wallpaper's bunting share, and
// the byte clamp the painters and the mesher tint with.
#include "mesh_builder.h"

inline unsigned char cl8(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// LEVEL FUN =): one faded palette. The sim picks balloon and confetti colours
// below PARTY_COLOURS (shared/sim/sim.h), an index into this.
constexpr Rgba PARTY_RGBA[5] = {
    { 206, 64, 58, 255 }, { 222, 172, 62, 255 }, { 84, 142, 198, 255 },
    { 106, 178, 92, 255 }, { 182, 96, 178, 255 },
};
