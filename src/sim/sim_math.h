#pragma once
// Vector, ray and box math for the sim, on core's Vec3. Each function repeats
// raymath's formula in raymath's operation order, so every non-NaN result is
// bit-identical to raymath's. Build with -ffp-contract=off: a fused
// multiply-add changes the last bit.
#include "../core/vec.h"
#include <cmath>

inline Vec3 add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 scale(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }
inline Vec3 negate(Vec3 v) { return { -v.x, -v.y, -v.z }; }
// Per component, as raymath's Vector3Divide.
inline Vec3 divide(Vec3 a, Vec3 b) { return { a.x / b.x, a.y / b.y, a.z / b.z }; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z) };
}
// Multiplies by 1/length rather than dividing, and returns a zero vector
// unchanged, as raymath does.
inline Vec3 normalize(Vec3 v) {
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length != 0.0f) {
        float inv = 1.0f / length;
        v.x *= inv; v.y *= inv; v.z *= inv;
    }
    return v;
}

struct Ray3 { Vec3 position, direction; };
struct Box3 { Vec3 min, max; };
struct RayHit { bool hit; float distance; Vec3 point, normal; };

// Where a ray meets an axis-aligned box: raylib's GetRayCollisionBox, step for
// step, so a shot resolves to the same distance and normal it always did.
// From inside the box the distance is negative, as there.
inline RayHit rayBox(Ray3 ray, Box3 box) {
    RayHit c = {};
    bool inside = ray.position.x > box.min.x && ray.position.x < box.max.x &&
                  ray.position.y > box.min.y && ray.position.y < box.max.y &&
                  ray.position.z > box.min.z && ray.position.z < box.max.z;
    if (inside) ray.direction = negate(ray.direction);
    float ix = 1.0f / ray.direction.x, iy = 1.0f / ray.direction.y, iz = 1.0f / ray.direction.z;
    float t0 = (box.min.x - ray.position.x) * ix, t1 = (box.max.x - ray.position.x) * ix;
    float t2 = (box.min.y - ray.position.y) * iy, t3 = (box.max.y - ray.position.y) * iy;
    float t4 = (box.min.z - ray.position.z) * iz, t5 = (box.max.z - ray.position.z) * iz;
    float tNear = (float)fmax(fmax(fmin(t0, t1), fmin(t2, t3)), fmin(t4, t5));
    float tFar = (float)fmin(fmin(fmax(t0, t1), fmax(t2, t3)), fmax(t4, t5));
    c.hit = !(tFar < 0 || tNear > tFar);
    c.distance = tNear;
    c.point = add(ray.position, scale(ray.direction, c.distance));
    // The normal is the axis the hit point sits furthest out along, scaled to
    // the unit cube; 2.01 rather than 2 keeps that axis past 1 after rounding.
    c.normal = sub(c.point, lerp(box.min, box.max, 0.5f));
    c.normal = divide(scale(c.normal, 2.01f), sub(box.max, box.min));
    c.normal = normalize({ (float)(int)c.normal.x, (float)(int)c.normal.y, (float)(int)c.normal.z });
    if (inside) {
        c.distance *= -1.0f;
        c.normal = negate(c.normal);
    }
    return c;
}
