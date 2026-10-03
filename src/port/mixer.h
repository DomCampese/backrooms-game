#pragma once
// The game's sound mixed in software into one stereo stream, for a port whose
// engine plays a buffer it is handed (Unreal: a procedural sound wave). It
// plays the sim's AudioEvents as GameAudio does with raylib: one voice per
// clip, which a PLAY restarts and which keeps its pitch, volume and pan between
// plays; the tape voice; the ambience bed; raylib's pan law on all of them.
//
// The recordings (the water, LEVEL FUN's music) are compressed files the
// engine imports and plays itself. Their PLAYs are left in `recorded`, and
// `loops` holds the levels the looped ones are eased to.
#include "ambience.h"
#include "sounds.h"
#include <cstdint>
#include <vector>

struct RecordedPlay {
    Sfx sfx;
    int variant;
    float pitch, volume;   // the clip's, after this PLAY; volume before CENTRE_GAIN
};

struct SoundMixer {
    LoopLevels loops;
    std::vector<RecordedPlay> recorded;   // since the caller last cleared it

    // Synthesizes every clip (about 0.1 s).
    void load();
    // One tick's events, in the order the sim emitted them.
    void apply(const std::vector<AudioEvent> &events);
    // A paused tick, after apply(): the bed goes on without the growl, hiss and
    // whispers, and the loops hold their levels.
    void holdPaused(const AmbienceMix &mix, const LoopCue &cue);
    // The next `frames` frames of interleaved stereo.
    void render(int16_t *stereo, int frames);
    // Whether a clip is still sounding, for a test or a meter.
    bool playing(Sfx sfx, int variant) const { return voices[(int)sfx][variant].playing; }

private:
    struct Voice {
        const Pcm *pcm = nullptr;
        double pos = 0;
        bool playing = false;
        float pitch = 1, volume = 1, pan = 0;
    };
    Pcm clips[SFX_COUNT][SFX_MAX_VARIANTS];
    Voice voices[SFX_COUNT][SFX_MAX_VARIANTS];
    Pcm voicePcm;
    Voice tapeVoice;
    Ambience bed;
    // The raylib build fills the bed's stream only on a tick that emits
    // AMBIENCE (not on Level 2 or the title screen), and a starved stream is
    // silent. The bed sounds only after a tick that fed it.
    bool bedFed = false;
    std::vector<int16_t> bedPcm;
    std::vector<float> acc;

    static void mixVoice(Voice &v, float *out, int frames);
};
