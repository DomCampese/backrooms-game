#include "game_audio.h"
#include "sfx.h"
#include <string>

void GameAudio::load() {
    for (int s = 0; s < SFX_COUNT; s++) {
        Sfx sfx = (Sfx)s;
        ClipSpec spec = clipSpec(sfx);
        for (int v = 0; v < spec.variants; v++) {
            std::string recording = clipRecording(sfx, v);
            Sound &c = clips[s][v];
            c = recording.empty() ? loadPcmSound(clipPcm(sfx, v)) : loadEmbeddedSound(recording.c_str());
            if (spec.pitch != 1.0f) SetSoundPitch(c, spec.pitch);
            if (spec.volume != 1.0f) SetSoundVolume(c, spec.volume);
        }
    }
    sndVoice = loadPcmSound(tapeVoicePcm());
    SetSoundVolume(sndVoice, TAPE_VOICE_VOLUME);
    musUnderwater = loadEmbeddedMusic(UNDERWATER_RECORDING);
    musParty = loadEmbeddedMusic(PARTY_RECORDING);
    synth.init();
}

void GameAudio::unload() {
    UnloadMusicStream(musUnderwater);
    UnloadMusicStream(musParty);
}

void GameAudio::play(const std::vector<AudioEvent> &events) {
    for (const AudioEvent &e : events) {
        switch (e.kind) {
        case AudioEvent::PLAY: {
            Sound &s = clips[(int)e.sfx][e.variant];
            if (e.set & AudioEvent::SET_PITCH) SetSoundPitch(s, e.pitch);
            if (e.set & AudioEvent::SET_VOLUME) SetSoundVolume(s, e.volume);
            if (e.set & AudioEvent::SET_PAN) SetSoundPan(s, panFor(e.pan));
            PlaySound(s);
            break;
        }
        case AudioEvent::VOICE:
            // A Sound has no loop flag: restart the clip whenever it ends.
            if (!IsSoundPlaying(sndVoice)) PlaySound(sndVoice);
            SetSoundPan(sndVoice, panFor(e.pan));
            SetSoundVolume(sndVoice, e.volume);
            break;
        case AudioEvent::VOICE_STOP:
            if (IsSoundPlaying(sndVoice)) StopSound(sndVoice);
            break;
        case AudioEvent::LOOPS:
            feedLoops(e.loops, e.dt);
            break;
        case AudioEvent::AMBIENCE:
            synth.bed.mix = e.mix;
            synth.update();
            break;
        }
    }
}

void GameAudio::holdPaused(const AmbienceMix &mix, const LoopCue &loops) {
    synth.bed.mix = mix;
    synth.bed.mix.growl = synth.bed.mix.hiss = synth.bed.mix.whisper = 0;
    synth.update();
    feedLoops(loops, 0);
}

// Runs every tick the rules do, and every paused tick, or a stream underruns
// and stutters its last buffer.
void GameAudio::feedLoops(const LoopCue &cue, float dt) {
    loopLevels.step(cue, dt, synth.bed.hum);
    auto feed = [](Music &m, float vol) {
        if (!m.stream.buffer) return;   // failed to load: stay silent rather than crash
        if (vol > 0.005f) {
            if (!IsMusicStreamPlaying(m)) PlayMusicStream(m);
            SetMusicVolume(m, vol);
            UpdateMusicStream(m);
        } else if (IsMusicStreamPlaying(m)) StopMusicStream(m);
    };
    feed(musUnderwater, loopLevels.underwater);
    if (musParty.stream.buffer) SetMusicPitch(musParty, loopLevels.partyPitch);
    feed(musParty, loopLevels.party);
}
