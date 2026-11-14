#include "../core/fp_strict.h"
#include "sounds.h"
#include "../core/hash.h"
#include "../core/level_rules.h"
#include "../core/vec.h"
#include "../sim/sim.h"
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

// Footstep on the Manila Room's wood floor: band-passed noise for the heel and
// toe, a decaying sine for the body, a one-pole low-pass, then one low-passed
// echo tap. Carpet uses makeFootstep, concrete and tile makeShoeStep.
Pcm makeWoodStep(uint32_t seed)
{
    struct Voice {
        float click;      // heel band-pass centre, Hz
        float clickQ;
        float clickDecay; // 1/s
        float body;       // body sine, Hz
        float bodyDecay;
        float bodyGain;
        float echo1, echo1Gain, echo2, echo2Gain;   // echo tap delays (s) and gains
        float gain;       // output gain; about 5 dB(A) over carpet
        float top;        // one-pole low-pass cutoff, Hz
    };
    const Voice v{ 1300.0f, 1.1f, 70.0f, 165.0f, 26.0f, 0.95f, 0.021f, 0.18f, 0.0f, 0.0f, 0.52f, 3000.0f };
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
        // A second body mode at 2.37x.
        sample += sinf(TAU * body * 2.37f * t) * 0.35f * expf(-t * 48.0f) * bodyEnv;
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

// The player hurt: a body thud (a sine falling 110 to 50 Hz, 60 ms time
// constant), a slap of noise through a 700 Hz band-pass, and a low-passed
// noise breath 25 ms later. 0.32 s. It replaced the floor groan pitched up,
// whose 1.3 s of low-passed noise sounded like rushing water.
Pcm makeHurt() {
    const int n = (int)(0.32f * SAMPLE_RATE);
    std::vector<float> d(n);
    Rng r(0x4027ULL);
    const float w = TAU * 700.0f / SAMPLE_RATE, alpha = sinf(w) / (2.0f * 0.9f), a0 = 1.0f + alpha;
    const float b0 = alpha / a0, a1 = -2.0f * cosf(w) / a0, a2 = (1.0f - alpha) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0, breath = 0, phase = 0, peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        float slap = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = slap;
        breath += 0.05f * (x - breath);
        phase += TAU * (50.0f + 60.0f * expf(-t * 30.0f)) / SAMPLE_RATE;
        float thud = sinf(phase) * expf(-t * 16.0f);
        float tb = t - 0.025f;
        float breathEnv = tb > 0.0f ? (1.0f - expf(-tb * 60.0f)) * expf(-tb * 14.0f) : 0.0f;
        const float onset = 1.0f - expf(-t * 2000.0f);
        d[i] = (thud + slap * 5.0f * expf(-t * 60.0f)) * onset + breath * 6.0f * breathEnv;
        peak = fmaxf(peak, fabsf(d[i]));
    }
    Pcm pcm(n);
    for (int i = 0; i < n; i++) {
        float fade = clampf1((float)(n - 1 - i) / (0.03f * SAMPLE_RATE));
        pcm[i] = (short)(clampf1(d[i] / peak * 0.85f * fade) * 30000.0f);
    }
    return pcm;
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

// Schroeder reverb with an RT60 of `tail` s: four combs with a one-pole
// low-pass in the feedback path, then two all-passes. Returns the wet signal,
// as long as `near`.
std::vector<float> roomTail(const std::vector<float> &near, float tail)
{
    const int n = (int)near.size();
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
    return room;
}

// Processing shared by the distant one-shots: two-pole low-pass at `cutoff`,
// a 30 Hz DC block, then roomTail with an RT60 of `tail` s, mixed `wet`. Normalised to a peak of
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
    const std::vector<float> room = roomTail(near, tail);
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

// Shoe on concrete or tile. One noise source feeds a low and a high RBJ
// band-pass (the heel's knock), a two-pole low-pass (the thud under it) and,
// 45-65 ms later, the toe through the same band-passes. roomTail adds the
// room. No tonal component: a sine body read as a drum hit.
Pcm makeShoeStep(uint32_t seed, Footing floor)
{
    struct Voice {
        float lo, hi, q;      // knock band-pass centres (Hz) and Q
        float hiGain;
        float decay;          // knock decay, 1/s
        float thudCut, thudDecay, thudGain;
        float toeGain;
        float tail, wet;      // roomTail RT60 (s) and mix
        float length;         // clip length, s; a variant replayed restarts its clip
        float level;          // peak; about 5 dB(A) over carpet
    };
    const Voice v = floor == Footing::Tile ? Voice{ 820.0f, 2500.0f, 1.2f, 0.75f, 105.0f, 210.0f, 60.0f, 0.30f,
                                                    0.55f, 1.0f, 0.10f, 0.50f, 0.84f }
                                           : Voice{ 430.0f, 1350.0f, 0.8f, 0.60f, 75.0f, 150.0f, 45.0f, 0.45f,
                                                    0.45f, 0.7f, 0.07f, 0.42f, 0.86f };
    Rng r((uint64_t)seed * 2000u + 11u);
    const float toeDelay = 0.045f + r.f01() * 0.02f;
    const float detune = 0.9f + r.f01() * 0.2f;   // per-variant frequency scale
    const int n = (int)(v.length * SAMPLE_RATE);

    struct Band {
        float b0, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        Band(float f, float q) {
            float w = TAU * f / SAMPLE_RATE, alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
            b0 = alpha / a0;
            a1 = -2.0f * cosf(w) / a0;
            a2 = (1.0f - alpha) / a0;
        }
        float operator()(float x) {   // b1 = 0, b2 = -b0
            float y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
    Band lo(v.lo * detune, v.q), hi(v.hi * detune, v.q);
    const float k = 1.0f - expf(-TAU * v.thudCut * detune / SAMPLE_RATE);
    float thudA = 0, thudB = 0;
    std::vector<float> near(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, tt = t - toeDelay;
        float x = r.f01() * 2.0f - 1.0f;
        float knock = lo(x) + hi(x) * v.hiGain;
        thudA += k * (x - thudA);
        thudB += k * (thudA - thudB);
        float heelEnv = (1.0f - expf(-t * 4000.0f)) * expf(-t * v.decay);
        float thudEnv = (1.0f - expf(-t * 1500.0f)) * expf(-t * v.thudDecay);
        float toeEnv = tt > 0.0f ? (1.0f - expf(-tt * 4000.0f)) * expf(-tt * v.decay * 1.3f) : 0.0f;
        near[i] = knock * (heelEnv + toeEnv * v.toeGain) + thudB * 8.0f * v.thudGain * thudEnv;
    }
    const std::vector<float> room = roomTail(near, v.tail);
    std::vector<float> mix(n);
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        mix[i] = near[i] * (1.0f - v.wet) + room[i] * v.wet * 2.2f;
        peak = fmaxf(peak, fabsf(mix[i]));
    }
    Pcm pcm(n);
    const float g = v.level / peak;
    for (int i = 0; i < n; i++) {
        float fade = clampf1((float)(n - 1 - i) / (0.12f * SAMPLE_RATE));   // 120 ms fade-out
        pcm[i] = (short)(clampf1(mix[i] * g * fade) * 30000.0f);
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

// Boom: a film's soundtrack through a wall. Variant 0 is one explosion (a
// 38-50 Hz drop under rumble), 1 two of them, 2 a low chord swelling up and
// cutting off.
Pcm makeFarBoom(int variant)
{
    Rng r(0xB0011ULL + (uint64_t)variant * 653u);
    std::vector<float> d((int)(2.2f * SAMPLE_RATE));
    const float f0 = 38.0f + r.f01() * 12.0f;
    const float hits[2] = { 0.05f, variant == 1 ? 0.55f + r.f01() * 0.2f : -1.0f };
    float lp = 0, lp2 = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.03f * (x - lp);
        lp2 += 0.03f * (lp - lp2);
        float s = 0;
        if (variant == 2) {
            // Root, fifth and octave, rising for 1.4 s and cut, as a scene ends.
            float env = t < 1.4f ? t / 1.4f : expf(-(t - 1.4f) * 30.0f);
            s = (sinf(TAU * 55.0f * t) + 0.6f * sinf(TAU * 82.4f * t) + 0.6f * sinf(TAU * 110.0f * t)) * env * env * 0.4f
              + (lp2 * 6.0f + lp * 1.6f) * env;
        } else {
            for (float at : hits) {
                float u = t - at;
                if (at < 0.0f || u <= 0.0f) continue;
                float env = (1.0f - expf(-u * 60.0f)) * expf(-u * 2.6f);
                s += env * (sinf(TAU * f0 * u * (1.0f - 0.12f * fminf(u, 1.0f))) * 0.6f + lp2 * 12.0f + lp * 1.5f);
            }
        }
        d[i] = s;
    }
    return farAway(d, 900.0f, 1.8f, 0.45f, 0.62f);
}

// Vend: the spiral motor (a 96 Hz buzz with three harmonics, pulsing at the
// 2.5 Hz the spiral turns) for 0.85 s, the drop (a 70 Hz thud and a noise
// burst) at 0.95 s, then 0.35 s of wrapper crinkle (sparse 2-5 kHz clicks).
Pcm makeVend()
{
    Rng r(0x5AC1ULL);
    Pcm d((int)(1.6f * SAMPLE_RATE));
    float lp = 0, hp = 0, prev = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.2f * (x - lp);
        hp = 0.7f * (hp + x - prev); prev = x;
        float s = 0;
        if (t < 0.85f) {
            float env = fminf(1.0f, t * 40.0f) * fminf(1.0f, (0.85f - t) * 40.0f);
            float pulse = 0.7f + 0.3f * sinf(TAU * 2.5f * t);
            float buzz = sinf(TAU * 96.0f * t) + 0.5f * sinf(TAU * 192.0f * t) + 0.3f * sinf(TAU * 288.0f * t)
                       + 0.2f * sinf(TAU * 384.0f * t);
            s += env * pulse * (buzz * 0.22f + lp * 0.12f);
        }
        float u = t - 0.95f;
        if (u > 0) s += (1.0f - expf(-u * 900.0f)) * expf(-u * 28.0f) * (sinf(TAU * 70.0f * u) * 0.9f + lp * 0.8f);
        float c = t - 1.08f;
        if (c > 0 && c < 0.35f && r.f01() < 0.03f) s += hp * 0.9f * (1.0f - c / 0.35f);
        d[i] = (short)(clampf1(tanhf(s * 1.4f)) * 28000);
    }
    return d;
}

// Whistle: a pea whistle, two blasts (0.22 s and 0.75 s). A 2.9 kHz tone whose
// pitch and level the pea trills at 26 Hz, over breath noise.
Pcm makeWhistle()
{
    Rng r(0x9EA1ULL);
    Pcm d((int)(1.25f * SAMPLE_RATE));
    const float blasts[2][2] = { { 0.0f, 0.22f }, { 0.36f, 1.11f } };
    float phase = 0, hp = 0, prev = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        hp = 0.85f * (hp + x - prev); prev = x;
        float env = 0;
        for (const auto &b : blasts)
            if (t > b[0] && t < b[1]) env = fminf(1.0f, (t - b[0]) * 60.0f) * fminf(1.0f, (b[1] - t) * 30.0f);
        float trill = sinf(TAU * 26.0f * t);
        phase += TAU * (2900.0f + 140.0f * trill) / SAMPLE_RATE;
        float tone = sinf(phase) + 0.15f * sinf(2.0f * phase);
        float s = env * (tone * (0.72f + 0.28f * trill) * 0.7f + hp * 0.12f);
        d[i] = (short)(clampf1(s) * 26000);
    }
    return d;
}

// A float buffer to 16-bit PCM at peak `level`, with 4 ms ramps at both ends
// so it starts and ends on zero.
Pcm toPcm(const std::vector<float> &d, float level) {
    float peak = 1e-6f;
    for (float v : d) peak = fmaxf(peak, fabsf(v));
    const int n = (int)d.size();
    Pcm pcm(n);
    for (int i = 0; i < n; i++) {
        float edge = clampf1(fminf((float)i, (float)(n - 1 - i)) / (0.004f * SAMPLE_RATE));
        pcm[i] = (short)(clampf1(d[i] / peak * level * edge) * 30000.0f);
    }
    return pcm;
}

// Gasp: a sharp breath in, then out. The inhale is noise through a resonator
// swept 1.4-2.6 kHz over 0.38 s with a 0.04 s attack; the exhale is 0.3 s
// through a 700-500 Hz resonator at half the level.
Pcm makeGasp()
{
    Rng r(0x6A5BULL);
    std::vector<float> d((int)(0.8f * SAMPLE_RATE));
    float y1 = 0, y2 = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        float f, env;
        if (t < 0.42f) {
            f = 1400.0f + 1200.0f * (t / 0.42f);
            env = fminf(1.0f, t / 0.04f) * expf(-t * 2.5f) * fminf(1.0f, (0.42f - t) * 30.0f);
        } else {
            float u = t - 0.48f;
            f = 700.0f - 200.0f * fmaxf(0.0f, u) / 0.3f;
            env = u > 0 ? 0.5f * fminf(1.0f, u / 0.03f) * expf(-u * 7.0f) : 0.0f;
        }
        // Two-pole resonator, radius 0.97: about 300 Hz wide.
        float w = TAU * f / SAMPLE_RATE, rr = 0.97f;
        float y = x * (1.0f - rr) + 2.0f * rr * cosf(w) * y1 - rr * rr * y2;
        y2 = y1; y1 = y;
        d[i] = y * env;
    }
    return toPcm(d, 0.8f);
}

// HoldBreath: a quick breath in through the mouth, then the throat closing.
// Noise through a resonator swept 900-1700 Hz over 0.22 s with a 0.02 s
// attack, stopped short by a 6 ms click low-passed at about 1 kHz. Quieter
// and shorter than the gasp, which is the same breath let out too late.
Pcm makeHoldBreath()
{
    Rng r(0xB4EAULL);
    std::vector<float> d((int)(0.34f * SAMPLE_RATE));
    float y1 = 0, y2 = 0, lp = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        float f = 900.0f + 800.0f * fminf(1.0f, t / 0.22f);
        float env = t < 0.24f ? fminf(1.0f, t / 0.02f) * (0.6f + 0.4f * t / 0.24f) * fminf(1.0f, (0.24f - t) * 60.0f) : 0.0f;
        float w = TAU * f / SAMPLE_RATE, rr = 0.965f;
        float y = x * (1.0f - rr) + 2.0f * rr * cosf(w) * y1 - rr * rr * y2;
        y2 = y1; y1 = y;
        float ct = t - 0.245f;   // the throat closing
        lp += 0.13f * (x - lp);
        float click = ct > 0 && ct < 0.006f ? lp * 2.5f * (1.0f - ct / 0.006f) : 0.0f;
        d[i] = y * env + click;
    }
    return toPcm(d, 0.6f);
}

// Choke: air escaping under water. Five to eight bubbles in 0.6 s, each a sine
// chirping up from 260-520 Hz by half again over 25-45 ms, over noise
// low-passed at about 400 Hz. Variants differ by seed.
Pcm makeChoke(int variant)
{
    Rng r(0xC40CULL + (uint64_t)variant * 977u);
    std::vector<float> d((int)(0.75f * SAMPLE_RATE));
    int n = 5 + (int)(r.f01() * 3.99f);
    for (int b = 0; b < n; b++) {
        float at = 0.02f + r.f01() * 0.55f, len = 0.025f + r.f01() * 0.02f;
        float f0 = 260.0f + r.f01() * 260.0f, amp = 0.5f + r.f01() * 0.5f, ph = 0;
        int i0 = (int)(at * SAMPLE_RATE), i1 = i0 + (int)(len * SAMPLE_RATE);
        for (int i = i0; i < i1 && i < (int)d.size(); i++) {
            float u = (float)(i - i0) / SAMPLE_RATE;
            ph += TAU * f0 * (1.0f + 0.5f * u / len) / SAMPLE_RATE;
            d[i] += sinf(ph) * amp * fminf(1.0f, u * 400.0f) * expf(-u * 90.0f);
        }
    }
    float lp = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        lp += 0.055f * ((r.f01() * 2.0f - 1.0f) - lp);
        d[i] += lp * 0.9f * fminf(1.0f, t / 0.03f) * expf(-t * 4.0f);
    }
    return toPcm(d, 0.7f);
}

// A door. Variant 0, shut: a latch click (noise and a 2.1 kHz ring, 12 ms)
// then the leaf meeting the frame, a 95 Hz thud and a 210 Hz panel mode over
// 0.18 s. Variant 1, opened: the latch, then 0.35 s of hinge creak, noise
// through a 230-300 Hz resonance swept up. Variant 2, opened by the hunter: a harder
// thud at 80 Hz with 0.25 s of broadband splintering.
Pcm makeDoorShut(int variant)
{
    Rng r(0xD0035ULL + (uint64_t)variant * 811u);
    std::vector<float> d((int)((variant == 1 ? 0.55f : 0.45f) * SAMPLE_RATE));
    float bp1 = 0, bp2 = 0, lp = 0;
    const float hitAt = variant == 1 ? 0.0f : 0.03f;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.08f * (x - lp);
        float latch = (x * 0.5f + sinf(TAU * 2100.0f * t) * 0.6f) * expf(-t * 400.0f) * 0.5f;
        float s = latch;
        if (variant == 1) {
            float u = t - 0.04f;
            if (u > 0.0f) {
                // Two-pole resonator on noise, its centre swept 230 -> 300 Hz.
                float f = 230.0f + 70.0f * fminf(u / 0.35f, 1.0f), q = 0.985f;
                float w = TAU * f / SAMPLE_RATE;
                float y = x * 0.08f + 2.0f * q * cosf(w) * bp1 - q * q * bp2;
                bp2 = bp1; bp1 = y;
                s += y * expf(-fmaxf(0.0f, u - 0.3f) * 25.0f);
            }
        } else {
            float u = t - hitAt;
            if (u > 0.0f) {
                float f0 = variant == 2 ? 80.0f : 95.0f;
                float env = (1.0f - expf(-u * 900.0f)) * expf(-u * (variant == 2 ? 14.0f : 22.0f));
                s += env * (sinf(TAU * f0 * u) * 1.2f + sinf(TAU * 210.0f * u) * 0.35f + lp * 2.5f);
                if (variant == 2) s += x * 0.9f * expf(-u * 9.0f) * (0.5f + 0.5f * sinf(TAU * 37.0f * u));
            }
        }
        d[i] = s;
    }
    return toPcm(d, variant == 2 ? 0.80f : 0.62f);
}

// The hunter knocking on a shut door: a 70-90 Hz thud, the panel's
// 180 Hz mode and the frame's rattle, 0.32 s. Each variant has its own pitch.
Pcm makeDoorKnock(int variant)
{
    Rng r(0xBA26ULL + (uint64_t)variant * 557u);
    std::vector<float> d((int)(0.32f * SAMPLE_RATE));
    const float f0 = 70.0f + variant * 9.0f + r.f01() * 4.0f;
    float lp = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.10f * (x - lp);
        float env = (1.0f - expf(-t * 700.0f)) * expf(-t * 16.0f);
        float rattle = x * 0.25f * expf(-t * 30.0f) * (0.5f + 0.5f * sinf(TAU * 23.0f * t));
        d[i] = env * (sinf(TAU * f0 * t) * 1.3f + sinf(TAU * 180.0f * t) * 0.4f + lp * 2.0f) + rattle;
    }
    return toPcm(d, 0.75f);
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

// A slot machine's beep: a square wave softened by its third harmonic only,
// as a cheap speaker plays it.
static float slotBeep(float hz, float t) {
    return sinf(TAU * hz * t) + 0.33f * sinf(3.0f * TAU * hz * t);
}

// A coin landing in a metal tray: three inharmonic partials, 2.6-5.6 kHz,
// gone in about 60 ms.
static float coinClink(float hz, float t) {
    if (t < 0) return 0;
    float env = (1.0f - expf(-t * 4000.0f)) * expf(-t * 70.0f);
    return env * (sinf(TAU * hz * t) + 0.6f * sinf(TAU * hz * 1.47f * t) + 0.35f * sinf(TAU * hz * 2.13f * t));
}

// CoinDrop: 0.6 s. A doubloon landing: four strikes of coinClink at
// 3.3 kHz (partials 1, 1.47 and 2.13 times), 0, 0.12, 0.21 and 0.27 s
// apart as its bounces shorten, each 0.55 times the last, then a 6 ms tap
// as it falls flat.
Pcm makeCoinDrop()
{
    std::vector<float> d((int)(0.6f * SAMPLE_RATE));
    const float at[4] = { 0.0f, 0.12f, 0.21f, 0.27f };
    const float hz[4] = { 3300.0f, 3320.0f, 3290.0f, 3310.0f };
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE, s = 0, lv = 1.0f;
        for (int k = 0; k < 4; k++, lv *= 0.55f) s += coinClink(hz[k], t - at[k]) * lv;
        float ft = t - 0.31f;   // falling flat: a short low tap
        if (ft > 0 && ft < 0.006f) s += 0.25f * sinf(TAU * 900.0f * ft) * (1.0f - ft / 0.006f);
        d[i] = s;
    }
    return toPcm(d, 0.7f);
}

// SlotSpin: Sim::SLOT_SPIN s. The reels tick past the window (clicks at 24 Hz) under a
// quiet looping arpeggio, and stop one at a time with a clunk at 0.85, 1.15
// and 1.45 s; each stop takes a third of the ticking away.
Pcm makeSlotSpin()
{
    Rng r(0x5107ULL);
    std::vector<float> d((int)(Sim::SLOT_SPIN * SAMPLE_RATE));
    const float stops[3] = { 0.85f, 1.15f, 1.45f };
    const float notes[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
    float hp = 0, prev = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        hp = 0.6f * (hp + x - prev); prev = x;
        int turning = 0;
        float s = 0;
        for (float st : stops) {
            if (t < st) turning++;
            float u = t - st;
            if (u > 0) s += (1.0f - expf(-u * 900.0f)) * expf(-u * 40.0f) * (sinf(TAU * 140.0f * u) * 0.8f + hp * 0.4f);
        }
        float tick = fmodf(t * 24.0f, 1.0f);
        s += hp * expf(-tick * 30.0f) * 0.35f * turning / 3.0f;
        float nt = t * 12.0f;
        float note = notes[(int)nt % 4], nu = (nt - floorf(nt)) / 12.0f;
        s += slotBeep(note, nu) * expf(-nu * 30.0f) * 0.10f * (turning > 0 ? 1.0f : 0.0f);
        d[i] = s;
    }
    return toPcm(d, 0.7f);
}

// SlotWin: 1.3 s. A rising run of six beeps an octave and a fifth wide, and
// coins dropping into the tray at about 16 a second for the first 0.9 s.
Pcm makeSlotWin()
{
    Rng r(0x5108ULL);
    std::vector<float> d((int)(1.3f * SAMPLE_RATE));
    const float run[6] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f, 1568.0f };
    float coinT[16], coinHz[16];
    for (int c = 0; c < 16; c++) { coinT[c] = c * 0.056f + r.f01() * 0.03f; coinHz[c] = 2600.0f + r.f01() * 3000.0f; }
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE, s = 0;
        for (int k = 0; k < 6; k++) {
            float u = t - k * 0.09f;
            if (u > 0) s += slotBeep(run[k], u) * (1.0f - expf(-u * 200.0f)) * expf(-u * (k == 5 ? 4.0f : 14.0f)) * 0.22f;
        }
        for (int c = 0; c < 16; c++) s += coinClink(coinHz[c], t - coinT[c]) * 0.30f;
        d[i] = s;
    }
    return toPcm(d, 0.8f);
}

// Jackpot: 3.2 s. An electric bell (partials at 1, 2.76 and 5.4 times
// 1.1 kHz) struck 18 times a second, a fanfare of beeps over it, and coins
// pouring for the whole clip.
Pcm makeJackpot()
{
    Rng r(0x5109ULL);
    std::vector<float> d((int)(3.2f * SAMPLE_RATE));
    const float fan[4] = { 783.99f, 1046.5f, 1318.5f, 1568.0f };
    const int NCOIN = 60;
    float coinT[NCOIN], coinHz[NCOIN];
    for (int c = 0; c < NCOIN; c++) { coinT[c] = r.f01() * 3.0f; coinHz[c] = 2400.0f + r.f01() * 3400.0f; }
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE, s = 0;
        float strike = fmodf(t * 18.0f, 1.0f) / 18.0f;
        float bell = sinf(TAU * 1100.0f * t) + 0.5f * sinf(TAU * 3036.0f * t) + 0.25f * sinf(TAU * 5940.0f * t);
        s += bell * (0.35f + 0.65f * expf(-strike * 60.0f)) * fminf(1.0f, (3.2f - t) * 3.0f) * 0.30f;
        float nt = t * 8.0f;
        float nu = (nt - floorf(nt)) / 8.0f;
        s += slotBeep(fan[(int)nt % 4], nu) * expf(-nu * 18.0f) * 0.18f;
        for (int c = 0; c < NCOIN; c++) s += coinClink(coinHz[c], t - coinT[c]) * 0.22f;
        d[i] = s;
    }
    return toPcm(d, 0.9f);
}

// Jingle: a slot machine paying out somewhere across the floor. Each variant
// is a short beep tune with a few coins, through farAway.
Pcm makeFarJingle(int variant)
{
    Rng r(0xF1A6ULL + (uint64_t)variant * 211u);
    static const float TUNES[3][5] = {
        { 523.25f, 659.25f, 783.99f, 1046.5f, 783.99f },
        { 659.25f, 587.33f, 659.25f, 783.99f, 1046.5f },
        { 392.0f,  523.25f, 392.0f,  523.25f, 659.25f },
    };
    std::vector<float> d((int)(1.0f * SAMPLE_RATE));
    float coinT[6], coinHz[6];
    for (int c = 0; c < 6; c++) { coinT[c] = 0.5f + r.f01() * 0.4f; coinHz[c] = 2600.0f + r.f01() * 2600.0f; }
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE, s = 0;
        int k = (int)(t / 0.11f);
        float u = t - k * 0.11f;
        if (k < 5) s += slotBeep(TUNES[variant % 3][k], u) * (1.0f - expf(-u * 300.0f)) * expf(-u * 12.0f) * 0.5f;
        for (int c = 0; c < 6; c++) s += coinClink(coinHz[c], t - coinT[c]) * 0.3f;
        d[i] = s;
    }
    return farAway(d, 1400.0f, 1.8f, 0.65f, 0.30f);
}

// Lighter: 0.9 s. The flint wheel (a dozen sharp clicks over 60 ms, a
// spark of high noise), then the flame catching as low rushing noise that
// swells and settles.
Pcm makeLighter()
{
    Rng r(0x11A7ULL);
    std::vector<float> d((int)(0.9f * SAMPLE_RATE));
    float lp = 0, hp = 0, prev = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.04f * (x - lp);
        hp = 0.5f * (hp + x - prev); prev = x;
        float s = 0;
        if (t < 0.06f) {
            float click = fmodf(t * 200.0f, 1.0f);
            s += hp * expf(-click * 12.0f) * 0.9f;
        }
        float u = t - 0.07f;
        if (u > 0) s += lp * 6.0f * (1.0f - expf(-u * 30.0f)) * (0.55f + 0.45f * expf(-u * 6.0f)) * fminf(1.0f, (0.9f - t) * 8.0f);
        d[i] = s;
    }
    return toPcm(d, 0.55f);
}

// Drag: 1.1 s, Sim::DRAG_IN. Air drawn in (band noise rising slowly) and the
// paper crackling as the ember brightens: sparse clicks, denser midway.
Pcm makeDrag()
{
    Rng r(0x11A8ULL);
    std::vector<float> d((int)(Sim::DRAG_IN * SAMPLE_RATE));
    float lp = 0, lp2 = 0, hp = 0, prev = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE, u = t / Sim::DRAG_IN;
        float x = r.f01() * 2.0f - 1.0f;
        lp += 0.25f * (x - lp); lp2 += 0.08f * (lp - lp2);
        hp = 0.6f * (hp + x - prev); prev = x;
        float breath = (lp - lp2) * sinf(u * 3.14159f) * 0.8f;
        float crackle = r.f01() < 0.004f + 0.012f * sinf(u * 3.14159f) ? hp * 0.9f : 0.0f;
        d[i] = breath + crackle;
    }
    return toPcm(d, 0.45f);
}

// Exhale: 1.2 s of breath out through the lips: band noise falling in
// pitch and level.
Pcm makeExhale()
{
    Rng r(0x11A9ULL);
    std::vector<float> d((int)(1.2f * SAMPLE_RATE));
    float lp = 0, lp2 = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = r.f01() * 2.0f - 1.0f;
        float k = 0.30f - 0.18f * (t / 1.2f);
        lp += k * (x - lp); lp2 += 0.05f * (lp - lp2);
        float env = (1.0f - expf(-t * 25.0f)) * expf(-t * 1.8f);
        d[i] = (lp - lp2) * env;
    }
    return toPcm(d, 0.5f);
}

// Cough: two or three barks. Each is a burst of noise through a 400-700 Hz
// resonance with a voiced 140-180 Hz buzz under it, sharp at the start and
// gone in about 0.15 s.
Pcm makeCough(int variant)
{
    Rng r(0xC0C0ULL + (uint64_t)variant * 97u);
    const int barks = 2 + variant % 2;
    std::vector<float> d((int)(0.25f * barks * SAMPLE_RATE + 0.2f * SAMPLE_RATE));
    float starts[3], f0[3], res[3];
    for (int b = 0; b < barks; b++) {
        starts[b] = b * (0.20f + r.f01() * 0.06f);
        f0[b] = 140.0f + r.f01() * 40.0f;
        res[b] = 400.0f + r.f01() * 300.0f;
    }
    float y1 = 0, y2 = 0;
    for (int i = 0; i < (int)d.size(); i++) {
        float t = (float)i / SAMPLE_RATE, s = 0;
        float x = r.f01() * 2.0f - 1.0f;
        for (int b = 0; b < barks; b++) {
            float u = t - starts[b];
            if (u < 0 || u > 0.25f) continue;
            float env = (1.0f - expf(-u * 400.0f)) * expf(-u * (b == 0 ? 16.0f : 20.0f));
            float rr = 0.97f, w = TAU * res[b] / SAMPLE_RATE;   // a two-pole resonator on the noise
            float y = x * 0.3f + 2.0f * rr * cosf(w) * y1 - rr * rr * y2;
            y2 = y1; y1 = y;
            s += env * (y * 0.5f + sinf(TAU * f0[b] * u) * 0.35f * expf(-u * 10.0f));
        }
        d[i] = s;
    }
    return toPcm(d, 0.75f);
}

// Shoe on grass (Level 94): a heel thud (white noise through a two-pole
// 140 Hz low-pass, 35/s decay), then the blades: noise through RBJ band-passes
// at 3.2 kHz and 1.7 kHz (Q 0.7), rising over 15 ms and decaying at 28/s,
// and the toe's rustle 50-70 ms later at 0.6. No room tail: it is outdoors.
// Peak 0.62; 0.30 s.
Pcm makeGrassStep(uint32_t seed)
{
    struct Band {
        float b0, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        Band(float f, float q) {
            float w = TAU * f / SAMPLE_RATE, alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
            b0 = alpha / a0;
            a1 = -2.0f * cosf(w) / a0;
            a2 = (1.0f - alpha) / a0;
        }
        float operator()(float x) {   // b1 = 0, b2 = -b0
            float y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
    Rng r((uint64_t)seed * 2000u + 13u);
    const float toeDelay = 0.05f + r.f01() * 0.02f, detune = 0.9f + r.f01() * 0.2f;
    const int n = (int)(0.30f * SAMPLE_RATE);
    Band hiB(3200.0f * detune, 0.7f), midB(1700.0f * detune, 0.7f);
    const float k = 1.0f - expf(-TAU * 140.0f / SAMPLE_RATE);
    float thudA = 0, thudB = 0;
    std::vector<float> d(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, tt = t - toeDelay;
        float x = r.f01() * 2.0f - 1.0f;
        thudA += k * (x - thudA);
        thudB += k * (thudA - thudB);
        float blades = hiB(x) + midB(x) * 0.5f;
        float heelEnv = (1.0f - expf(-t * 1000.0f)) * expf(-t * 35.0f);
        float rustle = (1.0f - expf(-t / 0.015f)) * expf(-t * 28.0f);
        float toe = tt > 0.0f ? 0.6f * (1.0f - expf(-tt / 0.015f)) * expf(-tt * 28.0f) : 0.0f;
        d[i] = thudB * 9.0f * heelEnv + blades * (rustle + toe);
    }
    return toPcm(d, 0.62f);
}

// The Animation's stop-motion camera: the shutter (white noise through an RBJ
// band-pass at 3.2 kHz, Q 1.2, decaying at 900/s), then the film advancing:
// three ratchet clicks 22-30 ms apart at 2.2 kHz, each at 0.45 of the one
// before. 0.14 s, peak 0.6.
Pcm makeShutter(int variant)
{
    Rng r((uint64_t)variant * 1000u + 41u);
    const int n = (int)(0.14f * SAMPLE_RATE);
    const float gap = 0.022f + 0.004f * variant;
    std::vector<float> d(n);
    auto bandOf = [](float f, float q, float &b0, float &a1, float &a2) {
        float w = TAU * f / SAMPLE_RATE, alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
        b0 = alpha / a0; a1 = -2.0f * cosf(w) / a0; a2 = (1.0f - alpha) / a0;
    };
    float sb0, sa1, sa2, rb0, ra1, ra2;
    bandOf(3200.0f, 1.2f, sb0, sa1, sa2);
    bandOf(2200.0f + 150.0f * variant, 2.0f, rb0, ra1, ra2);
    float sx1 = 0, sx2 = 0, sy1 = 0, sy2 = 0, rx1 = 0, rx2 = 0, ry1 = 0, ry2 = 0;
    for (int i = 0; i < n; i++) {
        const float t = (float)i / SAMPLE_RATE, x = r.f01() * 2.0f - 1.0f;
        float s = sb0 * x - sb0 * sx2 - sa1 * sy1 - sa2 * sy2; sx2 = sx1; sx1 = x; sy2 = sy1; sy1 = s;
        float k = rb0 * x - rb0 * rx2 - ra1 * ry1 - ra2 * ry2; rx2 = rx1; rx1 = x; ry2 = ry1; ry1 = k;
        float ratchet = 0;
        for (int c = 0; c < 3; c++) {
            const float tc = t - 0.03f - c * gap;
            if (tc > 0) ratchet += powf(0.45f, (float)c) * expf(-tc * 600.0f);
        }
        d[i] = s * expf(-t * 900.0f) * 3.0f + k * ratchet * 2.0f;
    }
    return toPcm(d, 0.6f);
}

// The Animation's laugh: five "ha"s on old film. Each is a sawtooth pulse
// train through two RBJ band-passes at 700 and 1200 Hz (an "ah"), 0.13 s with a
// 10 ms rise, 0.06 s apart. The pitch falls from 270 to 140 Hz across the
// laugh, with a 7 Hz vibrato of 6% and a 0.8 Hz wow of 4%, as film running off
// speed. A one-pole low-pass at 3.5 kHz and roomTail (0.9 s, a quarter wet)
// put it outside, somewhere. 1.3 s, peak 0.8.
Pcm makeCackle()
{
    const int n = (int)(1.3f * SAMPLE_RATE);
    Rng r(9401u);
    auto bandOf = [](float f, float q, float &b0, float &a1, float &a2) {
        float w = TAU * f / SAMPLE_RATE, alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
        b0 = alpha / a0; a1 = -2.0f * cosf(w) / a0; a2 = (1.0f - alpha) / a0;
    };
    float f1b, f1a1, f1a2, f2b, f2a1, f2a2;
    bandOf(700.0f, 2.5f, f1b, f1a1, f1a2);
    bandOf(1200.0f, 3.0f, f2b, f2a1, f2a2);
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0, phase = 0, lp = 0;
    const float soften = 1.0f - expf(-TAU * 3500.0f / SAMPLE_RATE);
    std::vector<float> near(n);
    for (int i = 0; i < n; i++) {
        const float t = (float)i / SAMPLE_RATE;
        const float f0 = (270.0f - 130.0f * fminf(t / 1.0f, 1.0f)) * (1.0f + 0.06f * sinf(TAU * 7.0f * t))
                       * (1.0f + 0.04f * sinf(TAU * 0.8f * t));
        phase += f0 / SAMPLE_RATE;
        phase -= floorf(phase);
        const float x = (2.0f * phase - 1.0f) + 0.15f * (r.f01() * 2.0f - 1.0f);
        float a = f1b * x - f1b * x2 - f1a1 * y1 - f1a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = a;
        float b = f2b * x - f2b * u2 - f2a1 * v1 - f2a2 * v2; u2 = u1; u1 = x; v2 = v1; v1 = b;
        const float st = fmodf(t, 0.19f);
        const int syl = (int)(t / 0.19f);
        float env = syl < 5 && st < 0.13f ? (1.0f - expf(-st / 0.010f)) * expf(-st * 9.0f) : 0.0f;
        env *= 1.0f - 0.1f * syl;
        lp += soften * ((a + 0.7f * b) * env - lp);
        near[i] = lp;
    }
    const std::vector<float> room = roomTail(near, 0.9f);
    std::vector<float> mix(n);
    for (int i = 0; i < n; i++) mix[i] = near[i] * 0.75f + room[i] * 0.25f * 2.2f;
    return toPcm(mix, 0.8f);
}

ClipSpec clipSpec(Sfx sfx) {
    switch (sfx) {
    case Sfx::Step: case Sfx::SwimStroke:
    case Sfx::EntStep: case Sfx::EntStepThrough:
    case Sfx::StepConcrete: case Sfx::StepTile: case Sfx::StepWood: case Sfx::StepGrass: return { 4, 1.0f, 1.0f };
    case Sfx::SplashIn: case Sfx::SplashOut:
    case Sfx::FarDoor: case Sfx::FarClang: case Sfx::FarPop:
    case Sfx::FarBoom: case Sfx::FarJingle:
    case Sfx::DoorShut: case Sfx::DoorKnock: case Sfx::Choke: case Sfx::Cough:
    case Sfx::Shutter: return { 3, 1.0f, 1.0f };
    case Sfx::Hit:       return { 1, 1.7f, 0.40f };
    case Sfx::Kill:      return { 1, 0.55f, 0.80f };
    case Sfx::Heartbeat: return { 1, 1.0f, 0.55f };
    case Sfx::TapeChime: return { 1, 1.0f, 0.6f };
    case Sfx::Gulp:      return { 1, 1.0f, 0.60f };
    case Sfx::Groan:     return { 1, 1.0f, 0.85f };
    case Sfx::Hurt:      return { 1, 1.0f, 0.8f };
    case Sfx::Vend:      return { 1, 1.0f, 0.75f };
    case Sfx::SlotSpin:  return { 1, 1.0f, 0.55f };
    case Sfx::SlotWin:   return { 1, 1.0f, 0.65f };
    case Sfx::Jackpot:   return { 1, 1.0f, 0.85f };
    case Sfx::CoinDrop:  return { 1, 1.0f, 0.7f };
    case Sfx::Lighter:   return { 1, 1.0f, 0.6f };
    case Sfx::Drag:      return { 1, 1.0f, 0.5f };
    case Sfx::Exhale:    return { 1, 1.0f, 0.5f };
    case Sfx::HoldBreath: return { 1, 1.0f, 0.6f };
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
// the player's.
Pcm clipPcm(Sfx sfx, int variant) {
    switch (sfx) {
    case Sfx::Step:           return makeFootstep(100 + variant * 17);
    case Sfx::EntStep:        return makeFootstep(300 + variant * 23);
    case Sfx::EntStepThrough: return makeFootstep(300 + variant * 23, true);
    case Sfx::StepConcrete:   return makeShoeStep(500 + variant * 19, Footing::Concrete);
    case Sfx::StepTile:       return makeShoeStep(600 + variant * 29, Footing::Tile);
    case Sfx::StepWood:       return makeWoodStep(700 + variant * 13);
    case Sfx::SplashIn: case Sfx::SplashOut: case Sfx::SwimStroke: return {};
    case Sfx::MannequinStep:  return makeWoodStep(739);
    case Sfx::Click:          return makeClick();
    case Sfx::Scare: case Sfx::Hit: case Sfx::Kill: return makeJumpscare();
    case Sfx::Win:            return makeWinChime();
    case Sfx::FlareStrike:    return makeFlareStrike();
    case Sfx::Shot:           return makeGunshot();
    case Sfx::Pop:            return makeBalloonPop();
    case Sfx::Heartbeat:      return makeHeartbeat();
    case Sfx::TapeChime:      return makeTapeChime();
    case Sfx::Gulp:           return gulpPcm();
    case Sfx::Groan:          return makeFloorGroan();
    case Sfx::Hurt:           return makeHurt();
    case Sfx::FarDoor:        return makeFarDoor(variant);
    case Sfx::FarClang:       return makeFarClang(variant);
    case Sfx::FarPop:         return makeFarPop(variant);
    case Sfx::FarBoom:        return makeFarBoom(variant);
    case Sfx::DoorShut:       return makeDoorShut(variant);
    case Sfx::DoorKnock:       return makeDoorKnock(variant);
    case Sfx::Vend:           return makeVend();
    case Sfx::Whistle:        return makeWhistle();
    case Sfx::Gasp:           return makeGasp();
    case Sfx::Choke:          return makeChoke(variant);
    case Sfx::FarJingle:      return makeFarJingle(variant);
    case Sfx::SlotSpin:       return makeSlotSpin();
    case Sfx::SlotWin:        return makeSlotWin();
    case Sfx::Jackpot:        return makeJackpot();
    case Sfx::Lighter:        return makeLighter();
    case Sfx::Drag:           return makeDrag();
    case Sfx::Exhale:         return makeExhale();
    case Sfx::Cough:          return makeCough(variant);
    case Sfx::StepGrass:      return makeGrassStep(800 + variant * 31);
    case Sfx::CoinDrop:       return makeCoinDrop();
    case Sfx::Shutter:        return makeShutter(variant);
    case Sfx::Cackle:         return makeCackle();
    case Sfx::HoldBreath:     return makeHoldBreath();
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
    // LEVEL FUN: the music plays a little flat, with a slowly wandering pitch
    // like a worn tape, and is quieter during a blackout. Slowed to 0.84 it
    // dragged; the tune should still bounce.
    // A stop (Sim::music) winds it down like a turntable losing power: pitch
    // to 0.35 of its own and level to nothing over 0.7 s. It comes back up
    // over 0.4 s.
    partyStop = fminf(1.0f, fmaxf(0.0f, partyStop + (cue.partyStopped ? dt / 0.7f : -dt / 0.4f)));
    float spin = 1.0f - partyStop;
    partyBase += ((cue.party ? 0.32f * hum : 0.0f) - partyBase) * k;
    party = partyBase * spin;
    t += dt;
    partyPitch = (0.97f + 0.012f * sinf(t * 0.41f) + 0.004f * sinf(t * 1.9f)) * (0.35f + 0.65f * spin);
}
