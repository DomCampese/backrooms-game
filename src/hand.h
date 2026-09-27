#pragma once
// The hand that holds the revolver: a gloved right hand gripping it, and the
// jacket sleeve behind it. Built once at startup, in the revolver GLB's own
// model space (metres, +z toward the muzzle, +y up), so it is drawn with the
// same transform as the gun's handle bone and follows recoil and the reload.
#include "raylib.h"

struct HeldHand {
    Mesh glove{}, sleeve{};
    Texture2D leather{}, leatherDetail{}, knit{}, knitDetail{};
};
HeldHand buildHeldHand();
void unloadHeldHand(HeldHand &h);
