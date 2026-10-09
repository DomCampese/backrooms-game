#pragma once
// The hunter and the pack as billboards: which sheet, which two walk frames
// and how far between them, which row, where and how big. Decided from the
// sim, so both builds draw the same pose; render.cpp and the Unreal scene read
// these. Lighting and fog are each renderer's own.
//
// Metres in the sim's frame: the storey the player is on, y = 0 its floor.
#include "../sim/sim.h"
#include "sheets.h"
#include <cstdint>
#include <vector>

enum class SpriteSheet : uint8_t { Clark, Smiler, Partygoer, Dog, Count };

struct ActorSprite {
    SpriteSheet sheet;
    bool glow = false;       // the Smiler: its eyes and grin go over the body, unlit
    Vec3 centre{};           // the billboard's middle
    float w = 0, h = 0;      // its size, metres
    int frame0 = 0, frame1 = 0;
    float blend = 0;         // frame0 at 1 - blend, frame1 at blend
    int row = 0;             // EntRow; 0 on the dog sheet
    float fade = 1;          // 1, falling to 0 as the hunter dies or a dog yelps away
    Vec3 litAt{};            // where the renderer samples the room's light
};

// Which two frames of a walk sheet a gait phase falls between, and how far.
// Cross-fading rather than snapping matters at this frame count: six frames
// over two steps is about one frame every 0.35 m, and a hard cut at that rate
// strobes. Faded, the ghost is where a fast-moving limb would be smeared anyway.
void gaitFrames(float phase, int frames, int &f0, int &f1, float &t);

// Dog i, if it is drawn (out of the pack's Gone state and within 42 m).
bool dogSprite(const Sim &sim, int i, ActorSprite &out);
// The hunter, if it is drawn (not hidden, and within 45 m).
bool hunterSprite(const Sim &sim, ActorSprite &out);
// The pack, then the hunter.
std::vector<ActorSprite> actorSprites(const Sim &sim);
