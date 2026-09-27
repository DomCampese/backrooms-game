#pragma once
// Conversions between core's vector types and raylib's, at the layer boundary.
#include "raylib.h"
#include "core/vec.h"

inline Vector2 toRl(Vec2 v) { return { v.x, v.y }; }
inline Vector3 toRl(Vec3 v) { return { v.x, v.y, v.z }; }
inline Vec2 fromRl(Vector2 v) { return { v.x, v.y }; }
inline Vec3 fromRl(Vector3 v) { return { v.x, v.y, v.z }; }
