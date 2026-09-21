#pragma once
// One place the game asks "is the player doing this", so a platform with no
// keyboard and no mouse can answer.
//
// Natively — and on a desktop browser — every one of these is the raylib call
// it is named after, inlined, and the generated code is identical. On a phone
// there is no pointer to lock and no key to press: web/shell.html draws the
// touch controls, publishes their state, and src/input_web.cpp folds that state
// into the same answers. Game code does not know which happened.
//
// Use these rather than the raylib functions anywhere the *player's* intent is
// being read. Window-level keys (F11) are not player intent and stay direct.
#include "raylib.h"

#ifdef PLATFORM_WEB

// Copy the touch state for this frame. Called once at the top of Game::tick, so
// that every query below sees one coherent snapshot — a button read twice in a
// frame must not answer differently the second time.
void webInputPoll();
// 1.0 unless a thumbstick is pushed part way, so movement stays analog on a
// stick and unchanged for anything that returns booleans.
float webMoveScale();
// The touch AIM button latches rather than being held (input_web.cpp). The game
// decides when an aim is legal, so it needs a way to drop a latch it has
// refused — otherwise the button sits lit over a gun that never comes up.
void webReleaseAim();

bool    inKeyDown(int key);
bool    inKeyPressed(int key);
bool    inMouseDown(int button);
bool    inMousePressed(int button);
Vector2 inMouseDelta();
float   inWheel();
bool    inCursorHidden();
// True only when the on-screen controls are up. The HUD asks so it can name the
// buttons the player actually has: a title card telling a phone to press WASD
// is telling it to do something it cannot.
bool    inTouchActive();

#else

inline void    webInputPoll() {}
inline float   webMoveScale() { return 1.0f; }
inline void    webReleaseAim() {}
inline bool    inTouchActive() { return false; }

inline bool    inKeyDown(int key)       { return IsKeyDown(key); }
inline bool    inKeyPressed(int key)    { return IsKeyPressed(key); }
inline bool    inMouseDown(int button)  { return IsMouseButtonDown(button); }
inline bool    inMousePressed(int b)    { return IsMouseButtonPressed(b); }
inline Vector2 inMouseDelta()           { return GetMouseDelta(); }
inline float   inWheel()                { return GetMouseWheelMove(); }
inline bool    inCursorHidden()         { return IsCursorHidden(); }

#endif
