#pragma once
// Revolver timings. The rules and the model's clip playback (revolver.cpp)
// both read these, so the animation keeps pace with the gun.
constexpr float SHOT_INTERVAL = 0.22f;   // s between shots; the Shoot clip plays over it
constexpr float RELOAD_TIME = 1.8f;      // s; the Reload clip plays over it
