// Level 94: the day and night, the Animation's stop-motion pose, and the
// houses that shelter the player at night.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

float Sim::daylight() const {
    if (level != MOTION) return 1.0f;
    const float dusk = DAY_TIME, night = dusk + DUSK_TIME, dawn = night + NIGHT_TIME;
    const float t = sky.t;
    if (t < dusk) return 1.0f;
    if (t < night) return 1.0f + (NIGHT_LIGHT - 1.0f) * (t - dusk) / DUSK_TIME;
    if (t < dawn) return NIGHT_LIGHT;
    return NIGHT_LIGHT + (1.0f - NIGHT_LIGHT) * (t - dawn) / DAWN_TIME;
}

// Night runs from the middle of dusk to the middle of dawn, while the sky is
// at or under halfway dark.
void Sim::updateSky(float dt) {
    sheltered = false;
    if (level != MOTION) return;
    if (inMenu || deathT > 0) return;
    const float cycle = DAY_TIME + DUSK_TIME + NIGHT_TIME + DAWN_TIME;
    sky.t = fmodf(sky.t + dt, cycle);
    const bool night = sky.t >= DAY_TIME + DUSK_TIME * 0.5f && sky.t < DAY_TIME + DUSK_TIME + NIGHT_TIME + DAWN_TIME * 0.5f;
    if (night != sky.night) {
        sky.night = night;
        deckNoteT = 2.6f;
        deckNote = night ? "it's dark. the Animations come out at night. get inside a house."
                         : "morning. nothing moves out there in the day.";
        if (night) play(Sfx::Scare).atPitch(0.55f).atVolume(0.5f);
    }
    sheltered = world.indoors(cellOf(px), cellOf(pz));
}

// The pose the Animation is drawn in holds for 1 / STOP_MOTION_FPS s, then
// jumps to where the sim has moved it.
void Sim::updateStopPose(float dt) {
    StopPose &p = stopPose;
    p.t += dt;
    if (p.t < 1.0f / STOP_MOTION_FPS && ent.st != EState::Hidden) return;
    p = { ent.x, ent.z, ent.dispY, ent.gait, 0.0f };
}
