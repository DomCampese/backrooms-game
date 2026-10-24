#pragma once
// What the Web build draws over the world each frame (render.cpp's renderUI):
// the title card, the death card, the intro and level name, the cards that
// come and go (an exit, a kill, the Manila Room, a note, the escape), the
// meters, the inventory and the hints, as a list of draws in window pixels.
// render.cpp draws from it, so the Unreal port's HUD lays out the same.
//
// Sizes come from the Web build's 850-pixel-high window, scaled to the one in
// hand. Text is measured by the caller's font, so a line is centred on the
// font that draws it.
#include "../sim/sim.h"
#include "mesh_builder.h"
#include <functional>
#include <string>
#include <vector>

struct HudDraw {
    enum Kind : uint8_t {
        Text,         // text at (x, y), top left, h pixels high
        Rect,         // filled, (x, y) to (x + w, y + h)
        RectLines,    // the same, outlined one pixel wide
        GradientV,    // filled, col at the top fading to col2 at the bottom
        GradientH,    // filled, col at the left fading to col2 at the right
        Circle,       // filled, centred on (x, y), radius r
        Triangle,     // filled, pointing right: (x, y), (x, y + h), (x + r, y + h / 2)
    };
    Kind kind;
    int x = 0, y = 0, w = 0, h = 0;
    float r = 0;
    Rgba col = {}, col2 = {};
    std::string text;
};

// What the HUD needs that the sim does not hold.
struct HudInput {
    int width, height;        // the window, pixels
    double now;               // the frame's clock (BACKROOMS_TIME pins it)
    bool filming;             // through the camcorder: its OSD replaces the inventory and meters
    bool replaying;           // the death card's tape is playing
    float cardAlpha;          // and how far its last frame has faded under the card
    bool touch;               // the touch controls are up: hints name buttons, not keys
    bool captureHint;         // the cursor is free and this is not a capture
    int fps;
    bool debug;               // the F3 overlay
};

// Measures a line of text at a size, in pixels, as the font that draws it does.
using HudMeasure = std::function<int(const char *text, int px)>;

// The frame's draws, in order. `everFlashed` remembers that the torch has been
// used, so its reminder goes for good; it is set here, where the reminder
// would be drawn.
std::vector<HudDraw> hudDraws(const Sim &sim, const HudInput &in, bool &everFlashed, const HudMeasure &measure);
