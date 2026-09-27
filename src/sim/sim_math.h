#pragma once
// The only raylib include allowed under src/sim/: its value types and raymath.
#include "raylib.h"
#include "raymath.h"
#include <cmath>

// Where a ray meets an axis-aligned box: raylib's GetRayCollisionBox, step for
// step, so a shot resolves to the same distance and normal it always did.
// From inside the box the distance is negative, as there.
inline RayCollision rayBox(Ray ray, BoundingBox box) {
    RayCollision c = {};
    bool inside = ray.position.x > box.min.x && ray.position.x < box.max.x &&
                  ray.position.y > box.min.y && ray.position.y < box.max.y &&
                  ray.position.z > box.min.z && ray.position.z < box.max.z;
    if (inside) ray.direction = Vector3Negate(ray.direction);
    float ix = 1.0f / ray.direction.x, iy = 1.0f / ray.direction.y, iz = 1.0f / ray.direction.z;
    float t0 = (box.min.x - ray.position.x) * ix, t1 = (box.max.x - ray.position.x) * ix;
    float t2 = (box.min.y - ray.position.y) * iy, t3 = (box.max.y - ray.position.y) * iy;
    float t4 = (box.min.z - ray.position.z) * iz, t5 = (box.max.z - ray.position.z) * iz;
    float tNear = (float)fmax(fmax(fmin(t0, t1), fmin(t2, t3)), fmin(t4, t5));
    float tFar = (float)fmin(fmin(fmax(t0, t1), fmax(t2, t3)), fmax(t4, t5));
    c.hit = !(tFar < 0 || tNear > tFar);
    c.distance = tNear;
    c.point = Vector3Add(ray.position, Vector3Scale(ray.direction, c.distance));
    // The normal is the axis the hit point sits furthest out along, scaled to
    // the unit cube; 2.01 rather than 2 keeps that axis past 1 after rounding.
    c.normal = Vector3Subtract(c.point, Vector3Lerp(box.min, box.max, 0.5f));
    c.normal = Vector3Divide(Vector3Scale(c.normal, 2.01f), Vector3Subtract(box.max, box.min));
    c.normal = Vector3Normalize({ (float)(int)c.normal.x, (float)(int)c.normal.y, (float)(int)c.normal.z });
    if (inside) {
        c.distance *= -1.0f;
        c.normal = Vector3Negate(c.normal);
    }
    return c;
}
