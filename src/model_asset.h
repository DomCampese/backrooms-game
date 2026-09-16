#pragma once
#include "raylib.h"
#include <vector>

// Standard raylib/glTF asset loading. One instance owns one animated model;
// static meshes and multiple materials use the same path without a custom importer.
struct ModelAsset {
    Model model{};
    ModelAnimation *animations=nullptr;
    int animationCount=0;
    std::vector<Transform> sampledPose;
    std::vector<Texture2D> detailMaps;
    ModelAsset()=default;
    ModelAsset(const ModelAsset&)=delete;
    ModelAsset& operator=(const ModelAsset&)=delete;
    bool load(const char *path);
    void unload();
    int clip(const char *name) const;
    int bone(const char *name) const;
    // The skeleton, asked for by name rather than reached for through Model.
    // Where those live moved between raylib 5.5 and 6.0, and model_asset.cpp is
    // the one file allowed to know that.
    int boneCount() const;
    const char *boneName(int index) const;
    void sample(int clipIndex,float progress);
    void update();
    void draw(Material sceneMaterial,Matrix placement);
};
