#pragma once
// The revolver in hand, posed as the Web build poses it (web/src/revolver.cpp):
// the clip and its progress from revolverPose, the drum and its live rounds
// turned about the handle's z by the shots fired, skinned as raylib 5.5 skins
// it (port/model.h). The Unreal port draws the gun from this.
#include "model.h"

struct RevolverModel {
    GlbModel glb;
    // The muzzle, in the GLB's frame, as the current pose leaves it.
    Vec3 muzzle = { 0, 0, 0 };

    // False with a reason if the file is not the revolver this expects.
    bool load(const uint8_t *data, size_t size, std::string &error);
    // Poses the gun for the sim's state; true when the pose changed.
    bool pose(float reloadT, float gunCd, int ammo);
    // Primitive `prim` skinned to the current pose.
    void skinned(int prim, std::vector<float> &pos, std::vector<float> &normal) const;

private:
    int idle = -1, reload = -1, shoot = -1, handle = -1;
    std::vector<int> spinning;
    std::vector<GlbPose> current;
    int lastClip = -1, lastAmmo = -1;
    float lastProgress = -1;
};
