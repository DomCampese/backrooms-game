#include "camcorder.h"
#include "game_audio.h"
#include "sfx.h"
#include "shaders.h"
#include "rlgl.h"

void CamcorderReel::load() {
    shader = LoadShaderFromMemory(NULL, TAPE_FS);
    if (shader.id == rlGetShaderIdDefault())
        TraceLog(LOG_ERROR, "tape shader failed to compile - see the SHADER lines above");
    locTime = GetShaderLocation(shader, "uTime");
    locRes = GetShaderLocation(shader, "uRes");
    locCellA = GetShaderLocation(shader, "uCellA");
    locCellB = GetShaderLocation(shader, "uCellB");
    locMix = GetShaderLocation(shader, "uMix");
    locNight = GetShaderLocation(shader, "uNight");
    locTrack = GetShaderLocation(shader, "uTrack");
    locStill = GetShaderLocation(shader, "uStill");
    locSnow = GetShaderLocation(shader, "uSnow");
    sndRaise = loadPcmSound(camcorderClip(CamSound::Raise));
    sndLower = loadPcmSound(camcorderClip(CamSound::Lower));
    sndSwitch = loadPcmSound(camcorderClip(CamSound::Switch));
    sndZoom = loadPcmSound(camcorderClip(CamSound::Zoom));
    sndTransport = loadPcmSound(camcorderClip(CamSound::Transport));
    sndHiss = loadPcmSound(camcorderClip(CamSound::Hiss));
    SetSoundVolume(sndHiss, Camcorder::HISS_VOLUME);
}

void CamcorderReel::unload() {
    if (reel.id) UnloadRenderTexture(reel);
    reel = {};
}

void CamcorderReel::fit(const Camcorder &cam) {
    unload();
    reel = LoadRenderTexture(cam.reelW(), cam.reelH());
    if (reel.id) SetTextureFilter(reel.texture, TEXTURE_FILTER_BILINEAR);
}

void CamcorderReel::grab(const Camcorder &cam, int slot, const RenderTexture2D &rt) {
    float x = (float)cam.cellX(slot), y = (float)cam.cellY(slot);
    int w = cam.cellWidth(), h = cam.cellHeight();
    BeginTextureMode(reel);
    // Copied, not blended: the scene target's alpha is below 1 wherever
    // something translucent was drawn (decals, the contact shadows, water,
    // glass), and the screen shows its colour regardless. Blended over black
    // the tape came out darker there than the eye saw it. GL_ONE, GL_ZERO,
    // GL_FUNC_ADD: rlgl's names for them are #defines the sandbox's headers lose.
    rlSetBlendFactors(1, 0, 0x8006);
    BeginBlendMode(BLEND_CUSTOM);
    DrawTexturePro(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height },
                   { x, y, (float)w, (float)h }, { 0, 0 }, 0, WHITE);
    EndBlendMode();
    EndTextureMode();
}

void CamcorderReel::playNoises(Camcorder &cam, GameAudio &audio) {
    for (const CamNoise &n : cam.noises) {
        switch (n.sound) {
        case CamSound::Raise: PlaySound(sndRaise); break;
        case CamSound::Lower: PlaySound(sndLower); break;
        case CamSound::Switch: PlaySound(sndSwitch); break;
        case CamSound::Zoom: if (!IsSoundPlaying(sndZoom)) PlaySound(sndZoom); break;
        case CamSound::Transport: PlaySound(sndTransport); break;
        case CamSound::Hiss: if (!IsSoundPlaying(sndHiss)) PlaySound(sndHiss); break;
        case CamSound::HissStop: StopSound(sndHiss); break;
        case CamSound::Echo: audio.echo(n.echo, n.pitch, n.volume); break;
        }
    }
    cam.noises.clear();
}

void CamcorderReel::present(const CamPicture &pic, const RenderTexture2D &scene, float time) {
    Texture2D tex = pic.fromReel ? reel.texture : scene.texture;
    Vector4 cellA = { pic.a.u0, pic.a.v0, pic.a.u1, pic.a.v1 };
    Vector4 cellB = { pic.b.u0, pic.b.v0, pic.b.u1, pic.b.v1 };
    Vector2 res = { (float)GetRenderWidth(), (float)GetRenderHeight() };
    SetShaderValue(shader, locTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locRes, &res, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, locCellA, &cellA, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locCellB, &cellB, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locMix, &pic.mix, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locNight, &pic.night, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locTrack, &pic.track, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locStill, &pic.still, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locSnow, &pic.snow, SHADER_UNIFORM_FLOAT);
    // The shader works out its own coordinates from the fragment's position,
    // so the quad only has to cover the screen with `tex` bound.
    BeginShaderMode(shader);
    DrawTexturePro(tex, { 0, 0, (float)tex.width, (float)tex.height },
                   { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() }, { 0, 0 }, 0, WHITE);
    EndShaderMode();
}
