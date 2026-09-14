#pragma once
#include "raylib.h"
#include <array>
#include <vector>

// The imported rig is rigid-weighted: two material batches, CPU skinning only
// while its pose changes. All source data is embedded; no runtime file paths.
struct Revolver {
    struct Vertex { Vector3 position, normal; unsigned joint; };
    Mesh meshes[2]{};
    Texture2D albedo[2]{}, detail[2]{};
    std::vector<Vertex> bind[2];
    struct Clip { float duration; std::vector<std::array<float, 17*12>> frames; };
    Clip clips[3];
    Vector3 muzzlePosition{0,.0388f,.2371f};
    int lastClip=-1, lastAmmo=-1;
    float lastTime=-1;
    void load();
    void unload();
    void pose(float reload, float cooldown, int ammo);
    void draw(Material material, Matrix transform);
};
