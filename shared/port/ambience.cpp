#include "../core/fp_strict.h"
#include "ambience.h"
#include <cmath>

// Everything in the loop is per sample, which is why the smoothing coefficients
// are so small: at this sample rate, 2e-5 per sample is roughly a one-second fade.
void Ambience::render(int16_t *stereo, int frames) {
    for (int i = 0; i < frames; i++) {
        hum += (mix.hum - hum) * 2e-5f;
        growl += (mix.growl - growl) * 4e-5f;
        hiss += (mix.hiss - hiss) * 6e-5f;
        whisper += (mix.whisper - whisper) * 8e-5f;
        wHum += (mix.humLevel - wHum) * 1.5e-5f;
        wDrone += (mix.droneLevel - wDrone) * 1.5e-5f;
        panel += (mix.panel - panel) * 3e-5f;
        space += (mix.space - space) * 1.2e-5f;   // rooms change slowly; a stepping tail is audible
        float wn = frand();
        lp1 += 0.035f * (wn - lp1);
        // L0: the fluorescent hum. Canon is explicit that this is "notably
        // louder and more obtrusive than ordinary fluorescent lights" and
        // that it gives you migraines that outlast the building — it should
        // be the thing players remember about Level 0 and are relieved to
        // get away from. It used to sit at 0.16 and recede politely.
        //
        // The second fundamental is detuned 0.6 Hz, so the two beat against
        // each other roughly twice a second. That is the whole trick: a
        // steady tone is something the ear files away within about a minute,
        // and one that never quite settles is not.
        float h = osc(0, 119.7f) * 0.45f + osc(1, 239.4f) * 0.20f
                + osc(2, 359.1f) * 0.09f + osc(3, 59.85f) * 0.12f;
        float beat = osc(16, 120.3f) * 0.30f + osc(17, 240.9f) * 0.13f;
        // Standing under a live fitting swells it. That is what makes the
        // buzz a function of where you are rather than a bed you stop
        // hearing, and it is why the panel grid is worth walking around.
        float swell = 0.78f + 0.55f * panel;
        float humS = (h + beat) * (0.85f + 0.15f * osc(4, 0.23f)) * 0.27f * swell * wHum;
        // L1: cavernous drone
        float droneS = (osc(8, 41.2f) * 0.6f + osc(9, 55.3f) * 0.45f)
                     * (0.55f + 0.45f * osc(10, 0.11f)) * 0.20f * wDrone
                     + lp1 * 0.14f * wDrone;
        // No Poolrooms water bed: synthesized noise read as static and a
        // recorded stream read as a running tap. The level is meant to be still.
        float room = lp1 * 0.08f;
        // LEVEL FUN's music is a looped recording (GameAudio::feedLoops).
        // ...and in a blackout it does not simply go away. `hum` ducks the
        // whole bed, and what is left underneath is a thin high ring — the
        // sound of a room that was buzzing a second ago and now isn't,
        // which is a good deal worse than silence.
        float ringLvl = (1.0f - hum) * wHum * 0.020f;
        float ring = (osc(18, 3140.0f) * 0.7f + osc(19, 4710.0f) * 0.3f) * ringLvl;
        float amb = (humS + droneS + room) * hum;   // hum var = blackout duck
        float g = osc(5, 46.0f) * 0.7f + osc(6, 33.5f) * 0.35f;
        float trem = 0.55f + 0.45f * osc(7, 2.1f);
        lp2 += 0.02f * (wn - lp2);
        float growlOut = (g * trem + lp2 * 2.2f) * growl * 0.5f;
        float hissOut = (wn - lp1) * 0.16f * hiss;      // flare burn: bright noise
        if (hiss > 0.01f) { float c = frand(); if (c > 0.998f) hissOut += c * 0.7f * hiss; }  // crackle
        // breathy half-syllables that never resolve into words
        float syl = fmaxf(0.0f, osc(13, 2.7f)) * (0.5f + 0.5f * osc(14, 0.31f));
        float whisperOut = (wn - lp1) * 0.20f * whisper * (0.25f + 0.75f * syl);
        // ---- the room the ambience is sitting in (AUD-04).
        // Only the bed and the whispers go through it: the growl and the
        // flare hiss are close-miked things happening at you, and drowning
        // them in tail costs the one cue that says how near they are.
        float wet = amb + whisperOut;
        float fb = 0.70f + 0.26f * space;     // corridor .. warehouse hall
        float combSum = comb(cb0, RV_L0, ci0, wet, fb) + comb(cb1, RV_L1, ci1, wet, fb)
                      + comb(cb2, RV_L2, ci2, wet, fb) + comb(cb3, RV_L3, ci3, wet, fb);
        combSum *= 0.25f;
        float rl = allpass(ap1, RV_A1, ai1, allpass(ap0, RV_A0, ai0, combSum));
        float rr = allpass(ap3, RV_A3, ai3, allpass(ap2, RV_A2, ai2, combSum));
        float wetMix = 0.18f + 0.42f * space;    // a big room is not just longer, it is wetter
        float dryL = wet * (1.0f - wetMix * 0.5f) + growlOut + hissOut + ring;
        float dryR = dryL;
        short vL = (short)(tanhf((dryL + rl * wetMix) * 1.3f) * 30000);
        short vR = (short)(tanhf((dryR + rr * wetMix) * 1.3f) * 30000);
        stereo[i * 2] = vL; stereo[i * 2 + 1] = vR;
    }
}
