#pragma once
// The camcorder: a viewfinder raised with V (CAM on a phone), its infrared
// night shot, and the tape that runs for the whole descent. On the death card
// the last seconds of that tape play back. The sim never learns the camera
// exists: the input it is handed has already had the camera's buttons taken
// out (Camcorder::control).
//
// This is the camera's rules, its tape's bookkeeping and its OSD. A platform
// keeps the reel (one texture of frame cells), draws the picture through the
// tape look (TAPE_FS in the Web build) and plays the sounds it asks for.
#include "hud.h"
#include "sounds.h"
#include "../sim/audio_events.h"
#include "../sim/input_frame.h"
#include <ctime>
#include <deque>
#include <functional>
#include <vector>

struct Sim;

// A sound the camera asks for, played in the order asked.
enum class CamSound : uint8_t {
    Raise, Lower, Switch,
    Zoom,        // the lens motor, unless it is already running
    Transport,   // the deck taking the tape
    Hiss,        // the playback's hiss, started again whenever it has stopped
    HissStop,
    Echo,        // a sound the run made, heard again off the tape
};
constexpr int CAM_CLIPS = (int)CamSound::Hiss + 1;   // the camera's own clips, Raise to Hiss

struct CamNoise {
    CamSound sound;
    AudioEvent echo;       // Echo: the event, its pitch and volume scaled by these
    float pitch, volume;
};

// The camera's synthesized clips (Raise to Hiss); the hiss plays at HISS_VOLUME.
Pcm camcorderClip(CamSound sound);

// A frame's rectangle on the reel as uv, v running up the picture.
struct CamCell { float u0, v0, u1, v1; };

// What the tape look draws this frame: the live scene (the whole of it) or
// two neighbouring frames off the reel, `mix` of the way from a to b.
struct CamPicture {
    bool fromReel;
    CamCell a, b;
    float mix, night, track, still, snow;
};

struct Camcorder {
    static constexpr float RAISE_S = 0.28f;         // lens to the eye, or back down
    static constexpr float ZOOM_MAX = 4.0f;
    static constexpr float IR_DRAIN_S = 75.0f;      // night shot from a full charge
    static constexpr float IR_RECHARGE_S = 150.0f;  // ...and back from empty with it off
    static constexpr float GRAB_HZ = 15.0f;         // frames a second the tape keeps
    static constexpr float HISS_VOLUME = 0.22f;
    // The whole tape is one texture, so the reel is one framebuffer however
    // many frames it holds. WebGL 2 guarantees 2048 on a side and no more.
    static constexpr int REEL_SIDE = 2048;
    static constexpr int CELL_LONG = 288;           // a frame's long side on the reel, px

    bool up = false;
    float raise = 0;          // 0 lowered .. 1 at the eye
    bool night = false;
    float zoom = 1, zoomSet = 1;
    float irCharge = 1;
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

    // Fold one tick's input before the sim sees it. `toggle` (V) raises and
    // lowers the camera; while it is up, fire switches night shot, aim and the
    // wheel zoom, and the keys that would need the hand holding it lower it first.
    void control(InputFrame &in, const Sim &sim, float dt, bool toggle);
    bool viewing() const { return raise >= 0.5f; }
    float irLamp() const;                  // the world shader's uIR
    float fovY(float baseDeg) const;       // the view through the lens at this zoom

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

    // The viewfinder, and the playback (while replaying()): the picture and
    // the OSD over it. `clock` is the wall clock's local time, or null.
    CamPicture viewfinderPicture() const;
    CamPicture replayPicture() const;
    std::vector<HudDraw> viewfinderOsd(int w, int h, float time, float runSeconds,
                                       const std::tm *clock, const HudMeasure &measure) const;
    std::vector<HudDraw> replayOsd(int w, int h, float time, const std::tm *clock,
                                   const HudMeasure &measure) const;

private:
    struct Frame { float tapeT, runT; bool night; float track; };
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
    bool zoomedLast = false;              // the aim was down last tick (a touch AIM tap is an edge)

    const Frame &frameAt(int age) const;  // 0 = oldest held
    CamCell cellUV(int age) const;
    float tapeAt(float t) const;          // playback clock -> tape time
    Playhead playhead(float tape) const;
    void play(CamSound s) { noises.push_back({ s, {}, 1.0f, 1.0f }); }
    void osd(std::vector<HudDraw> &out, int w, int h, float time, float counterS, bool playback,
             bool still, const std::tm *clock, const HudMeasure &measure) const;
};
