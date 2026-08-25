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

// a balloon giving up: sharp latex burst, then the rubbery flap of the skin
Sound makeBalloonPop() {
    int n = (int)(0.16f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xBA11ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.5f * (wn - lp);
        float burst = (wn * 0.7f + lp) * expf(-t * 150.0f) * 2.4f;             // the crack
        float flap = sinf(6.2831853f * (150.0f - 320.0f * t) * t) * expf(-t * 26.0f) * 0.5f; // skin snap
        float s = tanhf((burst + flap) * 1.6f);
        d[i] = (short)(clampf1(s) * 30000);
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

// two low thumps, close together — held breath while it lingers right beside you
Sound makeHeartbeat() {
    int n = (int)(1.0f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    const float starts[2] = { 0.0f, 0.34f };
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f, s = 0;
        for (int k = 0; k < 2; k++) {
            float lt = t - starts[k];
            if (lt < 0) continue;
            float env = (1 - expf(-lt * 500.0f)) * expf(-lt * 14.0f);
            s += sinf(6.2831853f * 58.0f * lt) * env * 0.85f;
        }
        d[i] = (short)(clampf1(tanhf(s * 1.3f)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// a tape found: two detuned, warbling low tones through a bed of hiss — melancholy, not triumphant
Sound makeTapeChime() {
    int n = (int)(1.6f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x7A9EULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.02f * (wn - lp);
        float wobble = sinf(6.2831853f * 4.2f * t) * 3.5f;   // tape-wow pitch waver
        float env = (1 - expf(-t * 18.0f)) * expf(-t * 1.5f);
        float tone = sinf(6.2831853f * (220.0f + wobble) * t) * 0.5f
                   + sinf(6.2831853f * (277.2f + wobble * 1.3f) * t) * 0.32f;
        float hiss = lp * 1.8f * expf(-t * 2.2f);
        d[i] = (short)(clampf1((tone * env + hiss) * 0.9f) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// A seized wheel giving way: a rising metallic squeal that grinds in steps,
// then the flat clunk of the gate seating home.
Sound makeValveTurn() {
    int n = (int)(1.15f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x7A17ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.4f * (wn - lp);
        // the squeal climbs as the wheel turns, and stutters as it catches
        float phase = t * 9.0f; phase -= floorf(phase);
        float grind = (phase > 0.35f) ? 1.0f : 0.55f;
        float squeal = sinf(6.2831853f * (620.0f + 260.0f * t) * t) * 0.30f
                     + sinf(6.2831853f * (930.0f + 380.0f * t) * t) * 0.16f;
        squeal *= grind * expf(-t * 1.1f) * (t < 0.82f ? 1.0f : 0.0f);
        float scrape = (wn - lp) * 0.22f * expf(-t * 1.4f) * (t < 0.82f ? 1.0f : 0.0f);
        float clunk = 0.0f;
        float ct = t - 0.86f;
        if (ct > 0.0f)
            clunk = (sinf(6.2831853f * 96.0f * ct) * 1.1f + lp * 0.8f)
                  * (1.0f - expf(-ct * 900.0f)) * expf(-ct * 21.0f);
        d[i] = (short)(clampf1(tanhf((squeal + scrape + clunk) * 1.5f)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// A bark: a hard glottal burst, a shout of noise-driven formants, a snap shut.
Sound makeDogBark(uint32_t seed) {
    int n = (int)(0.42f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r((uint64_t)seed * 7919u + 13u);
    float f0 = 155.0f + r.f01() * 70.0f;      // how big the animal reads
    float lp = 0, bp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        // pitch drops sharply through the bark, the way a real one does
        float f = f0 * (1.0f - 0.42f * t / 0.42f);
        float voice = sinf(6.2831853f * f * t) * 0.55f
                    + sinf(6.2831853f * f * 2.0f * t) * 0.28f
                    + sinf(6.2831853f * f * 3.0f * t) * 0.14f;
        bp += 0.45f * (lp - bp);             // a rough vocal-tract band
        float env = (1.0f - expf(-t * 700.0f)) * expf(-t * 13.0f);
        float s = (voice * (0.75f + 0.25f * bp) + bp * 0.8f) * env;
        d[i] = (short)(clampf1(tanhf(s * 2.1f)) * 31000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// The pack calling to each other across the halls — long, and not quite a dog.
Sound makeDogHowl() {
    int n = (int)(1.9f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xD06ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.08f * (wn - lp);
        float slide = 210.0f + 95.0f * sinf(t * 1.5f) - 40.0f * t;   // wavering pitch
        float voice = sinf(6.2831853f * slide * t) * 0.5f
                    + sinf(6.2831853f * slide * 1.5f * t) * 0.22f
                    + sinf(6.2831853f * slide * 2.0f * t) * 0.12f;
        float env = (1.0f - expf(-t * 5.0f)) * expf(-t * 1.25f);
        float breath = lp * 0.5f * env;
        d[i] = (short)(clampf1(tanhf((voice * env + breath) * 1.7f)) * 29000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// Three swallows of almond water: each one a wet click opening into a short
// resonant glug, the throat tightening a little further down the can.
Sound makeGulp() {
    int n = (int)(1.55f * 44100);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xA1B0ULL);
    float lp = 0, bp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / 44100.0f;
        float wn = r.f01() * 2 - 1;
        lp += 0.22f * (wn - lp);
        float sig = 0.0f;
        for (int g = 0; g < 3; g++) {
            float gt = t - (0.40f + g * 0.36f);   // timed to the carton reaching your lips
            if (gt < 0.0f || gt > 0.40f) continue;
            // the click of the throat opening
            float click = (wn - lp) * 1.1f * expf(-gt * 150.0f);
            // then a body that drops in pitch as the swallow goes down
            float f = 168.0f - g * 22.0f - gt * 190.0f;
            float body = sinf(6.2831853f * fmaxf(46.0f, f) * gt)
                       * (1.0f - expf(-gt * 90.0f)) * expf(-gt * 12.0f) * 0.72f;
            // a little liquid rattle riding on top
            bp += 0.5f * (lp * 0.5f - bp);
            sig += (click + body + bp * 0.35f * expf(-gt * 16.0f)) * (1.0f - g * 0.16f);
        }
        // the empty can rings faintly as it comes away from your mouth
        if (t > 1.24f) {
            float et = t - 1.24f;
            sig += (sinf(6.2831853f * 1180.0f * et) * 0.055f
                  + sinf(6.2831853f * 2630.0f * et) * 0.030f) * expf(-et * 15.0f);
            sig += (wn - lp) * 0.07f * expf(-et * 40.0f);   // the tap of it
        }
        d[i] = (short)(clampf1(tanhf(sig * 1.35f)) * 26000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}
