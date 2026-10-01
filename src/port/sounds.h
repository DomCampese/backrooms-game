#pragma once
// The game's sound, without an audio API: the one-shot clips synthesized as
// PCM, which clips are recordings instead, and the levels the looping
// recordings are eased to. The raylib build (sfx.cpp, game_audio.cpp) and a
// port's mixer (mixer.h) both load from here, so a clip sounds the same in
// either.
#include "../sim/audio_events.h"
#include <cstdint>
#include <string>
#include <vector>

// Everything synthesized is generated at this rate, the clips and the ambience.
constexpr int SAMPLE_RATE = 44100;

using Pcm = std::vector<int16_t>;   // mono, SAMPLE_RATE

constexpr int SFX_COUNT = (int)Sfx::EntStepThrough + 1;
constexpr int SFX_MAX_VARIANTS = 4;

// How many variants a PLAY of `sfx` may pick from, and the pitch and volume its
// clips are given once at load. A PLAY changes them only where it says.
struct ClipSpec {
    int variants;
    float pitch, volume;
};
ClipSpec clipSpec(Sfx sfx);
// "sounds/water/swim_1.ogg" for a recording in assets/sounds, or empty for a
// synthesized clip.
std::string clipRecording(Sfx sfx, int variant);
// The synthesized clip; empty for a recording.
Pcm clipPcm(Sfx sfx, int variant);

// The tape voice: a worn cassette, built to loop (the seam is crossfaded).
Pcm tapeVoicePcm();
constexpr float TAPE_VOICE_VOLUME = 0.9f;
// Three soft swallows; the regression checks it for clicks.
Pcm gulpPcm();

// A stereo gain pair for a bearing (-1 left, 0 centre, +1 right): raylib's
// mixer law (MixAudioFrames in raudio.c, 5.5 and 6.0), a sine approximation
// 0.5x(3 - x^2) of each side's share, applied to every stream and every clip.
struct PanGains { float left, right; };
PanGains panGains(float bearing);
constexpr float CENTRE_GAIN = 0.6875f;   // panGains(0), each side

// The looped recordings (the water while your head is under, LEVEL FUN's music)
// eased toward what the LoopCue asks for. `hum` is the ambience's blackout duck,
// which the music follows.
struct LoopLevels {
    float underwater = 0, party = 0;
    float partyPitch = 1;   // a stretched tape's wander
    float t = 0;
    void step(const LoopCue &cue, float dt, float hum);
};
constexpr const char *UNDERWATER_RECORDING = "sounds/water/underwater.ogg";
constexpr const char *PARTY_RECORDING = "sounds/music/level_fun.ogg";
