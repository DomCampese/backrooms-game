#pragma once
// The hand that holds the revolver: a bare right hand gripping it, and the
// black sleeve behind it. Built once at startup, in the revolver GLB's own
// model space (metres, +z toward the muzzle, +y up), so it
// is drawn with the same transform as the gun's handle bone and follows recoil
// and the reload.
#include "raylib.h"
#include <vector>

struct HeldHand {
    // Skin is split across meshes because a raylib mesh has 16-bit indices.
    // Each group is drawn with its own pair of maps: skin with the tiled skin
    // texture, the knuckle patches with their wrinkle atlas, the nails plain
    // white (their colour is all in the vertices) with their own gloss.
    std::vector<Mesh> skin, knuckles, nails;
    Mesh sleeve{};
    Texture2D skinTex{}, skinDetail{}, knuckleTex{}, knuckleDetail{}, white{}, nailDetail{},
              knit{}, knitDetail{};
};
HeldHand buildHeldHand();
void unloadHeldHand(HeldHand &h);
