#include "../core/fp_strict.h"
#include "sounds.h"
#include "../core/hash.h"
#include "../core/level_rules.h"
#include "../core/vec.h"
#include <cmath>
#include <utility>

namespace {

constexpr float HALF_TURN = TAU * 0.5f;

float clampf1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

// `through` builds the same one-shot as heard through geometry: two poles of
// low-pass, a softened transient and a level drop. On Level 0 a point 20 m out
// is occluded 83% of the time, so without it a footfall through two walls
// sounded like one down an open corridor.
Pcm makeFootstep(uint32_t seed, bool through = false)
{
    const int n = (int)(0.22f * SAMPLE_RATE);
    float wallA = 0, wallB = 0;   // two poles, for the through-a-wall variant

    Pcm d(n);

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

        // a little decaying noise
        float noise =
            (r.f01() * 2.0f - 1.0f) *
            expf(-t * 120.0f) *
            0.03f;

        float sample = heel + toe + noise;

        // soft saturation
        sample = tanhf(sample * 1.45f);

        // Through a wall: two-pole low-pass, which removes the transient and
        // everything above a few hundred Hz.
        if (through) {
            wallA += 0.055f * (sample - wallA);
            wallB += 0.055f * (wallA - wallB);
            sample = wallB * 0.62f;
        }

        d[i] = (short)(clampf1(sample) * 32000.0f);
    }

    return d;
}

// Footstep on a hard floor: band-passed noise for the heel and toe, a decaying
// sine for the body, a one-pole low-pass, then one or two low-passed echo taps.
// Voice holds the per-floor parameters. Carpet uses makeFootstep instead.
Pcm makeHardStep(uint32_t seed, Footing floor)
{
    struct Voice {
        float click;      // heel band-pass centre, Hz
        float clickQ;
        float clickDecay; // 1/s
        float body;       // body sine, Hz
        float bodyDecay;
        float bodyGain;
        float echo1, echo1Gain, echo2, echo2Gain;   // echo tap delays (s) and gains
        float gain;       // output gain; sets each floor about 5 dB(A) over carpet
        float top;        // one-pole low-pass cutoff, Hz
    };
    const Voice v = floor == Footing::Tile ? Voice{ 3600.0f, 1.4f, 130.0f, 118.0f, 55.0f, 0.35f,
                                                    0.047f, 0.30f, 0.096f, 0.16f, 0.40f, 6000.0f }
                  : floor == Footing::Wood ? Voice{ 1300.0f, 1.1f, 70.0f, 165.0f, 26.0f, 0.95f,
                                                    0.021f, 0.18f, 0.0f, 0.0f, 0.52f, 3000.0f }
                                           : Voice{ 1900.0f, 0.9f, 95.0f, 74.0f, 38.0f, 0.75f,
                                                    0.038f, 0.22f, 0.0f, 0.0f, 0.48f, 4500.0f };
    Rng r((uint64_t)seed * 2000u + 7u);
    const float toeDelay = 0.055f + r.f01() * 0.02f;
    const float detune = 0.92f + r.f01() * 0.16f;   // per-variant frequency scale
    const float tail = v.echo2 > 0 ? v.echo2 : v.echo1;
    const int dry = (int)(0.16f * SAMPLE_RATE);
    const int n = dry + (int)(tail * SAMPLE_RATE) + (int)(0.02f * SAMPLE_RATE);

    // Dry signal: two RBJ band-passes over the same noise, heel and toe (toe at
    // 1.35x the heel's centre).
    std::vector<float> d(dry);
    auto band = [](float f, float q, float &b0, float &a1, float &a2) {
        float w = TAU * f / SAMPLE_RATE, alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
        b0 = alpha / a0;
        a1 = -2.0f * cosf(w) / a0;
        a2 = (1.0f - alpha) / a0;
    };
    float hb0, ha1, ha2, tb0, ta1, ta2;
    band(v.click * detune, v.clickQ, hb0, ha1, ha2);
    band(v.click * detune * 1.35f, v.clickQ, tb0, ta1, ta2);
    float hx1 = 0, hx2 = 0, hy1 = 0, hy2 = 0, tx1 = 0, tx2 = 0, ty1 = 0, ty2 = 0;
    const float body = v.body * detune;
    const float soften = 1.0f - expf(-TAU * v.top / SAMPLE_RATE);
    float sole = 0;
    for (int i = 0; i < dry; i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        float h = hb0 * x - hb0 * hx2 - ha1 * hy1 - ha2 * hy2;   // b1 = 0, b2 = -b0
        hx2 = hx1; hx1 = x; hy2 = hy1; hy1 = h;
        float y = tb0 * x - tb0 * tx2 - ta1 * ty1 - ta2 * ty2;
        tx2 = tx1; tx1 = x; ty2 = ty1; ty1 = y;
        float heelEnv = (1.0f - expf(-t * 2500.0f)) * expf(-t * v.clickDecay);
        float bodyEnv = (1.0f - expf(-t * 900.0f)) * expf(-t * v.bodyDecay);
        float sample = h * 3.2f * heelEnv + sinf(TAU * body * t) * v.bodyGain * bodyEnv;
        // Wood adds a second body mode at 2.37x.
        if (floor == Footing::Wood) sample += sinf(TAU * body * 2.37f * t) * 0.35f * expf(-t * 48.0f) * bodyEnv;
        float tt = t - toeDelay;
        if (tt > 0.0f) {
            float toeEnv = (1.0f - expf(-tt * 2500.0f)) * expf(-tt * v.clickDecay * 1.4f);
            sample += y * 1.6f * toeEnv;
        }
        sole += soften * (sample - sole);
        d[i] = sole;
    }
    // Echo taps: each a delayed, low-passed copy of the dry signal.
    std::vector<float> out(n, 0.0f);
    const struct { float delay, gain; } taps[3] = { { 0.0f, 1.0f }, { v.echo1, v.echo1Gain }, { v.echo2, v.echo2Gain } };
    for (const auto &tap : taps) {
        if (tap.gain <= 0.0f) continue;
        int at = (int)(tap.delay * SAMPLE_RATE);
        float lp = 0, k = tap.delay > 0.0f ? 0.35f : 1.0f;   // echo taps are low-passed
        for (int i = 0; i < dry && at + i < n; i++) {
            lp += k * (d[i] - lp);
            out[at + i] += lp * tap.gain;
        }
    }
    Pcm pcm(n);
    for (int i = 0; i < n; i++) {
        // 10 ms fade-out, so the clip ends on zero.
        float fade = clampf1((float)(n - 1 - i) / (0.01f * SAMPLE_RATE));
        pcm[i] = (short)(clampf1(tanhf(out[i] * v.gain * 1.3f) * fade) * 30000.0f);
    }
    return pcm;
}

Pcm makeJumpscare() {
    int n = (int)(1.1f * SAMPLE_RATE);
    Pcm d(n);
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
    return d;
}

Pcm makeClick() {
    int n = (int)(0.035f * SAMPLE_RATE);
    Pcm d(n);
    Rng r(0xC11CULL);
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float s = (r.f01() * 2 - 1) * expf(-t * 320) * 0.5f + sinf(TAU * 2100 * t) * expf(-t * 260) * 0.35f;
        d[i] = (short)(clampf1(s) * 32000);
    }
    return d;
}

// Balloon pop: a short noise burst, then a falling-pitch sine for the skin.
Pcm makeBalloonPop() {
    int n = (int)(0.16f * SAMPLE_RATE);
    Pcm d(n);
    Rng r(0xBA11ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.5f * (wn - lp);
        float burst = (wn * 0.7f + lp) * expf(-t * 150.0f) * 2.4f;             // burst
        float flap = sinf(TAU * (150.0f - 320.0f * t) * t) * expf(-t * 26.0f) * 0.5f; // skin
        float s = tanhf((burst + flap) * 1.6f);
        d[i] = (short)(clampf1(s) * 30000);
    }
    return d;
}

Pcm makeFlareStrike() {
    int n = (int)(0.8f * SAMPLE_RATE);
    Pcm d(n);
    Rng r(0xF1A2EULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        float scratch = (wn - lp) * expf(-t * 34.0f) * 1.3f;                  // striker scrape
        float pop = sinf(TAU * 150.0f * t) * expf(-t * 42.0f) * 0.8f;  // ignition pop
        float swell = (wn - lp) * (1 - expf(-t * 9.0f)) * expf(-t * 2.6f) * 0.5f; // hiss onset; the ambience synth continues it
        d[i] = (short)(clampf1(tanhf(scratch + pop + swell)) * 30000);
    }
    return d;
}

Pcm makeGunshot() {
    int n = (int)(0.9f * SAMPLE_RATE);
    Pcm d(n);
    Rng r(0x6A17ULL);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.60f * (wn - lp);
        lp2 += 0.07f * (wn - lp2);
        float crack = lp * expf(-t * 170.0f) * 2.8f;                          // crack
        float body = lp2 * expf(-t * 16.0f) * 2.4f;                           // body
        float thump = sinf(TAU * (72.0f - 30.0f * t) * t) * expf(-t * 8.0f) * 1.2f;
        float tail = lp2 * expf(-t * 3.2f) * 0.4f;                            // tail
        float s = tanhf((crack + body + thump + tail) * 1.9f) * expf(-t * 0.9f);
        d[i] = (short)(clampf1(s) * 32000);
    }
    return d;
}

Pcm makeWinChime() {
    int n = (int)(1.8f * SAMPLE_RATE);
    Pcm d(n);
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
    return d;
}

// Heartbeat: two low thumps close together.
Pcm makeHeartbeat() {
    int n = (int)(1.0f * SAMPLE_RATE);
    Pcm d(n);
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
    return d;
}

// Tape found: two detuned low tones with pitch wobble, over hiss.
Pcm makeTapeChime() {
    int n = (int)(1.6f * SAMPLE_RATE);
    Pcm d(n);
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
    return d;
}

// Valve: a rising metallic squeal, gated at 9 Hz, then a low clunk at 0.86 s.
Pcm makeValveTurn() {
    int n = (int)(1.15f * SAMPLE_RATE);
    Pcm d(n);
    Rng r(0x7A17ULL);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.4f * (wn - lp);
        // rising squeal, gated on and off
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
    return d;
}

// Processing shared by the distant one-shots: two-pole low-pass at `cutoff`,
// a 30 Hz DC block, then a Schroeder reverb (four damped combs, two
// all-passes) with an RT60 of `tail` s, mixed `wet`. Normalised to a peak of
// `level`; each kind's level puts its loudest 100 ms at 0-6 dB(A) over a
// carpet footstep. Starts and ends on zero.
Pcm farAway(const std::vector<float> &dry, float cutoff, float tail, float wet, float level)
{
    const int n = (int)dry.size() + (int)(tail * SAMPLE_RATE);
    const float k = 1.0f - expf(-TAU * cutoff / SAMPLE_RATE);
    std::vector<float> near(n, 0.0f);
    // Without the DC block the low-passed door clip had a 3% offset.
    const float hold = expf(-TAU * 30.0f / SAMPLE_RATE);
    float a = 0, b = 0, prev = 0, dc = 0;
    for (int i = 0; i < n; i++) {
        a += k * ((i < (int)dry.size() ? dry[i] : 0.0f) - a);
        b += k * (a - b);
        dc = b - prev + hold * dc;
        prev = b;
        near[i] = dc;
    }
    // Combs with a one-pole low-pass in the feedback path, then two all-passes.
    const float combs[4] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
    std::vector<float> room(n, 0.0f);
    for (float delay : combs) {
        const int len = (int)(delay * SAMPLE_RATE);
        const float g = powf(10.0f, -3.0f * delay / tail);
        std::vector<float> line(len, 0.0f);
        float damp = 0;
        for (int i = 0, at = 0; i < n; i++) {
            const float out = line[at];
            damp += 0.45f * (out - damp);
            line[at] = near[i] + damp * g;
            room[i] += out * 0.25f;
            if (++at == len) at = 0;
        }
    }
    for (float delay : { 0.0050f, 0.0017f }) {
        const int len = (int)(delay * SAMPLE_RATE);
        std::vector<float> line(len, 0.0f);
        for (int i = 0, at = 0; i < n; i++) {
            const float held = line[at], in = room[i];
            room[i] = held - 0.7f * in;
            line[at] = in + 0.7f * held;
            if (++at == len) at = 0;
        }
    }
    std::vector<float> mix(n);
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        mix[i] = near[i] * (1.0f - wet) + room[i] * wet * 2.2f;
        peak = fmaxf(peak, fabsf(mix[i]));
    }
    Pcm pcm(n);
    const float g = level / peak;
    for (int i = 0; i < n; i++) {
        float edge = clampf1(fminf((float)i, (float)(n - 1 - i)) / (0.004f * SAMPLE_RATE));
        float fade = clampf1((float)(n - 1 - i) / (0.25f * SAMPLE_RATE));   // 250 ms fade-out
        pcm[i] = (short)(clampf1(mix[i] * g * edge * fade) * 30000.0f);
    }
    return pcm;
}

// Door: a latch click, then a low thud; variants 1 and 2 add a second, quieter
// thud 100-150 ms later.
Pcm makeFarDoor(int variant)
{
    Rng r(0xD0025ULL + (uint64_t)variant * 977u);
    std::vector<float> d((int)(0.7f * SAMPLE_RATE));
    const float slamAt = 0.07f + r.f01() * 0.05f, thud = 52.0f + r.f01() * 20.0f;
    const float bounceAt = slamAt + 0.10f + r.f01() * 0.05f, bounce = variant == 0 ? 0.0f : 0.30f + r.f01() * 0.12f;
    float lp = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.06f * (x - lp);
        float latch = (x * 0.6f + sinf(TAU * 1850.0f * t) * 0.5f) * expf(-t * 380.0f) * 0.35f;
        float s = latch;
        for (const auto &hit : { std::pair<float, float>{ slamAt, 1.0f }, { bounceAt, bounce } }) {
            float u = t - hit.first;
            if (u <= 0.0f || hit.second <= 0.0f) continue;
            float env = (1.0f - expf(-u * 1400.0f)) * expf(-u * 22.0f);
            s += hit.second * env * (sinf(TAU * thud * u) * 1.1f + lp * 3.0f + sinf(TAU * thud * 2.6f * u) * 0.25f);
        }
        d[i] = s;
    }
    return farAway(d, 650.0f, 1.5f, 0.55f, 0.60f);
}

// Clang: four inharmonic bar modes (1, 2.76, 5.40, 8.93) at 190-310 Hz;
// variant 1 adds one bounce, variant 2 two.
Pcm makeFarClang(int variant)
{
    Rng r(0xC1A26ULL + (uint64_t)variant * 1201u);
    std::vector<float> d((int)(2.0f * SAMPLE_RATE));
    const float f0 = 190.0f + r.f01() * 120.0f;
    const float ratio[4] = { 1.0f, 2.76f, 5.40f, 8.93f }, amp[4] = { 1.0f, 0.6f, 0.35f, 0.2f },
                decay[4] = { 3.5f, 5.0f, 8.0f, 12.0f };
    struct Strike { float at, gain; };
    const Strike strikes[3] = { { 0.0f, 1.0f },
                                { 0.20f + r.f01() * 0.08f, variant == 0 ? 0.0f : 0.45f },
                                { 0.36f + r.f01() * 0.06f, variant == 2 ? 0.2f : 0.0f } };
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f, s = 0;
        for (const Strike &k : strikes) {
            float u = t - k.at;
            if (u <= 0.0f || k.gain <= 0.0f) continue;
            float ring = 0;
            for (int m = 0; m < 4; m++) ring += sinf(TAU * f0 * ratio[m] * u) * amp[m] * expf(-u * decay[m]);
            s += k.gain * (ring * (1.0f - expf(-u * 3000.0f)) + x * 0.8f * expf(-u * 250.0f));
        }
        d[i] = s;
    }
    return farAway(d, 1500.0f, 2.6f, 0.7f, 0.22f);
}

// Knock: 3-5 knocks (3 + variant), each spacing 0.84x and gain 0.72x the
// last; a 95-125 Hz thump plus a 600-740 Hz ring.
Pcm makeFarKnock(int variant)
{
    Rng r(0x7A3E1ULL + (uint64_t)variant * 733u);
    const int knocks = 3 + variant;
    std::vector<float> d((int)(1.3f * SAMPLE_RATE));
    float at[6], gain[6], gap = 0.17f + r.f01() * 0.05f, t0 = 0.0f, g0 = 1.0f;
    for (int k = 0; k < knocks; k++) {
        at[k] = t0; gain[k] = g0 * (0.85f + r.f01() * 0.3f);
        t0 += gap; gap *= 0.84f; g0 *= 0.72f;
    }
    const float thump = 95.0f + r.f01() * 30.0f, ring = 600.0f + r.f01() * 140.0f;
    float lp = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.12f * (x - lp);
        float s = 0;
        for (int k = 0; k < knocks; k++) {
            float u = t - at[k];
            if (u <= 0.0f) continue;
            float hit = (1.0f - expf(-u * 2000.0f)) * expf(-u * 45.0f);
            s += gain[k] * (hit * (sinf(TAU * thump * u) + lp * 2.0f) + sinf(TAU * ring * u) * 0.25f * expf(-u * 22.0f));
        }
        d[i] = s;
    }
    return farAway(d, 900.0f, 1.2f, 0.5f, 0.36f);
}

// Pop: the makeBalloonPop burst, run through farAway with a high wet mix.
Pcm makeFarPop(int variant)
{
    Rng r(0xFA2B0ULL + (uint64_t)variant * 409u);
    std::vector<float> d((int)(0.18f * SAMPLE_RATE));
    const float flapHz = 130.0f + r.f01() * 50.0f;
    float lp = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.5f * (x - lp);
        d[i] = (x * 0.7f + lp) * expf(-t * 150.0f) * 2.4f + sinf(TAU * (flapHz - 320.0f * t) * t) * expf(-t * 26.0f) * 0.5f;
    }
    return farAway(d, 1200.0f, 1.4f, 0.7f, 0.32f);
}

// Bark: a falling-pitch harmonic voice through a noise band, with a fast attack
// and decay.
Pcm makeDogBark(uint32_t seed, bool through = false) {
    int n = (int)(0.42f * SAMPLE_RATE);
    float wallA = 0, wallB = 0;
    Pcm d(n);
    Rng r((uint64_t)seed * 7919u + 13u);
    float f0 = 155.0f + r.f01() * 70.0f;      // base pitch per variant
    float lp = 0, bp = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.30f * (wn - lp);
        // pitch falls 42% over the bark
        float f = f0 * (1.0f - 0.42f * t / 0.42f);
        float voice = sinf(TAU * f * t) * 0.55f
                    + sinf(TAU * f * 2.0f * t) * 0.28f
                    + sinf(TAU * f * 3.0f * t) * 0.14f;
        bp += 0.45f * (lp - bp);             // noise band
        float env = (1.0f - expf(-t * 700.0f)) * expf(-t * 13.0f);
        float s = (voice * (0.75f + 0.25f * bp) + bp * 0.8f) * env;
        if (through) {   // through a wall: two-pole low-pass
            wallA += 0.048f * (s - wallA);
            wallB += 0.048f * (wallA - wallB);
            s = wallB * 0.70f;
        }
        d[i] = (short)(clampf1(tanhf(s * 2.1f)) * 31000);
    }
    return d;
}

// Howl: a long tone with a wavering, slowly falling pitch.
Pcm makeDogHowl() {
    int n = (int)(1.9f * SAMPLE_RATE);
    Pcm d(n);
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
    return d;
}

// Floor groan (rotten patch): a low resonant groan with a stick-slip stutter
// over it and no final impact, so it does not sound finished.
Pcm makeFloorGroan() {
    int n = (int)(1.30f * SAMPLE_RATE);
    Pcm d(n);
    Rng r(0x50F7ULL);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SAMPLE_RATE;
        float wn = r.f01() * 2 - 1;
        lp += 0.08f * (wn - lp);        // low-passed noise body
        lp2 += 0.35f * (wn - lp2);
        // stick-slip gating; the rate rises over the clip so it does not sound
        // like a loop
        float ph = t * (5.0f + 6.0f * t); ph -= floorf(ph);
        float slip = ph < 0.22f ? 1.0f : 0.25f;
        float body = (sinf(TAU * (74.0f - 12.0f * t) * t) * 0.55f
                    + sinf(TAU * (113.0f - 18.0f * t) * t) * 0.25f) * slip;
        float fibre = (lp2 - lp) * 0.30f * slip;          // high band noise
        float env = (1.0f - expf(-t * 12.0f)) * (t < 1.12f ? 1.0f : expf(-(t - 1.12f) * 14.0f));
        d[i] = (short)(clampf1(tanhf((body + lp * 1.4f + fibre) * 1.25f)) * env * 26000);
    }
    return d;
}

} // namespace

// Soft liquid movement and three short swallows, synchronized with heldCan (port/held.h).
// Smooth attacks avoid the old full-amplitude noise discontinuity; integrating
// frequency keeps the bubble pitch from reversing into an electronic bass chirp.
Pcm gulpPcm() {
    const int n=(int)(1.55f*SAMPLE_RATE);
    Pcm d(n);
    Rng r(0xA1B0ULL);
    float low=0, smooth=0, phase[3]={}, bubble[3]={};
    for (int i=0;i<n;++i) {
        float t=i/(float)SAMPLE_RATE;
        float noise=r.f01()*2-1;
        low+=0.045f*(noise-low);
        smooth+=0.012f*(low-smooth);
        float sig=0;
        for (int g=0;g<3;++g) {
            float gt=t-(0.40f+g*0.36f);
            if (gt<0 || gt>0.26f) continue;
            float env=sinf(HALF_TURN*gt/0.26f);
            env*=env;
            float freq=310.0f+g*37.0f-90.0f*gt/0.26f;
            phase[g]+=TAU*freq/SAMPLE_RATE;
            bubble[g]+=TAU*(610.0f+70*sinf(gt*31+g))/SAMPLE_RATE;
            // Mostly filtered liquid noise, with restrained, irregular bubbles.
            float wet=(low-smooth)*0.48f+smooth*0.22f;
            float throat=sinf(phase[g])*0.035f*(0.65f+0.35f*sinf(gt*47));
            float bubbles=sinf(bubble[g])*0.025f*expf(-gt*18);
            sig+=(wet+throat+bubbles)*env*(1-g*0.12f);
        }
        d[i]=(short)(clampf1(sig)*26000);
    }
    return d;
}

// The tape voice: a pulse train through two formant resonators, in syllables
// and phrases with pauses, under tape hiss, pitch wobble and dropouts. Not
// intelligible on purpose.
//
// The deck loops it for the length of a tape, so it starts and ends in a pause
// and the seam is crossfaded.
Pcm tapeVoicePcm() {
    const int SR = SAMPLE_RATE;
    const float LEN = 7.5f;
    const int n = (int)(LEN * SR);
    Pcm d(n);
    Rng r(0x7A9E4D02ULL);

    // ---- phrasing first: a syllable is a pitch, two formants and an envelope;
    // phrases are runs of syllables with pauses between them.
    struct Syl { float t0, t1, f0, F1, F2, amp; };
    const int MAXSYL = 96;
    Syl syl[MAXSYL];
    int nsyl = 0;
    float cur = 0.30f;                            // starts in silence, so the loop seam is in a pause
    while (cur < LEN - 0.70f && nsyl < MAXSYL) {
        int words = 2 + (int)(r.f01() * 4.0f);
        float base = 96.0f + r.f01() * 26.0f;     // this phrase's base pitch
        for (int i = 0; i < words && cur < LEN - 0.70f && nsyl < MAXSYL; i++) {
            float dur = 0.10f + r.f01() * 0.16f;
            // pitch falls over each phrase
            float fall = 1.0f - 0.16f * (i / (float)words);
            Syl &s = syl[nsyl++];
            s.t0 = cur; s.t1 = cur + dur;
            s.f0 = base * fall * (0.94f + r.f01() * 0.12f);
            // F1/F2 pairs, roughly matching real vowels
            s.F1 = 300.0f + r.f01() * 480.0f;
            s.F2 = 1000.0f + r.f01() * 1150.0f;
            s.amp = 0.72f + r.f01() * 0.28f;
            cur = s.t1 + 0.012f + r.f01() * 0.045f;
        }
        cur += 0.34f + r.f01() * 0.62f;           // pause between phrases
    }

    // dropouts: short gaps in the signal
    const int NDROP = 5;
    float dropT[NDROP], dropL[NDROP];
    for (int i = 0; i < NDROP; i++) { dropT[i] = r.f01() * LEN; dropL[i] = 0.03f + r.f01() * 0.09f; }

    std::vector<float> tmp(n);
    // two 2-pole resonators (the formants), one shared lowpass for the hiss bed
    float y1a = 0, y2a = 0, y1b = 0, y2b = 0, hissLp = 0, phase = 0, dcx = 0, dcy = 0;
    int si = 0;
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        // wow and flutter
        float wow = 1.0f + 0.013f * sinf(TAU * 2.9f * t) + 0.006f * sinf(TAU * 0.63f * t);

        while (si < nsyl && t > syl[si].t1) si++;
        float voice = 0.0f;
        if (si < nsyl && t >= syl[si].t0) {
            const Syl &s = syl[si];
            float u = (t - s.t0) / (s.t1 - s.t0);
            // envelope: quick on, slower off, so syllables run into each other
            float env = clampf1(fminf(u / 0.16f, (1.0f - u) / 0.34f)) * s.amp;

            // source: a sawtooth plus a little noise, through the formant filters
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
            // let the filters decay rather than cutting to zero
            y1a *= 0.995f; y2a *= 0.995f; y1b *= 0.995f; y2b *= 0.995f;
        }

        // tape hiss plus a little mains hum
        float wn = r.f01() * 2.0f - 1.0f;
        hissLp += 0.30f * (wn - hissLp);
        float bed = hissLp * 0.075f + sinf(TAU * 50.0f * t) * 0.010f;

        float drop = 1.0f;
        for (int k = 0; k < NDROP; k++) {
            float dt2 = t - dropT[k];
            if (dt2 > 0 && dt2 < dropL[k]) drop *= 0.12f;
        }

        float s2 = voice * drop * 0.85f + bed;
        // DC block, so the resonators do not drift off centre
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
    return d;
}

ClipSpec clipSpec(Sfx sfx) {
    switch (sfx) {
    case Sfx::Step: case Sfx::SwimStroke:
    case Sfx::EntStep: case Sfx::EntStepThrough:
    case Sfx::StepConcrete: case Sfx::StepTile: case Sfx::StepWood: return { 4, 1.0f, 1.0f };
    case Sfx::SplashIn: case Sfx::SplashOut:
    case Sfx::Bark: case Sfx::BarkThrough:
    case Sfx::FarDoor: case Sfx::FarClang: case Sfx::FarKnock: case Sfx::FarPop: return { 3, 1.0f, 1.0f };
    case Sfx::Hit:       return { 1, 1.7f, 0.40f };
    case Sfx::Kill:      return { 1, 0.55f, 0.80f };
    case Sfx::Heartbeat: return { 1, 1.0f, 0.55f };
    case Sfx::TapeChime: return { 1, 1.0f, 0.6f };
    case Sfx::Valve:     return { 1, 1.0f, 0.7f };
    case Sfx::Howl:      return { 1, 1.0f, 0.5f };
    case Sfx::Gulp:      return { 1, 1.0f, 0.60f };
    case Sfx::Groan:     return { 1, 1.0f, 0.85f };
    default:             return { 1, 1.0f, 1.0f };
    }
}

std::string clipRecording(Sfx sfx, int variant) {
    const char *stem = sfx == Sfx::SplashIn ? "sounds/water/splash_in_"
                     : sfx == Sfx::SplashOut ? "sounds/water/splash_out_"
                     : sfx == Sfx::SwimStroke ? "sounds/water/swim_" : nullptr;
    if (!stem) return {};
    return stem + std::to_string(variant + 1) + ".ogg";
}

// Each variant has its own seed. The hunter's steps use different seeds from
// the player's, and each dog has its own bark.
Pcm clipPcm(Sfx sfx, int variant) {
    switch (sfx) {
    case Sfx::Step:           return makeFootstep(100 + variant * 17);
    case Sfx::EntStep:        return makeFootstep(300 + variant * 23);
    case Sfx::EntStepThrough: return makeFootstep(300 + variant * 23, true);
    case Sfx::StepConcrete:   return makeHardStep(500 + variant * 19, Footing::Concrete);
    case Sfx::StepTile:       return makeHardStep(600 + variant * 29, Footing::Tile);
    case Sfx::StepWood:       return makeHardStep(700 + variant * 13, Footing::Wood);
    case Sfx::Bark:           return makeDogBark(400 + variant * 31);
    case Sfx::BarkThrough:    return makeDogBark(400 + variant * 31, true);
    case Sfx::SplashIn: case Sfx::SplashOut: case Sfx::SwimStroke: return {};
    case Sfx::Click:          return makeClick();
    case Sfx::Scare: case Sfx::Hit: case Sfx::Kill: return makeJumpscare();
    case Sfx::Win:            return makeWinChime();
    case Sfx::FlareStrike:    return makeFlareStrike();
    case Sfx::Shot:           return makeGunshot();
    case Sfx::Pop:            return makeBalloonPop();
    case Sfx::Heartbeat:      return makeHeartbeat();
    case Sfx::TapeChime:      return makeTapeChime();
    case Sfx::Valve:          return makeValveTurn();
    case Sfx::Howl:           return makeDogHowl();
    case Sfx::Gulp:           return gulpPcm();
    case Sfx::Groan:          return makeFloorGroan();
    case Sfx::FarDoor:        return makeFarDoor(variant);
    case Sfx::FarClang:       return makeFarClang(variant);
    case Sfx::FarKnock:       return makeFarKnock(variant);
    case Sfx::FarPop:         return makeFarPop(variant);
    }
    return {};
}

PanGains panGains(float bearing) {
    float right = clampf((bearing + 1.0f) * 0.5f, 0.0f, 1.0f), left = 1.0f - right;
    return { 0.5f * left * (3.0f - left * left), 0.5f * right * (3.0f - right * right) };
}

void LoopLevels::step(const LoopCue &cue, float dt, float hum) {
    float k = 1 - expf(-3.0f * dt);
    underwater += ((cue.underwater ? 0.5f : 0.0f) - underwater) * (1 - expf(-8.0f * dt));
    // LEVEL FUN: the music plays slowed and flat, with a slowly wandering
    // pitch, and is quieter during a blackout.
    party += ((cue.party ? 0.32f * hum : 0.0f) - party) * k;
    t += dt;
    partyPitch = 0.84f + 0.025f * sinf(t * 0.41f) + 0.008f * sinf(t * 1.9f);
}
