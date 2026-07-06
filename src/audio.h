#pragma once
#include "raylib.h"
#include <cstdint>
#include <cmath>

// ------------------------------------------------------------- audio synth
// Continuously synthesized ambience: fluorescent hum, room tone, entity growl.
struct AudioSynth {
    AudioStream stream;
    float humTarget = 1, hum = 1, growlTarget = 0, growl = 0;   // humTarget ducks all ambience
    float hissTarget = 0, hiss = 0;                             // burning flare hiss
    float whisperTarget = 0, whisper = 0;                       // something in the walls
    float tHum = 1, tDrone = 0, tWater = 0, tParty = 0;         // per-level ambience mix targets
    float wHum = 1, wDrone = 0, wWater = 0, wParty = 0;
    double musT = 0; int musI = 0;                              // LEVEL FUN music box position
    double ph[16] = {};
    float lp1 = 0, lp2 = 0, lp3 = 0;
    uint32_t xr = 0x12345u;
    float frand() { xr ^= xr << 13; xr ^= xr >> 17; xr ^= xr << 5; return (float)(xr & 0xFFFFFF) / 8388608.0f - 1.0f; }
    float osc(int i, float freq) {
        ph[i] += 6.283185307 * freq / 44100.0;
        if (ph[i] > 6.283185307) ph[i] -= 6.283185307;
        return sinf((float)ph[i]);
    }
    void init();
    void update();
};
