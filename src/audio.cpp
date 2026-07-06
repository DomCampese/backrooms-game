#include "audio.h"
#include <cmath>

void AudioSynth::init() {
    SetAudioStreamBufferSizeDefault(2048);
    stream = LoadAudioStream(44100, 16, 2);
    PlayAudioStream(stream);
}

void AudioSynth::update() {
    static short buf[4096];
    while (IsAudioStreamProcessed(stream)) {
        for (int i = 0; i < 2048; i++) {
            hum += (humTarget - hum) * 2e-5f;
            growl += (growlTarget - growl) * 4e-5f;
            hiss += (hissTarget - hiss) * 6e-5f;
            whisper += (whisperTarget - whisper) * 8e-5f;
            wHum += (tHum - wHum) * 1.5e-5f;
            wDrone += (tDrone - wDrone) * 1.5e-5f;
            wWater += (tWater - wWater) * 1.5e-5f;
            wParty += (tParty - wParty) * 1.5e-5f;
            float wn = frand();
            lp1 += 0.035f * (wn - lp1);
            // L0: fluorescent hum
            float h = osc(0, 119.7f) * 0.45f + osc(1, 239.4f) * 0.20f + osc(2, 359.1f) * 0.09f + osc(3, 59.85f) * 0.12f;
            float humS = h * (0.85f + 0.15f * osc(4, 0.23f)) * 0.16f * wHum;
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
                musT += 1.0 / 44100.0;
                if (musT > 0.42) { musT -= 0.42; musI = (musI + 1) % 25; }
                float env = expf(-(float)musT * 4.0f);
                float note = osc(15, MEL[musI] * 0.972f);   // half a semitone flat
                partyS = (note * 0.75f + sinf(3.0f * (float)ph[15]) * 0.22f) * env * 0.085f * wParty;
            }
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
            short v = (short)(tanhf((amb + growlOut + hissOut + whisperOut) * 1.3f) * 30000);
            buf[i * 2] = v; buf[i * 2 + 1] = v;
        }
        UpdateAudioStream(stream, buf, 2048);
    }
}
