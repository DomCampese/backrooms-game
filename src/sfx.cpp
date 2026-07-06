#include "sfx.h"
#include "util.h"
#include <cmath>

// ---------------------------------------------------------------- sounds
static Wave makeWaveBuf(int frames) {
    Wave w = {};
    w.frameCount = (unsigned)frames; w.sampleRate = 44100; w.sampleSize = 16; w.channels = 1;
    w.data = MemAlloc(frames * 2);
    return w;
}
static float clampf1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

Sound makeFootstep(uint32_t seed)
{
    const int sampleRate = 44100;
    const int n = (int)(0.22f * sampleRate);

    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;

    Rng r((uint64_t)seed * 2000u + 3u);

    float thump = 52.0f + r.f01() * 8.0f;
    float knock = 92.0f + r.f01() * 12.0f;
    float toeDelay = 0.055f + r.f01() * 0.02f;

    for (int i = 0; i < n; i++)
    {
        float t = (float)i / sampleRate;

        // Heel impact
        float env1 = (1.0f - expf(-t * 1200.0f)) * expf(-t * 18.0f);

        float heel =
            (sinf(6.2831853f * thump * t) * 1.45f +
             sinf(6.2831853f * knock * t) * 0.50f)
            * env1;

        // Toe impact
        float toe = 0.0f;
        float tt = t - toeDelay;
        if (tt > 0.0f)
        {
            float env2 = (1.0f - expf(-tt * 900.0f)) * expf(-tt * 42.0f);

            toe =
                (sinf(6.2831853f * 120.0f * tt) * 0.50f +
                 sinf(6.2831853f * 180.0f * tt) * 0.18f)
                * env2;
        }

        // Tiny bit of texture
        float noise =
            (r.f01() * 2.0f - 1.0f) *
            expf(-t * 120.0f) *
            0.03f;

        // Mix
        float sample = heel + toe + noise;

        // Soft saturation for punch
        sample = tanhf(sample * 1.45f);

        d[i] = (short)(clampf1(sample) * 32000.0f);
    }

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

Sound makeJumpscare() {
    int n = (int)(1.1f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xDEADULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.35f * (wn - lp);
        float shriek = lp * expf(-t * 3.2f) * 1.6f;
        float sweep = sinf(6.2831853f * (210 - 150 * t) * t) * expf(-t * 2.4f);
        float sub = sinf(6.2831853f * 38 * t) * expf(-t * 1.6f);
        d[i] = (short)(clampf1(tanhf((shriek + sweep * 0.8f + sub * 1.1f) * 2.4f) * expf(-t * 1.1f)) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeSplash(uint32_t seed, bool big) {
    int n = (int)((big ? 0.55f : 0.32f) * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r((uint64_t)seed * 6151u + 11u);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.05f * (wn - lp);
        lp2 += 0.16f * (wn - lp2);
        float env = (1 - expf(-t * 160)) * expf(-t * (big ? 6.5f : 11.0f));
        float s = (lp * 2.6f + lp2 * 0.9f * expf(-t * 20)) * env * (big ? 0.8f : 0.5f);
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeClick() {
    int n = (int)(0.035f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xC11CULL);
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float s = (r.f01() * 2 - 1) * expf(-t * 320) * 0.5f + sinf(6.2831853f * 2100 * t) * expf(-t * 260) * 0.35f;
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeFlareStrike() {
    int n = (int)(0.8f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xF1A2EULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        float scratch = (wn - lp) * expf(-t * 34.0f) * 1.3f;                  // striker scrape
        float pop = sinf(6.2831853f * 150.0f * t) * expf(-t * 42.0f) * 0.8f;  // ignition pop
        float swell = (wn - lp) * (1 - expf(-t * 9.0f)) * expf(-t * 2.6f) * 0.5f; // hiss hands off to synth
        d[i] = (short)(clampf1(tanhf(scratch + pop + swell)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeGunshot() {
    int n = (int)(0.9f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x6A17ULL);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.60f * (wn - lp);
        lp2 += 0.07f * (wn - lp2);
        float crack = lp * expf(-t * 170.0f) * 2.8f;                          // supersonic crack
        float body = lp2 * expf(-t * 16.0f) * 2.4f;                           // blast body
        float thump = sinf(6.2831853f * (72.0f - 30.0f * t) * t) * expf(-t * 8.0f) * 1.2f;
        float tail = lp2 * expf(-t * 3.2f) * 0.4f;                            // hallway slap-back
        float s = tanhf((crack + body + thump + tail) * 1.9f) * expf(-t * 0.9f);
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeWinChime() {
    int n = (int)(1.8f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    const float freqs[3] = { 392.0f, 523.25f, 659.25f };
    const float starts[3] = { 0.0f, 0.28f, 0.56f };
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f, s = 0;
        for (int k = 0; k < 3; k++) {
            float lt = t - starts[k];
            if (lt < 0) continue;
            s += sinf(6.2831853f * freqs[k] * lt) * (1 - expf(-lt * 30)) * expf(-lt * 2.4f) * 0.20f;
        }
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}
