#pragma once
// What stands in the world besides its chunks, from the sim: loose pickups,
// crates, the set-down tape deck, spilled doubloons, burning flares, chalk,
// balloons and confetti, bullet impacts, the hunter and the pack. For each,
// where it is and which way it faces, decided by the same calls the rules make
// (pickupAt, crateAt, balloonAt, ...), so what a port draws is what the sim
// tests. How each looks, and any bob or flicker, is the renderer's.
//
// Metres in the sim's frame: the storey the player is on, y = 0 its floor.
#include "../sim/sim.h"
#include <cstdint>
#include <vector>

enum class SceneKind : uint8_t {
    AlmondWater, Doubloon, Battery, Tape, Key,   // loose pickups, on the floor or a shelf
    Crate, CrateOpen,                            // Level 1's supply crates
    Deck,                                        // the tape deck, set down (variant 1: playing)
    Coin,                                        // a doubloon spilled by the hunter
    Flare,                                       // burning; `amount` is its burn left, 0..1
    Chalk,                                       // an arrow on the floor (variant 1: yours)
    Balloon,                                     // variant: its party colour
    Confetti,                                    // variant: its party colour; `amount` its life
    Impact,                                      // a round's mark; `normal` the face it hit
    Hunter,                                      // variant: its EState
    Dog,                                         // variant: its DState
    Count,
};

struct SceneItem {
    SceneKind kind;
    Vec3 pos;                // its base (or centre, for balloons, confetti and flares)
    float yaw = 0;           // radians, core's convention: turning +x toward +z
    Vec3 normal{ 0, 1, 0 };
    float amount = 0;
    uint8_t variant = 0;
};

// Everything within `radius` metres of the player (the hunter and pack to
// twice that). Non-const: the pickup and crate rules generate the chunks they
// read.
std::vector<SceneItem> simScene(Sim &sim, float radius);
