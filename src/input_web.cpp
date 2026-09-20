// Touch input for the browser build. web/shell.html owns the on-screen
// controls and writes their state into Module.__touch; this reads that once a
// frame and answers the game's ordinary input questions from it.
#ifdef PLATFORM_WEB
#include "input.h"
#include <emscripten/emscripten.h>
#include <cstdint>
#include <cmath>

namespace {

// Virtual buttons. THESE BITS ARE DUPLICATED IN web/shell.html — the JS sets
// them and this file reads them, and nothing checks that the two agree. Change
// one and a button silently starts doing a different job: the symptom is a
// control that works but fires the wrong action, not one that does nothing.
enum : uint32_t {
    BTN_FIRE     = 1u << 0,
    BTN_AIM      = 1u << 1,
    BTN_RELOAD   = 1u << 2,
    BTN_FLASH    = 1u << 3,
    BTN_INTERACT = 1u << 4,
    BTN_JUMP     = 1u << 5,
    BTN_WEAPON   = 1u << 6,
    BTN_CHALK    = 1u << 7,
    BTN_DRINK    = 1u << 8,
    BTN_SPRINT   = 1u << 9,
    BTN_CROUCH   = 1u << 10,
    BTN_THROW    = 1u << 11,
};

// One frame's worth of touch state, filled by the EM_ASM block below. Laid out
// as 4-byte fields because that block addresses it through HEAPU32/HEAPF32.
struct TouchFrame {
    uint32_t active;    // are the touch controls on at all
    uint32_t down;      // held this frame
    uint32_t pressed;   // went down this frame (edge)
    float    lookX;     // look drag since the last poll, in pixels
    float    lookY;
    float    moveX;     // thumbstick, -1..1, already deadzoned by the shell
    float    moveY;
    float    wheel;     // weapon cycle steps
};
TouchFrame g{};

bool touchOn() { return g.active != 0; }

} // namespace

void webInputPoll() {
    EM_ASM({
        var p = $0 >> 2;
        var t = Module.__touch;
        if (!t || !t.active) { HEAPU32[p] = 0; return; }
        HEAPU32[p + 0] = 1;
        HEAPU32[p + 1] = t.down;
        // Edges are drained, not sampled: a tap that starts and ends between
        // two frames still has to fire exactly once. The shell accumulates
        // them and this is the only reader.
        HEAPU32[p + 2] = t.pressed; t.pressed = 0;
        HEAPF32[p + 3] = t.lookX;   t.lookX = 0;
        HEAPF32[p + 4] = t.lookY;   t.lookY = 0;
        HEAPF32[p + 5] = t.moveX;
        HEAPF32[p + 6] = t.moveY;
        HEAPF32[p + 7] = t.wheel;   t.wheel = 0;
    }, &g);
}

float webMoveScale() {
    if (!touchOn()) return 1.0f;
    float m = sqrtf(g.moveX * g.moveX + g.moveY * g.moveY);
    if (m <= 0.001f) return 1.0f;      // not touching the stick: keys decide
    return m > 1.0f ? 1.0f : m;
}

// The thumbstick stands in for the movement keys. It is reported in the
// player's own frame — y forward, x right — which is what W/S and A/D are.
bool inKeyDown(int key) {
    if (IsKeyDown(key)) return true;
    if (!touchOn()) return false;
    const float DZ = 0.18f;
    switch (key) {
        case KEY_W:            return g.moveY >  DZ;
        case KEY_S:            return g.moveY < -DZ;
        case KEY_D:            return g.moveX >  DZ;
        case KEY_A:            return g.moveX < -DZ;
        case KEY_LEFT_SHIFT:   return (g.down & BTN_SPRINT) != 0;
        case KEY_LEFT_CONTROL: return (g.down & BTN_CROUCH) != 0;
        default:               return false;
    }
}

bool inKeyPressed(int key) {
    if (IsKeyPressed(key)) return true;
    if (!touchOn()) return false;
    switch (key) {
        case KEY_R:     return (g.pressed & BTN_RELOAD)   != 0;
        case KEY_F:     return (g.pressed & BTN_FLASH)    != 0;
        case KEY_E:     return (g.pressed & BTN_INTERACT) != 0;
        case KEY_SPACE: return (g.pressed & BTN_JUMP)     != 0;
        case KEY_M:     return (g.pressed & BTN_CHALK)    != 0;
        case KEY_THREE: return (g.pressed & BTN_DRINK)    != 0;
        case KEY_Q:     return (g.pressed & BTN_THROW)    != 0;
        default:        return false;
    }
}

bool inMouseDown(int button) {
    if (IsMouseButtonDown(button)) return true;
    if (!touchOn()) return false;
    return button == MOUSE_BUTTON_RIGHT && (g.down & BTN_AIM) != 0;
}

bool inMousePressed(int button) {
    if (IsMouseButtonPressed(button)) return true;
    if (!touchOn()) return false;
    return button == MOUSE_BUTTON_LEFT && (g.pressed & BTN_FIRE) != 0;
}

Vector2 inMouseDelta() {
    Vector2 d = GetMouseDelta();
    if (!touchOn()) return d;
    // The look drag is in the same pixels a mouse delta is, so the game's own
    // sensitivity — including the aim-down-sights reduction — applies unchanged.
    return { d.x + g.lookX, d.y + g.lookY };
}

float inWheel() {
    float w = GetMouseWheelMove();
    return touchOn() ? w + g.wheel : w;
}

// There is no pointer to lock on a phone, and IsCursorHidden gates nearly
// everything the player can do — looking, firing, reloading, throwing. Touch
// controls being up IS the playing state, so say so. Without this the game
// renders perfectly and ignores every input.
bool inCursorHidden() {
    return touchOn() || IsCursorHidden();
}

bool inTouchActive() { return touchOn(); }

#endif
