#include "sfx.h"
#include "util.h"
#include <cmath>

// ---------------------------------------------------------------- sounds
static Wave makeWaveBuf(int frames) {
    Wave w = {};
    w.frameCount = (unsigned)frames; w.sampleRate = SAMPLE_RATE; w.sampleSize = 16; w.channels = 1;
    w.data = MemAlloc(frames * 2);
    return w;
}
static float clampf1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

Sound makeFootstep(uint32_t seed)
{
    const int n = (int)(0.22f * SAMPLE_RATE);

    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;

    Rng r((uint64_t)seed * 2000u + 3u);

    float thump = 52.0f + r.f01() * 8.0f;
    float knock = 92.0f + r.f01() * 12.0f;
    float toeDelay = 0.055f + r.f01() * 0.02f;

    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SAMPLE_RATE;

        // Heel impact
        float env1 = (1.0f - expf(-t * 1200.0f)) * expf(-t * 18.0f);

        float heel =
            (sinf(TAU * thump * t) * 1.45f +
             sinf(TAU * knock * t) * 0.50f)
            * env1;

        // Toe impact
        float toe = 0.0f;
        float tt = t - toeDelay;
        if (tt > 0.0f)
        {
            float env2 = (1.0f - expf(-tt * 900.0f)) * expf(-tt * 42.0f);

            toe =
                (sinf(TAU * 120.0f * tt) * 0.50f +
                 sinf(TAU * 180.0f * tt) * 0.18f)
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
    int n = (int)(1.1f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xDEADULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.35f * (wn - lp);
        float shriek = lp * expf(-t * 3.2f) * 1.6f;
        float sweep = sinf(TAU * (210 - 150 * t) * t) * expf(-t * 2.4f);
        float sub = sinf(TAU * 38 * t) * expf(-t * 1.6f);
        d[i] = (short)(clampf1(tanhf((shriek + sweep * 0.8f + sub * 1.1f) * 2.4f) * expf(-t * 1.1f)) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeSplash(uint32_t seed, bool big) {
    int n = (int)((big ? 0.55f : 0.32f) * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r((uint64_t)seed * 6151u + 11u);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
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
    int n = (int)(0.035f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xC11CULL);
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float s = (r.f01() * 2 - 1) * expf(-t * 320) * 0.5f + sinf(TAU * 2100 * t) * expf(-t * 260) * 0.35f;
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// a balloon giving up: sharp latex burst, then the rubbery flap of the skin
Sound makeBalloonPop() {
    int n = (int)(0.16f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xBA11ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.5f * (wn - lp);
        float burst = (wn * 0.7f + lp) * expf(-t * 150.0f) * 2.4f;             // the crack
        float flap = sinf(TAU * (150.0f - 320.0f * t) * t) * expf(-t * 26.0f) * 0.5f; // skin snap
        float s = tanhf((burst + flap) * 1.6f);
        d[i] = (short)(clampf1(s) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeFlareStrike() {
    int n = (int)(0.8f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xF1A2EULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        float scratch = (wn - lp) * expf(-t * 34.0f) * 1.3f;                  // striker scrape
        float pop = sinf(TAU * 150.0f * t) * expf(-t * 42.0f) * 0.8f;  // ignition pop
        float swell = (wn - lp) * (1 - expf(-t * 9.0f)) * expf(-t * 2.6f) * 0.5f; // hiss hands off to synth
        d[i] = (short)(clampf1(tanhf(scratch + pop + swell)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeGunshot() {
    int n = (int)(0.9f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x6A17ULL);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.60f * (wn - lp);
        lp2 += 0.07f * (wn - lp2);
        float crack = lp * expf(-t * 170.0f) * 2.8f;                          // supersonic crack
        float body = lp2 * expf(-t * 16.0f) * 2.4f;                           // blast body
        float thump = sinf(TAU * (72.0f - 30.0f * t) * t) * expf(-t * 8.0f) * 1.2f;
        float tail = lp2 * expf(-t * 3.2f) * 0.4f;                            // hallway slap-back
        float s = tanhf((crack + body + thump + tail) * 1.9f) * expf(-t * 0.9f);
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

Sound makeWinChime() {
    int n = (int)(1.8f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    const float freqs[3] = { 392.0f, 523.25f, 659.25f };
    const float starts[3] = { 0.0f, 0.28f, 0.56f };
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE, s = 0;
        for (int k = 0; k < 3; k++) {
            float lt = t - starts[k];
            if (lt < 0) continue;
            s += sinf(TAU * freqs[k] * lt) * (1 - expf(-lt * 30)) * expf(-lt * 2.4f) * 0.20f;
        }
        d[i] = (short)(clampf1(s) * 32000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// two low thumps, close together — held breath while it lingers right beside you
Sound makeHeartbeat() {
    int n = (int)(1.0f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    const float starts[2] = { 0.0f, 0.34f };
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE, s = 0;
        for (int k = 0; k < 2; k++) {
            float lt = t - starts[k];
            if (lt < 0) continue;
            float env = (1 - expf(-lt * 500.0f)) * expf(-lt * 14.0f);
            s += sinf(TAU * 58.0f * lt) * env * 0.85f;
        }
        d[i] = (short)(clampf1(tanhf(s * 1.3f)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// a tape found: two detuned, warbling low tones through a bed of hiss — melancholy, not triumphant
Sound makeTapeChime() {
    int n = (int)(1.6f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x7A9EULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.02f * (wn - lp);
        float wobble = sinf(TAU * 4.2f * t) * 3.5f;   // tape-wow pitch waver
        float env = (1 - expf(-t * 18.0f)) * expf(-t * 1.5f);
        float tone = sinf(TAU * (220.0f + wobble) * t) * 0.5f
                   + sinf(TAU * (277.2f + wobble * 1.3f) * t) * 0.32f;
        float hiss = lp * 1.8f * expf(-t * 2.2f);
        d[i] = (short)(clampf1((tone * env + hiss) * 0.9f) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// A seized wheel giving way: a rising metallic squeal that grinds in steps,
// then the flat clunk of the gate seating home.
Sound makeValveTurn() {
    int n = (int)(1.15f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x7A17ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.4f * (wn - lp);
        // the squeal climbs as the wheel turns, and stutters as it catches
        float phase = t * 9.0f; phase -= floorf(phase);
        float grind = (phase > 0.35f) ? 1.0f : 0.55f;
        float squeal = sinf(TAU * (620.0f + 260.0f * t) * t) * 0.30f
                     + sinf(TAU * (930.0f + 380.0f * t) * t) * 0.16f;
        squeal *= grind * expf(-t * 1.1f) * (t < 0.82f ? 1.0f : 0.0f);
        float scrape = (wn - lp) * 0.22f * expf(-t * 1.4f) * (t < 0.82f ? 1.0f : 0.0f);
        float clunk = 0.0f;
        float ct = t - 0.86f;
        if (ct > 0.0f)
            clunk = (sinf(TAU * 96.0f * ct) * 1.1f + lp * 0.8f)
                  * (1.0f - expf(-ct * 900.0f)) * expf(-ct * 21.0f);
        d[i] = (short)(clampf1(tanhf((squeal + scrape + clunk) * 1.5f)) * 30000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// A bark: a hard glottal burst, a shout of noise-driven formants, a snap shut.
Sound makeDogBark(uint32_t seed) {
    int n = (int)(0.42f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r((uint64_t)seed * 7919u + 13u);
    float f0 = 155.0f + r.f01() * 70.0f;      // how big the animal reads
    float lp = 0, bp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        // pitch drops sharply through the bark, the way a real one does
        float f = f0 * (1.0f - 0.42f * t / 0.42f);
        float voice = sinf(TAU * f * t) * 0.55f
                    + sinf(TAU * f * 2.0f * t) * 0.28f
                    + sinf(TAU * f * 3.0f * t) * 0.14f;
        bp += 0.45f * (lp - bp);             // a rough vocal-tract band
        float env = (1.0f - expf(-t * 700.0f)) * expf(-t * 13.0f);
        float s = (voice * (0.75f + 0.25f * bp) + bp * 0.8f) * env;
        d[i] = (short)(clampf1(tanhf(s * 2.1f)) * 31000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// The pack calling to each other across the halls — long, and not quite a dog.
Sound makeDogHowl() {
    int n = (int)(1.9f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xD06ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.08f * (wn - lp);
        float slide = 210.0f + 95.0f * sinf(t * 1.5f) - 40.0f * t;   // wavering pitch
        float voice = sinf(TAU * slide * t) * 0.5f
                    + sinf(TAU * slide * 1.5f * t) * 0.22f
                    + sinf(TAU * slide * 2.0f * t) * 0.12f;
        float env = (1.0f - expf(-t * 5.0f)) * expf(-t * 1.25f);
        float breath = lp * 0.5f * env;
        d[i] = (short)(clampf1(tanhf((voice * env + breath) * 1.7f)) * 29000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// Three swallows of almond water: each one a wet click opening into a short
// resonant glug, the throat tightening a little further down the can.
Sound makeGulp() {
    int n = (int)(1.55f * SAMPLE_RATE);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0xA1B0ULL);
    float lp = 0, bp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
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
            float body = sinf(TAU * fmaxf(46.0f, f) * gt)
                       * (1.0f - expf(-gt * 90.0f)) * expf(-gt * 12.0f) * 0.72f;
            // a little liquid rattle riding on top
            bp += 0.5f * (lp * 0.5f - bp);
            sig += (click + body + bp * 0.35f * expf(-gt * 16.0f)) * (1.0f - g * 0.16f);
        }
        // the empty can rings faintly as it comes away from your mouth
        if (t > 1.24f) {
            float et = t - 1.24f;
            sig += (sinf(TAU * 1180.0f * et) * 0.055f
                  + sinf(TAU * 2630.0f * et) * 0.030f) * expf(-et * 15.0f);
            sig += (wn - lp) * 0.07f * expf(-et * 40.0f);   // the tap of it
        }
        d[i] = (short)(clampf1(tanhf(sig * 1.35f)) * 26000);
    }
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// Someone else's voice, off a cassette that has been played too many times.
// A glottal pulse train through two formant resonators, syllables strung into
// phrases with breath between them, the whole thing under tape hiss, wow and
// the odd dropout. It is deliberately just short of intelligible: the words
// are not the point, the fact that there was once a person saying them is.
//
// Built to loop — the deck runs it end to end for as long as the tape lasts —
// so it opens and closes inside a breath, and the seam is crossfaded.
Sound makeTapeVoice() {
    const int SR = SAMPLE_RATE;
    const float LEN = 7.5f;
    const int n = (int)(LEN * SR);
    Wave w = makeWaveBuf(n);
    short *d = (short *)w.data;
    Rng r(0x7A9E4D02ULL);

    // ---- lay out the phrasing first: a syllable is a pitch, two formants and
    // an envelope, and speech is syllables in runs with pauses between runs.
    struct Syl { float t0, t1, f0, F1, F2, amp; };
    const int MAXSYL = 96;
    Syl syl[MAXSYL];
    int nsyl = 0;
    float cur = 0.30f;                            // opens in silence, so the loop seam lands in a breath
    while (cur < LEN - 0.70f && nsyl < MAXSYL) {
        int words = 2 + (int)(r.f01() * 4.0f);
        float base = 96.0f + r.f01() * 26.0f;     // this speaker's pitch for this phrase
        for (int i = 0; i < words && cur < LEN - 0.70f && nsyl < MAXSYL; i++) {
            float dur = 0.10f + r.f01() * 0.16f;
            // declination: a phrase falls away as it runs out of breath
            float fall = 1.0f - 0.16f * (i / (float)words);
            Syl &s = syl[nsyl++];
            s.t0 = cur; s.t1 = cur + dur;
            s.f0 = base * fall * (0.94f + r.f01() * 0.12f);
            // vowel space: F1 low/high pairs with F2, roughly as real vowels do
            s.F1 = 300.0f + r.f01() * 480.0f;
            s.F2 = 1000.0f + r.f01() * 1150.0f;
            s.amp = 0.72f + r.f01() * 0.28f;
            cur = s.t1 + 0.012f + r.f01() * 0.045f;
        }
        cur += 0.34f + r.f01() * 0.62f;           // breath between phrases
    }

    // dropouts: a worn tape loses the signal for a moment here and there
    const int NDROP = 5;
    float dropT[NDROP], dropL[NDROP];
    for (int i = 0; i < NDROP; i++) { dropT[i] = r.f01() * LEN; dropL[i] = 0.03f + r.f01() * 0.09f; }

    float *tmp = (float *)MemAlloc((unsigned)(n * sizeof(float)));
    // two 2-pole resonators (the formants), one shared lowpass for the hiss bed
    float y1a = 0, y2a = 0, y1b = 0, y2b = 0, hissLp = 0, phase = 0, dcx = 0, dcy = 0;
    int si = 0;
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        // wow and flutter: the capstan has never run true
        float wow = 1.0f + 0.013f * sinf(TAU * 2.9f * t) + 0.006f * sinf(TAU * 0.63f * t);

        while (si < nsyl && t > syl[si].t1) si++;
        float voice = 0.0f;
        if (si < nsyl && t >= syl[si].t0) {
            const Syl &s = syl[si];
            float u = (t - s.t0) / (s.t1 - s.t0);
            // envelope: quick on, slower off, so syllables run into each other
            float env = clampf1(fminf(u / 0.16f, (1.0f - u) / 0.34f)) * s.amp;

            // glottal source: a sawtooth is close enough once the formants have
            // had it, plus a little breath noise through the same filters
            phase += s.f0 * wow / SR;
            phase -= floorf(phase);
            float src = (2.0f * phase - 1.0f) * 0.75f + (r.f01() * 2.0f - 1.0f) * 0.10f;
            src *= env;

            // resonator: y = x + 2rcos(w)y1 - r^2 y2, one per formant
            const float ra = 0.976f, rb = 0.962f;
            float wa = TAU * s.F1 * wow / SR, wb = TAU * s.F2 * wow / SR;
            float ya = src + 2.0f * ra * cosf(wa) * y1a - ra * ra * y2a;
            y2a = y1a; y1a = ya;
            float yb = src + 2.0f * rb * cosf(wb) * y1b - rb * rb * y2b;
            y2b = y1b; y1b = yb;
            voice = (ya * (1.0f - ra * ra) * 1.9f + yb * (1.0f - rb * rb) * 1.1f);
        } else {
            // let the filters ring down rather than snapping to zero
            y1a *= 0.995f; y2a *= 0.995f; y1b *= 0.995f; y2b *= 0.995f;
        }

        // tape bed: hiss, plus a touch of mains hum bleeding off the heads
        float wn = r.f01() * 2.0f - 1.0f;
        hissLp += 0.30f * (wn - hissLp);
        float bed = hissLp * 0.075f + sinf(TAU * 50.0f * t) * 0.010f;

        float drop = 1.0f;
        for (int k = 0; k < NDROP; k++) {
            float dt2 = t - dropT[k];
            if (dt2 > 0 && dt2 < dropL[k]) drop *= 0.12f;
        }

        float s2 = voice * drop * 0.85f + bed;
        // block DC so the resonators can't walk the signal off centre
        dcy = s2 - dcx + 0.995f * dcy; dcx = s2;
        tmp[i] = dcy;
        float av = fabsf(dcy);
        if (av > peak) peak = av;
    }

    // crossfade the seam so looping the clip doesn't click
    const int XF = (int)(0.05f * SR);
    for (int i = 0; i < XF; i++) {
        float m = i / (float)XF;
        tmp[i] = tmp[i] * m + tmp[n - XF + i] * (1.0f - m);
    }
    float g = 0.86f / peak;
    for (int i = 0; i < n; i++) d[i] = (short)(clampf1(tmp[i] * g) * 30000);
    MemFree(tmp);

    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}
