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

// The post pass's migraine throb at time t, seconds: a slow double pulse, like
// a heartbeat behind the eyes, from 0 to about `migraine`. The post shader
// (shaders.cpp) keeps its own GLSL copy; change both.
float migraineThrob(float migraine, float t);
// The post pass's colour split at the frame's edge, as a fraction of the
// distance from the centre: fear and the throb widen it.
float colourSplit(float fear, float throb);
