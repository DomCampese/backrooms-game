#pragma once
#include "model_asset.h"

// Gameplay adapter only: standard asset loading/playback lives in ModelAsset.
struct Revolver {
    ModelAsset asset;
    Vector3 muzzlePosition{0,.0388f,.2371f};
    int idle=-1,reload=-1,shoot=-1,handle=-1;
    std::vector<int> spinningBones;
    int lastClip=-1,lastAmmo=-1;
    float lastProgress=-1;
    void load();
    void unload() {asset.unload();}
    void pose(float reloadTime,float cooldown,int ammo);
    void draw(Material scene,Matrix transform) {asset.draw(scene,transform);}
};
