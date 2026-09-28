#pragma once
// Every Sound and Music handle and the ambience synth. The rules only emit
// AudioEvents; this plays them, once a tick, in the order they were emitted.
#include "raylib.h"
#include "audio.h"
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
    static constexpr int NBARKS = 3;
    Sound steps[4]{}, splashIn[3]{}, splashOut[3]{}, swimStrokes[4]{};
    Sound sndClick{}, sndScare{}, sndWin{}, sndFlare{}, sndShot{}, sndHit{}, sndKill{}, sndPop{},
          sndHeartbeat{}, sndTape{}, sndValve{}, sndHowl{}, sndGulp{}, sndVoice{}, sndGroan{};
    Sound sndBarks[NBARKS]{};
    Sound entSteps[4]{};
    // The same two sets heard through a wall: low-passed and quieter
    // (sfx.h makeFootstep). The rules pick one on line of sight.
    Sound sndBarksThrough[NBARKS]{};
    Sound entStepsThrough[4]{};
    // Looped recordings: the muffled water while your head is under, and LEVEL
    // FUN's music. Both stream and loop; volumes are eased.
    Music musUnderwater{}, musParty{};
    float underwaterVol = 0, partyVol = 0, loopT = 0;

    Sound &clip(Sfx sfx, int variant);
    void feedLoops(const LoopCue &cue, float dt);
};
