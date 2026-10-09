#include "../core/fp_strict.h"
#include "atlas.h"

const FixtureRect FIXTURES[FIX_COUNT] = {
    { 8/1024.f,     8/512.f, 104/1024.f, 160/512.f, 0.0375f, 0.059f  },
    { 120/1024.f,   8/512.f, 216/1024.f, 160/512.f, 0.0375f, 0.059f  },
    { 232/1024.f,   8/512.f, 324/1024.f, 156/512.f, 0.036f,  0.0575f },
    { 8/1024.f,   168/512.f, 232/1024.f, 328/512.f, 0.280f,  0.200f  },
    { 296/1024.f, 176/512.f, 488/1024.f, 368/512.f, 0.300f,  0.300f  },
    { 280/1024.f, 400/512.f, 504/1024.f, 480/512.f, 0.280f,  0.100f  },
    // inset half a texel: these are tiled edge to edge, and bilinear filtering
    // would otherwise pull the neighbouring cell into every seam
    { 8.5f/1024.f, 336.5f/512.f, 135.5f/1024.f, 463.5f/512.f, 0.250f, 0.250f },
    { 336/1024.f,   8/512.f, 440/1024.f, 156/512.f, 0.074f,  0.105f  },
};
