#pragma once
// The Web build's half of the camcorder (port/camcorder.h has the rules, the
// tape's bookkeeping and the OSD): the reel, one render texture of frame
// cells; the tape look (TAPE_FS) that draws the playback; and the tape's
// sounds.
#include "raylib.h"
#include "port/camcorder.h"

struct GameAudio;

struct CamcorderReel {
    void load();
    // A new reel for the cells `cam` has just fitted to the window.
    void fit(const Camcorder &cam);
    void unload();
    bool ready() const { return reel.id != 0; }
    // The scene just rendered, into the reel's cell `slot`.
    void grab(const Camcorder &cam, int slot, const RenderTexture2D &rt);
    // Plays what the camera asked for and clears the list.
    void playNoises(Camcorder &cam, GameAudio &audio);
    // The playback through the tape look, covering the window.
    void present(const CamPicture &pic, float time);

private:
    Shader shader{};
    int locTime = -1, locRes = -1, locCellA = -1, locCellB = -1, locMix = -1,
        locTrack = -1, locStill = -1, locSnow = -1;
    Sound sndTransport{}, sndHiss{};
    RenderTexture2D reel{};
};
