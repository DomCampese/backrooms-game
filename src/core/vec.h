#pragma once
// Small math types for core. Platform code converts to its own vector types at
// the boundary (src/vec_rl.h for raylib).

constexpr float TAU = 6.28318530718f;   // one turn, radians

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
