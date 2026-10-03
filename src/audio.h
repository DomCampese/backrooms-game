#pragma once
// The ambience bed (port/ambience.h) on a raylib audio stream.
#include "raylib.h"
#include "port/ambience.h"

struct AudioSynth {
    AudioStream stream;
    Ambience bed;

    void init();
    // Fill however many buffers the stream has finished with.
    void update();
};
