#include "sfx.h"
#include <cstring>

Sound loadPcmSound(const Pcm &pcm) {
    Wave w = {};
    w.frameCount = (unsigned)pcm.size(); w.sampleRate = SAMPLE_RATE; w.sampleSize = 16; w.channels = 1;
    w.data = (void *)pcm.data();
    return LoadSoundFromWave(w);   // copies the samples
}

// Recorded sounds embedded at build time by tools/embed-materials.py (see
// assets/sounds/*/README.md for provenance). Same record layout as the models.
struct EmbeddedAsset { const char *path; const unsigned char *data; size_t size; };
#include "sounds.generated.h"

static const EmbeddedAsset *findSound(const char *key) {
    for (const EmbeddedAsset &a : soundAssets)
        if (strcmp(a.path, key) == 0) return &a;
    TraceLog(LOG_ERROR, "SFX: embedded sound %s is missing", key);
    return nullptr;
}

Sound loadEmbeddedSound(const char *key) {
    const EmbeddedAsset *a = findSound(key);
    if (!a) return Sound{};
    Wave w = LoadWaveFromMemory(".ogg", a->data, (int)a->size);
    Sound s = LoadSoundFromWave(w); UnloadWave(w); return s;
}

// Streamed and looping: raylib's music stream wraps without the one-frame gap
// a retriggered one-shot leaves at its seam. The data is static, so the
// stream may keep pointing into it.
Music loadEmbeddedMusic(const char *key) {
    const EmbeddedAsset *a = findSound(key);
    if (!a) return Music{};
    Music m = LoadMusicStreamFromMemory(".ogg", a->data, (int)a->size);
    m.looping = true;
    return m;
}
