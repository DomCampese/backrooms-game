#pragma once
#include "sim_math.h"

// Keys that only act while the F3 debug HUD is up. The platform leaves them
// false otherwise.
struct DevKeys {
    bool blackout = false;     // B: a blackout now
    bool spawnAhead = false;   // E: the hunter stalking ~12 m ahead
    bool chase = false;        // C: a chase, spawning him first if hidden
    bool banish = false;       // H
    bool refill = false;       // G: flares and ammo
    bool storeyUp = false, storeyDown = false;   // PageUp / PageDown
    bool nextLevel = false;    // N
};

// What the player asked for on one tick. The platform fills it once from
// src/input.h before stepping the sim, and the sim reads input through nothing
// else. `...Pressed` fields and the single actions are down edges; the rest
// are held state.
struct InputFrame {
    // The mouse is captured, or the touch controls are up (inCursorHidden).
    // Looking, aiming, firing, throwing, reloading and squeezing need it.
    bool playing = false;
    bool touch = false;          // touch controls are up: the stick sprints, so no double-tap run

    bool forward = false, back = false, left = false, right = false;
    bool forwardPressed = false;  // for the double-tap run
    float moveScale = 1.0f;       // a thumbstick's share of full speed; 1 for keys
    bool sprint = false, crouch = false, squeeze = false;
    bool jumpHeld = false, jumpPressed = false;
    Vec2 look{};               // mouse or touch-drag delta, px

    float wheel = 0;              // weapon-cycle steps
    bool pickRevolver = false, pickFlare = false, pickDeck = false;
    bool fire = false;            // primary press, less a click that captured the mouse
    bool aim = false;             // secondary held (touch latches it)
    bool reload = false, throwFlare = false, flashlight = false;
    bool use = false, drink = false, chalk = false;

    bool begin = false;           // title screen: any key but F11, a click, or a touch start gesture
    DevKeys dev;
    float screenFov = 70.0f;      // base vertical FOV for the window, deg (Game::fovForWindow)
    bool forceSpawn = false;      // headless capture: put the hunter in view on this tick
};
