#pragma once
#include <cstdint>

// The one-shot sounds the rules ask for. The platform holds the clips
// (game_audio.cpp); a count is the number of variants an event may pick from.
enum class Sfx : uint8_t {
    Step,             // 4: the player's footfalls and landings
    SplashIn,         // 3
    SplashOut,        // 3
    SwimStroke,       // 4
    Click,            // switches, pickups, the gun's action, the pause
    Scare, Win, FlareStrike, Shot, Hit, Kill, Pop, Heartbeat, TapeChime, Valve, Howl, Gulp, Groan,
    Bark,             // 3: one voice per dog
    BarkThrough,      // 3: the same voices heard through a wall
    EntStep,          // 4: the hunter's footfalls
    EntStepThrough,   // 4
};

// What the ambience synth (audio.cpp) chases. The rules write each field where
// they decide it; the synth reads a copy when an AMBIENCE event plays.
struct AmbienceMix {
    float hum = 1.0f;          // ducks the whole bed: a blackout, the Manila Room
    float growl = 0.0f;
    float hiss = 0.0f;         // the loudest burning flare
    float whisper = 0.0f;
    float humLevel = 1.0f;     // this level's share of the fluorescent hum
    float droneLevel = 0.0f;   // ...and of Level 1's drone
    float panel = 0.0f;        // how directly overhead a live fitting is, 0..1
    float space = 0.25f;       // how big the room sounds: 0 corridor .. 1 hall
};

// The looping recordings, by what the player is in.
struct LoopCue {
    bool underwater = false;   // head under water in the Poolrooms
    bool party = false;        // on LEVEL FUN
};

// One thing for the platform to do with sound, in the order the rules did it.
struct AudioEvent {
    enum Kind : uint8_t {
        PLAY,         // start a one-shot
        VOICE,        // keep the tape voice running at `pan` and `volume`, restarting it if it ended
        VOICE_STOP,
        LOOPS,        // feed the looping recordings for `dt` seconds
        AMBIENCE,     // hand the synth `mix` and let it fill its buffers
    };
    // A clip keeps its pitch, volume and pan between plays, so a PLAY changes
    // only the properties flagged here.
    enum : uint8_t { SET_PITCH = 1, SET_VOLUME = 2, SET_PAN = 4 };

    Kind kind = PLAY;
    Sfx sfx = Sfx::Click;
    uint8_t variant = 0;
    uint8_t set = 0;
    float pitch = 1.0f, volume = 1.0f;
    float pan = 0.0f;          // bearing: -1 left, 0 centre, +1 right (sfx.h panFor)
    float dt = 0.0f;           // LOOPS
    LoopCue loops;             // LOOPS
    AmbienceMix mix;           // AMBIENCE

    AudioEvent &atPitch(float p) { pitch = p; set |= SET_PITCH; return *this; }
    AudioEvent &atVolume(float v) { volume = v; set |= SET_VOLUME; return *this; }
    AudioEvent &atPan(float bearing) { pan = bearing; set |= SET_PAN; return *this; }
};
