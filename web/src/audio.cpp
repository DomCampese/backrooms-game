#include "audio.h"

// Frames generated per stream buffer. One value, because raylib is told the
// buffer size at init and then handed exactly that many frames on each refill.
static constexpr int CHUNK_FRAMES = 2048;

void AudioSynth::init() {
    SetAudioStreamBufferSizeDefault(CHUNK_FRAMES);
    stream = LoadAudioStream(SAMPLE_RATE, 16, 2);
    PlayAudioStream(stream);
}

void AudioSynth::update() {
    static short buf[CHUNK_FRAMES * 2];   // interleaved stereo
    while (IsAudioStreamProcessed(stream)) {
        bed.render(buf, CHUNK_FRAMES);
        UpdateAudioStream(stream, buf, CHUNK_FRAMES);
    }
}
