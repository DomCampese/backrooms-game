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
#include "camcorder.h"
#include "sounds.h"
#include <cstdint>
#include <vector>

struct RecordedPlay {
    Sfx sfx;
    int variant;
    float pitch, volume;   // the clip's, after this PLAY; volume before CENTRE_GAIN
    // Heard again off the camcorder's tape: played on a voice of its own, as
    // GameAudio::echo plays an alias, so the recording's own play goes on.
    bool echo = false;
};

struct SoundMixer {
    LoopLevels loops;
    std::vector<RecordedPlay> recorded;   // since the caller last cleared it

    // Synthesizes every clip (about 0.1 s).
    void load();
    // One tick's events, in the order the sim emitted them.
    void apply(const std::vector<AudioEvent> &events);
    // The camcorder's sounds, in the order it asked for them; clears the list.
    // An echo of a recording is left in `recorded`, marked `echo`.
    void camera(std::vector<CamNoise> &noises);
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
    Pcm camPcm[CAM_CLIPS];
    Voice camVoices[CAM_CLIPS];
    // An echo plays on a voice of its own (raylib: an alias of the clip), so
    // it leaves the clip's pitch, volume and pan as the rules set them.
    Voice echoes[SFX_COUNT][SFX_MAX_VARIANTS];
    Ambience bed;
    // The Web build fills the bed's stream only on a tick that emits
    // AMBIENCE (not on Level 2 or the title screen), and a starved stream is
    // silent. The bed sounds only after a tick that fed it.
    bool bedFed = false;
    std::vector<int16_t> bedPcm;
    std::vector<float> acc;

    static void mixVoice(Voice &v, float *out, int frames);
};
