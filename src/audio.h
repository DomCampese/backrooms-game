#pragma once
#include "raylib.h"
#include "util.h"
#include <cstdint>
#include <cmath>

// ------------------------------------------------------------- audio synth
// Continuously synthesized ambience: fluorescent hum, room tone, entity growl.
struct AudioSynth {
    AudioStream stream;
    float humTarget = 1, hum = 1, growlTarget = 0, growl = 0;   // humTarget ducks all ambience
    float hissTarget = 0, hiss = 0;                             // burning flare hiss
    float whisperTarget = 0, whisper = 0;                       // something in the walls
    // Per-level ambience mix. applyLevel sets the t* targets; the w* weights
    // chase them a sample at a time in update(), so a level change is a
    // crossfade rather than a cut.
    float tHum = 1, tDrone = 0, tWater = 0, tParty = 0;
    float wHum = 1, wDrone = 0, wWater = 0, wParty = 0;
    double musT = 0; int musI = 0;                              // LEVEL FUN music box position
    double ph[16] = {};        // one running phase per oscillator, indexed by osc()
    float lp1 = 0, lp2 = 0, lp3 = 0;   // one-pole low-passed noise, at three cutoffs
    uint32_t xr = 0x12345u;    // xorshift state for frand()
    float frand() { xr ^= xr << 13; xr ^= xr >> 17; xr ^= xr << 5; return (float)(xr & 0xFFFFFF) / 8388608.0f - 1.0f; }
    // Sine at freq, advanced by one sample. The phase is per oscillator and kept
    // in double: a float accumulating tens of thousands of additions a second
    // loses audible precision within a couple of minutes of play.
    float osc(int i, float freq) {
        constexpr double TWO_PI = 6.283185307;
        ph[i] += TWO_PI * freq / SAMPLE_RATE;
        if (ph[i] > TWO_PI) ph[i] -= TWO_PI;
        return sinf((float)ph[i]);
    }
    void init();
    void update();
};
