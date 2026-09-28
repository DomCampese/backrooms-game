#include "game_audio.h"
#include "sfx.h"
#include <cmath>

void GameAudio::load() {
    for (int i = 0; i < 4; i++) steps[i] = makeFootstep(100 + i * 17);
    for (int i = 0; i < 4; i++) {
        entSteps[i] = makeFootstep(300 + i * 23);                  // heavier, its own gait
        entStepsThrough[i] = makeFootstep(300 + i * 23, true);
    }
    for (int i = 0; i < 3; i++) {
        splashIn[i]  = loadEmbeddedSound(TextFormat("sounds/water/splash_in_%d.ogg", i + 1));
        splashOut[i] = loadEmbeddedSound(TextFormat("sounds/water/splash_out_%d.ogg", i + 1));
    }
    for (int i = 0; i < 4; i++)
        swimStrokes[i] = loadEmbeddedSound(TextFormat("sounds/water/swim_%d.ogg", i + 1));
    musUnderwater = loadEmbeddedMusic("sounds/water/underwater.ogg");
    musParty = loadEmbeddedMusic("sounds/music/level_fun.ogg");
    sndClick = makeClick();
    sndScare = makeJumpscare();
    sndWin = makeWinChime();
    sndFlare = makeFlareStrike();
    sndShot = makeGunshot();
    sndPop = makeBalloonPop();
    sndHit = makeJumpscare();  SetSoundPitch(sndHit, 1.7f);  SetSoundVolume(sndHit, 0.40f);
    sndKill = makeJumpscare(); SetSoundPitch(sndKill, 0.55f); SetSoundVolume(sndKill, 0.80f);
    sndHeartbeat = makeHeartbeat(); SetSoundVolume(sndHeartbeat, 0.55f);
    sndTape = makeTapeChime();     SetSoundVolume(sndTape, 0.6f);
    sndValve = makeValveTurn();    SetSoundVolume(sndValve, 0.7f);
    sndHowl = makeDogHowl();       SetSoundVolume(sndHowl, 0.5f);
    sndGulp = makeGulp();          SetSoundVolume(sndGulp, 0.60f);
    sndVoice = makeTapeVoice();    SetSoundVolume(sndVoice, 0.9f);
    sndGroan = makeFloorGroan();   SetSoundVolume(sndGroan, 0.85f);
    for (int i = 0; i < NBARKS; i++) {
        sndBarks[i] = makeDogBark(400 + i * 31);
        sndBarksThrough[i] = makeDogBark(400 + i * 31, true);
    }
    synth.init();
}

void GameAudio::unload() {
    UnloadMusicStream(musUnderwater);
    UnloadMusicStream(musParty);
}

Sound &GameAudio::clip(Sfx sfx, int variant) {
    switch (sfx) {
    case Sfx::Step:           return steps[variant];
    case Sfx::SplashIn:       return splashIn[variant];
    case Sfx::SplashOut:      return splashOut[variant];
    case Sfx::SwimStroke:     return swimStrokes[variant];
    case Sfx::Click:          return sndClick;
    case Sfx::Scare:          return sndScare;
    case Sfx::Win:            return sndWin;
    case Sfx::FlareStrike:    return sndFlare;
    case Sfx::Shot:           return sndShot;
    case Sfx::Hit:            return sndHit;
    case Sfx::Kill:           return sndKill;
    case Sfx::Pop:            return sndPop;
    case Sfx::Heartbeat:      return sndHeartbeat;
    case Sfx::TapeChime:      return sndTape;
    case Sfx::Valve:          return sndValve;
    case Sfx::Howl:           return sndHowl;
    case Sfx::Gulp:           return sndGulp;
    case Sfx::Groan:          return sndGroan;
    case Sfx::Bark:           return sndBarks[variant];
    case Sfx::BarkThrough:    return sndBarksThrough[variant];
    case Sfx::EntStep:        return entSteps[variant];
    case Sfx::EntStepThrough: return entStepsThrough[variant];
    }
    return sndClick;
}

void GameAudio::play(const std::vector<AudioEvent> &events) {
    for (const AudioEvent &e : events) {
        switch (e.kind) {
        case AudioEvent::PLAY: {
            Sound &s = clip(e.sfx, e.variant);
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
            synth.mix = e.mix;
            synth.update();
            break;
        }
    }
}

void GameAudio::holdPaused(const AmbienceMix &mix, const LoopCue &loops) {
    synth.mix = mix;
    synth.mix.growl = synth.mix.hiss = synth.mix.whisper = 0;
    synth.update();
    feedLoops(loops, 0);
}

// Runs every tick the rules do, and every paused tick, or a stream underruns
// and stutters its last buffer.
void GameAudio::feedLoops(const LoopCue &cue, float dt) {
    float k = 1 - expf(-3.0f * dt);
    underwaterVol += ((cue.underwater ? 0.5f : 0.0f) - underwaterVol) * (1 - expf(-8.0f * dt));
    auto feed = [](Music &m, float vol) {
        if (!m.stream.buffer) return;   // failed to load: stay silent rather than crash
        if (vol > 0.005f) {
            if (!IsMusicStreamPlaying(m)) PlayMusicStream(m);
            SetMusicVolume(m, vol);
            UpdateMusicStream(m);
        } else if (IsMusicStreamPlaying(m)) StopMusicStream(m);
    };
    feed(musUnderwater, underwaterVol);
    // LEVEL FUN: the loop played slow and flat, its pitch wandering like a
    // stretched tape. It ducks with the lights in a blackout.
    partyVol += ((cue.party ? 0.32f * synth.hum : 0.0f) - partyVol) * k;
    if (musParty.stream.buffer) {
        loopT += dt;
        SetMusicPitch(musParty, 0.84f + 0.025f * sinf(loopT * 0.41f) + 0.008f * sinf(loopT * 1.9f));
    }
    feed(musParty, partyVol);
}
