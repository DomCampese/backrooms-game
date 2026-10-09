#include "../core/fp_strict.h"
#include "mixer.h"
#include "../core/vec.h"
#include <cmath>

void SoundMixer::load() {
    for (int s = 0; s < SFX_COUNT; s++) {
        Sfx sfx = (Sfx)s;
        ClipSpec spec = clipSpec(sfx);
        for (int v = 0; v < spec.variants; v++) {
            clips[s][v] = clipPcm(sfx, v);
            Voice &voice = voices[s][v];
            voice.pcm = &clips[s][v];
            voice.pitch = spec.pitch;
            voice.volume = spec.volume;
        }
    }
    voicePcm = tapeVoicePcm();
    tapeVoice.pcm = &voicePcm;
    tapeVoice.volume = TAPE_VOICE_VOLUME;
}

void SoundMixer::apply(const std::vector<AudioEvent> &events) {
    bedFed = false;
    for (const AudioEvent &e : events) {
        switch (e.kind) {
        case AudioEvent::PLAY: {
            Voice &v = voices[(int)e.sfx][e.variant];
            if (e.set & AudioEvent::SET_PITCH) v.pitch = e.pitch;
            if (e.set & AudioEvent::SET_VOLUME) v.volume = e.volume;
            if (e.set & AudioEvent::SET_PAN) v.pan = e.pan;
            if (v.pcm->empty()) {
                recorded.push_back({ e.sfx, e.variant, v.pitch, v.volume });
            } else {
                v.pos = 0;
                v.playing = true;
            }
            break;
        }
        case AudioEvent::VOICE:
            if (!tapeVoice.playing) { tapeVoice.pos = 0; tapeVoice.playing = true; }
            tapeVoice.pan = e.pan;
            tapeVoice.volume = e.volume;
            break;
        case AudioEvent::VOICE_STOP:
            tapeVoice.playing = false;
            break;
        case AudioEvent::LOOPS:
            loops.step(e.loops, e.dt, bed.hum);
            break;
        case AudioEvent::AMBIENCE:
            bed.mix = e.mix;
            bedFed = true;
            break;
        }
    }
}

void SoundMixer::holdPaused(const AmbienceMix &mix, const LoopCue &cue) {
    bed.mix = mix;
    bed.mix.growl = bed.mix.hiss = bed.mix.whisper = 0;
    bedFed = true;
    loops.step(cue, 0, bed.hum);
}

// Linear interpolation between samples, `pitch` source samples per output
// sample: miniaudio's resampler under raylib, without its low-pass stage.
void SoundMixer::mixVoice(Voice &v, float *out, int frames) {
    if (!v.playing) return;
    const Pcm &pcm = *v.pcm;
    const double last = (double)pcm.size() - 1;
    PanGains g = panGains(v.pan);
    float gl = g.left * v.volume / 32768.0f, gr = g.right * v.volume / 32768.0f;
    for (int i = 0; i < frames; i++) {
        if (v.pos >= last) { v.playing = false; return; }
        size_t at = (size_t)v.pos;
        float f = (float)(v.pos - (double)at);
        float s = (float)pcm[at] + ((float)pcm[at + 1] - (float)pcm[at]) * f;
        out[i * 2] += s * gl;
        out[i * 2 + 1] += s * gr;
        v.pos += v.pitch;
    }
}

void SoundMixer::render(int16_t *stereo, int frames) {
    acc.assign((size_t)frames * 2, 0.0f);
    if (bedFed) {
        bedPcm.resize((size_t)frames * 2);
        bed.render(bedPcm.data(), frames);
        for (size_t i = 0; i < acc.size(); i++) acc[i] = (float)bedPcm[i] * (CENTRE_GAIN / 32768.0f);
    }
    for (auto &variants : voices)
        for (Voice &v : variants) if (v.pcm) mixVoice(v, acc.data(), frames);
    mixVoice(tapeVoice, acc.data(), frames);
    for (size_t i = 0; i < acc.size(); i++)
        stereo[i] = (int16_t)lrintf(clampf(acc[i], -1.0f, 1.0f) * 32767.0f);
}
