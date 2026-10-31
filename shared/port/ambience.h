#pragma once
// The ambience bed, synthesized a sample at a time: the fluorescent hum, Level
// 1's drone, the hunter's growl, a burning flare's hiss, the whispers, and a
// reverb sized to the room. The rules set `mix` (AmbienceMix); render() fills
// interleaved stereo at SAMPLE_RATE for whatever plays it.
#include "sounds.h"
#include "../sim/audio_events.h"
#include <cstdint>
#include <cmath>

struct Ambience {
    // The targets, set by the rules. Each value below chases its target a
    // sample at a time in render(), so a level change or a blackout is a
    // crossfade rather than a cut.
    AmbienceMix mix;
    float hum = 1, growl = 0, hiss = 0, whisper = 0;
    float wHum = 1, wDrone = 0;
    float panel = 0, space = 0.25f;
    // One running phase per oscillator, indexed by osc(). Each index belongs to
    // one signal: two signals sharing one advance it at both frequencies, and
    // both come out wrong. Current owners:
    //   0-4   fluorescent hum      5-7   growl        8-10  L1 drone
    //   11-12 (free; was poolroom water, now a recording) 13-14 whispers     15    (free; LEVEL FUN music is a recording)
    //   16-17 hum beat frequency  18-19 blackout ring
    double ph[20] = {};
    float lp1 = 0, lp2 = 0;   // one-pole low-passed noise, at two cutoffs
    uint32_t xr = 0x12345u;    // xorshift state for frand()
    float frand() { xr ^= xr << 13; xr ^= xr >> 17; xr ^= xr << 5; return (float)(xr & 0xFFFFFF) / 8388608.0f - 1.0f; }
    // Sine at freq, advanced by one sample. The phase is per oscillator and kept
    // in double: a float accumulating tens of thousands of additions a second
    // loses audible precision within a couple of minutes of play.
    float osc(int i, float freq) {
        constexpr double TURN = 6.283185307;   // not TWO_PI: Unreal defines that as a macro
        ph[i] += TURN * freq / SAMPLE_RATE;
        if (ph[i] > TURN) ph[i] -= TURN;
        return sinf((float)ph[i]);
    }
    // ---- AUD-04: a Schroeder reverb on the ambience bus, so a corridor and a
    // large hall sound different. Four parallel combs into two series
    // allpasses; the comb feedback follows `space`.
    //
    // The lengths are the standard mutually prime Schroeder set and must stay
    // mutually prime: two with a common factor line up into a pitched ring
    // instead of a diffuse tail. The left and right allpass chains differ,
    // which is what makes the output stereo.
    static constexpr int RV_L0 = 1557, RV_L1 = 1617, RV_L2 = 1491, RV_L3 = 1422;
    static constexpr int RV_A0 = 225, RV_A1 = 556, RV_A2 = 341, RV_A3 = 441;
    float cb0[RV_L0] = {}, cb1[RV_L1] = {}, cb2[RV_L2] = {}, cb3[RV_L3] = {};
    float ap0[RV_A0] = {}, ap1[RV_A1] = {}, ap2[RV_A2] = {}, ap3[RV_A3] = {};
    int ci0 = 0, ci1 = 0, ci2 = 0, ci3 = 0, ai0 = 0, ai1 = 0, ai2 = 0, ai3 = 0;
    float comb(float *b, int n, int &i, float x, float fb) {
        float y = b[i];
        b[i] = x + y * fb;
        if (++i >= n) i = 0;
        return y;
    }
    float allpass(float *b, int n, int &i, float x) {
        float y = b[i];
        b[i] = x + y * 0.5f;
        if (++i >= n) i = 0;
        return y - x;
    }

    // Interleaved stereo, `frames` frames.
    void render(int16_t *stereo, int frames);
};
