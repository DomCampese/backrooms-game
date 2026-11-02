#pragma once
// The camcorder's tape, which runs for the whole descent. On the death card
// the last seconds of it play back. The player never holds the camera: the
// eye sees through a lighter tape look (POST_FS) and a small REC mark sits in
// the top left. The sim never learns the tape exists.
//
// This is the tape's bookkeeping, its playback and its OSD. A platform keeps
// the reel (one texture of frame cells), draws the playback through the tape
// look (TAPE_FS in the Web build) and plays the sounds it asks for.
#include "hud.h"
#include "sounds.h"
#include "../sim/audio_events.h"
#include <ctime>
#include <deque>
#include <functional>
#include <vector>

struct Sim;

// A sound the camera asks for, played in the order asked.
enum class CamSound : uint8_t {
    Transport,   // the deck taking the tape
    Hiss,        // the playback's hiss, started again whenever it has stopped
    HissStop,
    Echo,        // a sound the run made, heard again off the tape
};
constexpr int CAM_CLIPS = (int)CamSound::Hiss + 1;   // the camera's own clips, Transport and Hiss

struct CamNoise {
    CamSound sound;
    AudioEvent echo;       // Echo: the event, its pitch and volume scaled by these
    float pitch, volume;
};

// The camera's synthesized clips (Transport and Hiss); the hiss plays at HISS_VOLUME.
Pcm camcorderClip(CamSound sound);

// A frame's rectangle on the reel as uv, v running up the picture.
struct CamCell { float u0, v0, u1, v1; };

// What the tape look draws this frame: two neighbouring frames off the reel,
// `mix` of the way from a to b.
struct CamPicture {
    CamCell a, b;
    float mix, track, still, snow;
};

struct Camcorder {
    static constexpr float GRAB_HZ = 15.0f;         // frames a second the tape keeps
    static constexpr float HISS_VOLUME = 0.22f;
    // The whole tape is one texture, so the reel is one framebuffer however
    // many frames it holds. WebGL 2 guarantees 2048 on a side and no more.
    static constexpr int REEL_SIDE = 2048;
    static constexpr int CELL_LONG = 288;           // a frame's long side on the reel, px

    float track = 0;          // interference: 0 clean .. 1 torn

    // What to play, in order; the platform plays and clears it.
    std::vector<CamNoise> noises;

    // Frame cells take the window's aspect, so a resize starts a fresh tape.
    // The platform then makes a reel reelW() x reelH().
    void fitReel(int screenW, int screenH);
    int reelW() const { return cols * cellW; }
    int reelH() const { return (slots + cols - 1) / cols * cellH; }
    int cellWidth() const { return cellW; }
    int cellHeight() const { return cellH; }
    int cellX(int slot) const { return slot % cols * cellW; }   // px on the reel, y down
    int cellY(int slot) const { return slot / cols * cellH; }

    void startTape();                      // a new run: nothing on the tape
    // One tick of the descent onto tape: the interference the camera picked
    // up, and the slot to draw the scene just rendered into, or -1 when no
    // frame is due. `reelReady`: the platform has a reel to draw on.
    // `random(lo, hi)` is an inclusive integer draw (raylib's GetRandomValue).
    int record(const Sim &sim, float dt, float runSeconds, bool reelReady,
               const std::function<int(int, int)> &random);
    void logSounds(const std::vector<AudioEvent> &events);

    // The death card's playback. begin returns false when there is too
    // little tape to be worth showing.
    bool beginReplay();
    void stepReplay(float dt);
    void stopReplay();
    bool replaying() const { return replayOn; }
    // 0..1, how far the card's words have come up over the still frame.
    float cardAlpha() const;

    // The playback (while replaying()): the picture and the OSD over it.
    // `clock` is the wall clock's local time, or null.
    CamPicture replayPicture() const;
    std::vector<HudDraw> replayOsd(int w, int h, const std::tm *clock,
                                   const HudMeasure &measure) const;
    // The REC mark in the top left while the tape runs in play: a blinking
    // red dot and REC, small, under the HUD's own scale.
    std::vector<HudDraw> recMark(int w, int h, float time) const;

private:
    struct Frame { float tapeT, runT, track; };
    struct Logged { float tapeT; AudioEvent e; };
    struct Playhead { int a, b; float mix; };

    int cellW = 0, cellH = 0, cols = 1, slots = 0;
    std::vector<Frame> frames;            // by reel slot
    int head = 0, count = 0;              // next slot to write; frames held
    float tapeClock = 0, nextGrab = 0;
    std::deque<Logged> sounds;
    float glitch = 0;                     // a burst of tearing, seconds left

    bool replayOn = false;
    float replayT = 0, lastTape = 0;
    float tapeFrom = 0, tapeTo = 0, slowFrom = 0, freezeAt = 0;
    size_t nextSound = 0;

    const Frame &frameAt(int age) const;  // 0 = oldest held
    CamCell cellUV(int age) const;
    float tapeAt(float t) const;          // playback clock -> tape time
    Playhead playhead(float tape) const;
    void play(CamSound s) { noises.push_back({ s, {}, 1.0f, 1.0f }); }
    void osd(std::vector<HudDraw> &out, int w, int h, float counterS,
             bool still, const std::tm *clock, const HudMeasure &measure) const;
};
