#include "../core/fp_strict.h"
#include "view.h"
#include <cmath>

namespace {
// raylib's DEG2RAD and RAD2DEG, as it computes them, so the raylib build's
// field of view is unchanged to the bit.
constexpr float PI_F = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI_F / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI_F;
}

SimView simView(const Sim &sim) {
    SimView v;
    v.eye = { sim.px, sim.eyeY, sim.pz };
    v.forward = sim.fwd;
    float roll = sim.leanCur * -0.035f + sim.squeezeBlend * 0.07f + sim.floatRoll;
    v.up = { sim.r2x * sinf(roll), cosf(roll), sim.r2z * sinf(roll) };
    v.fovY = sim.fov;
    return v;
}

float windowFovY(int w, int h, float aim) {
    if (w <= 0 || h <= 0) return 70.0f;   // no window yet: the authored number
    const float refAspect = 1440.0f / 850.0f;
    float anchorX = 2.0f * atanf(tanf(35.0f * DEG_TO_RAD) * refAspect);   // radians across at 70 vertical
    // raylib's own identity (render.cpp's culling cone uses it too), so the
    // lock cannot drift from what the camera draws.
    float fovy = 2.0f * atanf(tanf(anchorX * 0.5f) * h / w) * RAD_TO_DEG;
    if (w >= refAspect * h) {
        // Wide: keep the authored 70 vertical, so ultrawides gain peripheral view.
        fovy = clampf(fovy, 70.0f, fminf(100.0f, 78.0f + 8.0f * aim));
    }
    return fmaxf(fovy, 58.0f);   // portrait floor: ~69 deg horizontal at 0.46
}
