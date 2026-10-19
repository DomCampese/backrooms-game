#pragma once
// The camcorder: a viewfinder raised with V (CAM on a phone), its infrared
// night shot, and the tape that runs for the whole descent. On the death card
// the last seconds of that tape play back. All of it is the platform's: the
// sim never learns the camera exists, and the input it is handed has already
// had the camera's buttons taken out (Camcorder::control).
#include "raylib.h"
#include "sim/audio_events.h"
#include "sim/input_frame.h"
#include <deque>
#include <vector>

struct Sim;
struct GameAudio;

struct Camcorder {
    static constexpr float RAISE_S = 0.28f;         // lens to the eye, or back down
    static constexpr float ZOOM_MAX = 4.0f;
    static constexpr float IR_DRAIN_S = 75.0f;      // night shot from a full charge
    static constexpr float IR_RECHARGE_S = 150.0f;  // ...and back from empty with it off
    static constexpr float GRAB_HZ = 15.0f;         // frames a second the tape keeps
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

    void load();
    void unload();
    // Frame cells take the window's aspect, so a resize starts a fresh tape.
    void fitReel(int screenW, int screenH);
    // Fold one tick's input before the sim sees it. V raises and lowers the
    // camera; while it is up, fire switches night shot, aim and the wheel
    // zoom, and the keys that would need the hand holding it lower it first.
    void control(InputFrame &in, const Sim &sim, float dt);
    bool viewing() const { return raise >= 0.5f; }
    float irLamp() const;                  // the world shader's uIR
    float fovY(float baseDeg) const;       // the view through the lens at this zoom

    void startTape();                      // a new run: nothing on the tape
    // One tick of the descent onto tape: the scene just rendered, if a
    // frame is due, and the interference the camera picked up.
    void record(const RenderTexture2D &rt, const Sim &sim, float dt, float runSeconds);
    void logSounds(const std::vector<AudioEvent> &events);

    // The death card's playback. begin returns false when there is too
    // little tape to be worth showing.
    bool beginReplay();
    void stepReplay(float dt, GameAudio &audio);
    void stopReplay();
    bool replaying() const { return replayOn; }
    // 0..1, how far the card's words have come up over the still frame.
    float cardAlpha() const;

    // Screen pass. The viewfinder draws the scene through the tape look; the
    // OSD goes over it, and over the playback.
    void drawViewfinder(const RenderTexture2D &rt, float time, float runSeconds);
    void drawReplay(float time);

private:
    struct Frame { float tapeT, runT; bool night; float track; };
    struct Logged { float tapeT; AudioEvent e; };

    Shader shader{};
    int locTime = -1, locRes = -1, locCellA = -1, locCellB = -1, locMix = -1,
        locNight = -1, locTrack = -1, locStill = -1, locSnow = -1;
    Sound sndRaise{}, sndLower{}, sndSwitch{}, sndZoom{}, sndTransport{}, sndHiss{};

    RenderTexture2D reel{};
    int cellW = 0, cellH = 0, cols = 0, slots = 0;
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
    Vector4 cellUV(int age) const;
    float tapeAt(float t) const;          // playback clock -> tape time
    void present(Texture2D tex, Vector4 cellA, Vector4 cellB, float mix, float time,
                 float nightAmt, float trackAmt, float still, float snow);
    void drawOsd(float time, float counterS, bool playback, bool still);
};
