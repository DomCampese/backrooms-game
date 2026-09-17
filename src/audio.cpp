#include "audio.h"
#include "util.h"
#include <cmath>

// Frames generated per stream buffer. One value, because raylib is told the
// buffer size at init and then handed exactly that many frames on each refill.
static constexpr int CHUNK_FRAMES = 2048;

void AudioSynth::init() {
    SetAudioStreamBufferSizeDefault(CHUNK_FRAMES);
    stream = LoadAudioStream(SAMPLE_RATE, 16, 2);
    PlayAudioStream(stream);
}

// Fill however many buffers the stream has finished with. Everything in the
// loop below is per *sample*, which is why the smoothing coefficients look so
// small: at this sample rate, 2e-5 per sample is roughly a one-second fade.
void AudioSynth::update() {
    static short buf[CHUNK_FRAMES * 2];   // interleaved stereo
    while (IsAudioStreamProcessed(stream)) {
        for (int i = 0; i < CHUNK_FRAMES; i++) {
            hum += (humTarget - hum) * 2e-5f;
            growl += (growlTarget - growl) * 4e-5f;
            hiss += (hissTarget - hiss) * 6e-5f;
            whisper += (whisperTarget - whisper) * 8e-5f;
            wHum += (tHum - wHum) * 1.5e-5f;
            wDrone += (tDrone - wDrone) * 1.5e-5f;
            wWater += (tWater - wWater) * 1.5e-5f;
            wParty += (tParty - wParty) * 1.5e-5f;
            panel += (panelTarget - panel) * 3e-5f;
            space += (spaceTarget - space) * 1.2e-5f;   // rooms change slowly; a stepping tail is audible
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
            // Poolrooms: moving water
            lp3 += 0.010f * (wn - lp3);
            float waterS = (lp3 * 3.2f * (0.55f + 0.45f * osc(11, 0.16f))
                          + lp1 * 0.55f * (0.5f + 0.5f * osc(12, 0.071f))) * 0.30f * wWater;
            float room = lp1 * 0.08f;
            // LEVEL FUN: a music box grinding through the birthday song, slightly flat, forever
            float partyS = 0;
            if (wParty > 0.001f) {
                static const float MEL[25] = { 392, 392, 440, 392, 523, 494,
                                               392, 392, 440, 392, 587, 523,
                                               392, 392, 784, 659, 523, 494, 440,
                                               698, 698, 659, 523, 587, 523 };
                musT += 1.0 / SAMPLE_RATE;
                if (musT > 0.42) { musT -= 0.42; musI = (musI + 1) % 25; }
                float env = expf(-(float)musT * 4.0f);
                float note = osc(15, MEL[musI] * 0.972f);   // half a semitone flat
                partyS = (note * 0.75f + sinf(3.0f * (float)ph[15]) * 0.22f) * env * 0.085f * wParty;
            }
            // ...and in a blackout it does not simply go away. `hum` ducks the
            // whole bed, and what is left underneath is a thin high ring — the
            // sound of a room that was buzzing a second ago and now isn't,
            // which is a good deal worse than silence.
            float ringLvl = (1.0f - hum) * wHum * 0.020f;
            float ring = (osc(18, 3140.0f) * 0.7f + osc(19, 4710.0f) * 0.3f) * ringLvl;
            float amb = (humS + droneS + waterS + partyS + room) * hum;   // hum var = blackout duck
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
            float mix = 0.18f + 0.42f * space;    // a big room is not just longer, it is wetter
            float dryL = wet * (1.0f - mix * 0.5f) + growlOut + hissOut + ring;
            float dryR = dryL;
            short vL = (short)(tanhf((dryL + rl * mix) * 1.3f) * 30000);
            short vR = (short)(tanhf((dryR + rr * mix) * 1.3f) * 30000);
            buf[i * 2] = vL; buf[i * 2 + 1] = vR;
        }
        UpdateAudioStream(stream, buf, CHUNK_FRAMES);
    }
}
