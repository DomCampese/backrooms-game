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
    // AUD-03: how hard you are standing under a live fluorescent, 0..1, and
    // AUD-04: how big the space around you sounds, 0 (corridor) to 1 (hall).
    // Game::updateAmbience sets the targets once a frame; they are chased a
    // sample at a time below so neither one steps.
    float panelTarget = 0, panel = 0;
    float spaceTarget = 0.25f, space = 0.25f;
    double musT = 0; int musI = 0;                              // LEVEL FUN music box position
    // One running phase per oscillator, indexed by osc(). The index is an
    // OWNERSHIP claim, not a scratch slot: two signals sharing one advance it at
    // both their frequencies, so each gets the other's detune folded in and both
    // come out subtly wrong rather than obviously broken. Current owners:
    //   0-4   fluorescent hum      5-7   growl        8-10  L1 drone
    //   11-12 poolroom water      13-14 whispers     15    LEVEL FUN music box
    //   16-17 hum beat frequency  18-19 blackout ring
    double ph[20] = {};
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
    // ---- AUD-04: a Schroeder reverb on the ambience bus.
    //
    // The game's whole subject is architecture and it was one flat stereo
    // stream, so a cramped corridor and a 30 m warehouse hall were acoustically
    // identical and the ears never confirmed what the eyes were being told.
    // Four parallel combs into two series allpasses is the cheapest thing that
    // sells a room, and the comb feedback rides `space`, so the decay is the
    // building's rather than a constant.
    //
    // Lengths are the classic mutually-prime Schroeder set. They MUST stay
    // mutually prime: pick two with a common factor and their echo trains line
    // up into a pitched ring rather than a diffuse tail, which sounds like a
    // broken oscillator and not like a room. The two allpass chains differ
    // between left and right, which is the whole of the stereo width — the
    // stream was previously the same sample in both channels.
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

    void init();
    void update();
};
