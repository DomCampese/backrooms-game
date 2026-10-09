#pragma once
// Every Sound and Music handle and the ambience synth. The rules only emit
// AudioEvents; this plays them, once a tick, in the order they were emitted.
#include "raylib.h"
#include "audio.h"
#include "port/sounds.h"
#include "sim/audio_events.h"
#include <vector>

struct GameAudio {
    AudioSynth synth;

    void load();
    void unload();
    void play(const std::vector<AudioEvent> &events);
    // While paused the rules do not run, but the streams must still be fed or
    // they underrun and repeat their last buffer. The growl, hiss and
    // whispers fall silent.
    void holdPaused(const AmbienceMix &mix, const LoopCue &loops);

private:
    // One raylib Sound per clip, by Sfx and variant (port/sounds.h). The
    // through-a-wall sets are their own clips; the rules pick one on line of
    // sight.
    Sound clips[SFX_COUNT][SFX_MAX_VARIANTS]{};
    Sound sndVoice{};
    // Looped recordings: the muffled water while your head is under, and LEVEL
    // FUN's music. Both stream and loop; volumes are eased.
    Music musUnderwater{}, musParty{};
    LoopLevels loopLevels;

    void feedLoops(const LoopCue &cue, float dt);
};
