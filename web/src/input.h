#pragma once
// All player input goes through these, so touch devices can answer them.
//
// Natively and in a desktop browser each one is the raylib call it is named
// after, inlined (identical generated code). On a touch device web/shell.html
// draws the controls and publishes their state, and web/src/input_web.cpp
// answers from it.
//
// Use these instead of raylib's input functions for player input. Window keys
// (F11) are not player input and stay direct.
#include "raylib.h"

#ifdef PLATFORM_WEB

// Copy the touch state for this frame. Called once at the top of Game::tick, so
// every query below sees the same snapshot within a frame.
void webInputPoll();
// 1.0 unless a thumbstick is pushed part way, so movement stays analog on a
// stick and unchanged for anything that returns booleans.
float webMoveScale();
// The touch AIM button latches rather than being held (input_web.cpp). The game
// decides when an aim is allowed, so it drops a latch it refused; otherwise the
// button stays lit with the gun lowered.
void webReleaseAim();

bool    inKeyDown(int key);
bool    inKeyPressed(int key);
bool    inMouseDown(int button);
bool    inMousePressed(int button);
Vector2 inMouseDelta();
float   inWheel();
bool    inCursorHidden();
// True only when the on-screen controls are up, so the HUD names touch buttons
// instead of keys.
bool    inTouchActive();
// A tap on open screen or a push of the stick: starts a run from the title
// screen on touch.
bool    webStartGesture();

#else

inline void    webInputPoll() {}
inline float   webMoveScale() { return 1.0f; }
inline void    webReleaseAim() {}
inline bool    inTouchActive() { return false; }
inline bool    webStartGesture() { return false; }

inline bool    inKeyDown(int key)       { return IsKeyDown(key); }
inline bool    inKeyPressed(int key)    { return IsKeyPressed(key); }
inline bool    inMouseDown(int button)  { return IsMouseButtonDown(button); }
inline bool    inMousePressed(int b)    { return IsMouseButtonPressed(b); }
inline Vector2 inMouseDelta()           { return GetMouseDelta(); }
inline float   inWheel()                { return GetMouseWheelMove(); }
inline bool    inCursorHidden()         { return IsCursorHidden(); }

#endif
