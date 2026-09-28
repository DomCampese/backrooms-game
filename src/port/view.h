#pragma once
// What a renderer shows of the sim: where the eye is, where it looks, which way
// is up (the camera leans into a strafe and rolls with a swimmer's swell), and
// the vertical field of view. The raylib build and a port both draw from this,
// so the camera cannot drift between them. Metres, the sim's storey frame.
#include "../sim/sim.h"

struct SimView {
    Vec3 eye, forward, up;
    float fovY;   // vertical, degrees
};

SimView simView(const Sim &sim);

// The window's base vertical field of view, degrees: the horizontal view is
// locked to what 70 vertical gives at 1440 x 850, clamped so a square window
// does not go fisheye and a phone on its side does not go binoculars. `aim`
// (0..1) lets an ultrawide narrow while aiming. The sim eases toward it
// (InputFrame::screenFov).
float windowFovY(int w, int h, float aim);
