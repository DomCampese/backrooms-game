#pragma once
// The Web build's half of the camcorder (port/camcorder.h has the rules, the
// tape's bookkeeping and the OSD): the reel, one render texture of frame
// cells; the tape look (TAPE_FS) that draws the viewfinder and the playback;
// and the camera's sounds.
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
    // The picture through the tape look, covering the window: the scene or the reel.
    void present(const CamPicture &pic, const RenderTexture2D &scene, float time);

private:
    Shader shader{};
    int locTime = -1, locRes = -1, locCellA = -1, locCellB = -1, locMix = -1,
        locNight = -1, locTrack = -1, locStill = -1, locSnow = -1;
    Sound sndRaise{}, sndLower{}, sndSwitch{}, sndZoom{}, sndTransport{}, sndHiss{};
    RenderTexture2D reel{};
};
