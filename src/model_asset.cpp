#include "model_asset.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>
#include <unordered_set>

namespace {
// raylib 6.0 moved the skeleton off Model and renamed ModelAnimation's frame
// fields. It is the same data under different names, but the file was written
// against 5.5 and simply does not compile against 6.0 — which is what
// tools/sandbox-build.sh links, so the whole game stopped building in the
// sandbox while `make` against a brew 5.5 kept working. These accessors are the
// only place that difference is allowed to live.
//
//   5.5                     6.0
//   model.boneCount         model.skeleton.boneCount
//   model.bones             model.skeleton.bones
//   clip.frameCount         clip.keyframeCount
//   clip.framePoses[f]      clip.keyframePoses[f]      (both are Transform *)
//   ModelAnimation::bones   gone — 6.0 keeps only boneCount
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
inline int boneCountOf(const Model &m) { return m.skeleton.boneCount; }
inline BoneInfo *bonesOf(const Model &m) { return m.skeleton.bones; }
inline int keyframesOf(const ModelAnimation &c) { return c.keyframeCount; }
inline const Transform *poseOf(const ModelAnimation &c, int f) { return c.keyframePoses[f]; }
// Two keyframes, both the same pose, and it has to be two.
//
// 6.0 also changed UpdateModelAnimation's frame from int to float and made it
// interpolate, so it reads keyframe f *and* f+1. Hand it a one-keyframe clip and
// it reads one past the end and segfaults inside the library — which is what
// the whole game did on startup, with a stack ending in UpdateModelAnimation
// and nothing in the log. Two identical keyframes interpolate to themselves.
inline ModelAnimation onePoseClip(const Model &m, ModelAnimPose *twoFrames) {
    ModelAnimation a{};
    a.boneCount = boneCountOf(m);
    a.keyframeCount = 2;
    a.keyframePoses = twoFrames;
    return a;
}
#else
inline int boneCountOf(const Model &m) { return m.boneCount; }
inline BoneInfo *bonesOf(const Model &m) { return m.bones; }
inline int keyframesOf(const ModelAnimation &c) { return c.frameCount; }
inline const Transform *poseOf(const ModelAnimation &c, int f) { return c.framePoses[f]; }
inline ModelAnimation onePoseClip(const Model &m, Transform **twoFrames) {
    ModelAnimation a{};
    a.boneCount = m.boneCount;
    a.bones = m.bones;
    a.frameCount = 1;      // 5.5 takes an int frame and does not interpolate
    a.framePoses = twoFrames;
    return a;
}
#endif

struct EmbeddedAsset { const char *path; const unsigned char *data; size_t size; };
#include "models.generated.h"
// Raylib's documented file callback preserves standard GLB loading while making
// packaged models independent of cwd. Ordinary filesystem assets still work.
unsigned char *readAsset(const char *path,int *size) {
    *size=0;
    for(const auto &asset:modelAssets) if(std::strcmp(path,asset.path)==0) {
        if(asset.size>INT_MAX) return nullptr;
        auto *copy=(unsigned char*)MemAlloc(asset.size);
        if(copy) {std::memcpy(copy,asset.data,asset.size);*size=(int)asset.size;}
        return copy;
    }
    FILE *file=std::fopen(path,"rb");if(!file)return nullptr;
    std::fseek(file,0,SEEK_END);long length=std::ftell(file);std::rewind(file);
    if(length<=0 || length>INT_MAX) {std::fclose(file);return nullptr;}
    auto *data=(unsigned char*)MemAlloc(length);
    if(!data || std::fread(data,1,length,file)!=(size_t)length) {MemFree(data);data=nullptr;}
    else *size=(int)length;
    std::fclose(file);return data;
}
Texture2D detailMap(Material material) {
    // Adapter between standard glTF normals/metallic-roughness and the existing
    // world shader. The source GLB remains portable to other tools/engines.
    Texture2D normal=material.maps[MATERIAL_MAP_NORMAL].texture;
    Texture2D mr=material.maps[MATERIAL_MAP_ROUGHNESS].texture;
    int width=normal.id?normal.width:(mr.id?mr.width:1);
    int height=normal.id?normal.height:(mr.id?mr.height:1);
    Image n=normal.id?LoadImageFromTexture(normal):GenImageColor(width,height,{128,128,255,255});
    Image r=mr.id?LoadImageFromTexture(mr):GenImageColor(width,height,{255,255,0,255});
    if(r.width!=width || r.height!=height) ImageResize(&r,width,height);
    Color *nc=LoadImageColors(n),*rc=LoadImageColors(r);
    for(int i=0;i<width*height;++i) {
        float z=fmaxf(nc[i].b/127.5f-1,.25f);
        auto byte=[](float v){return (unsigned char)std::clamp(v,0.0f,255.0f);};
        nc[i]={byte(128-127*(nc[i].r/127.5f-1)/z),byte(128-127*(nc[i].g/127.5f-1)/z),
               byte((1-rc[i].g/255.0f)*(.18f+.72f*rc[i].b/255.0f)*255),128};
    }
    Image packed{nc,width,height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D result=LoadTextureFromImage(packed);GenTextureMipmaps(&result);SetTextureFilter(result,result.mipmaps>1?TEXTURE_FILTER_TRILINEAR:TEXTURE_FILTER_BILINEAR);
    UnloadImageColors(nc);UnloadImageColors(rc);UnloadImage(n);UnloadImage(r);return result;
}
}
bool ModelAsset::load(const char *path) {
    if(model.meshes)unload();
    SetLoadFileDataCallback(readAsset);
    int bytes=0;unsigned char *data=LoadFileData(path,&bytes);
    bool isGlb=data && bytes>=12 && std::memcmp(data,"glTF",4)==0;
    UnloadFileData(data);
    if(!isGlb) {TraceLog(LOG_WARNING,"ASSET: Missing or invalid GLB: %s",path);return false;}
    model=LoadModel(path);
    if(!IsModelValid(model)) {unload();return false;}
    if(boneCountOf(model)>0) animations=LoadModelAnimations(path,&animationCount);
    for(int i=0;i<animationCount;++i) if(!IsModelAnimationValid(model,animations[i])) {unload();return false;}
    sampledPose.resize(boneCountOf(model));
    for(int i=0;i<model.materialCount;++i) {
        Texture2D &texture=model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
        if(texture.id!=rlGetTextureIdDefault()) {GenTextureMipmaps(&texture);SetTextureFilter(texture,TEXTURE_FILTER_TRILINEAR);}
        detailMaps.push_back(detailMap(model.materials[i]));
    }
    return true;
}
int ModelAsset::clip(const char *name) const {
    for(int i=0;i<animationCount;++i) if(std::strcmp(animations[i].name,name)==0)return i;
    return -1;
}
int ModelAsset::boneCount() const { return boneCountOf(model); }
const char *ModelAsset::boneName(int index) const {
    return (index>=0 && index<boneCountOf(model)) ? bonesOf(model)[index].name : "";
}
int ModelAsset::bone(const char *name) const {
    for(int i=0;i<boneCountOf(model);++i) if(std::strcmp(bonesOf(model)[i].name,name)==0)return i;
    return -1;
}
void ModelAsset::sample(int index,float progress) {
    const auto &clip=animations[index];
    float frame=std::clamp(progress,0.0f,1.0f)*(keyframesOf(clip)-1);
    int a=(int)frame,b=std::min(a+1,keyframesOf(clip)-1);float f=frame-a;
    for(int i=0;i<boneCountOf(model);++i) {
        const Transform &x=poseOf(clip,a)[i],&y=poseOf(clip,b)[i];
        sampledPose[i]={Vector3Lerp(x.translation,y.translation,f),QuaternionSlerp(x.rotation,y.rotation,f),Vector3Lerp(x.scale,y.scale,f)};
    }
}
void ModelAsset::update() {
    if(sampledPose.empty())return;
    Transform *frame=sampledPose.data();
    Transform *frames[2]={frame,frame};
    ModelAnimation pose=onePoseClip(model,frames);
    // A GLB can contain both static and skinned primitives. Raylib's CPU update
    // assumes every supplied mesh is skinned, so pass it only those meshes.
    std::vector<Mesh> skinned;
    for(int i=0;i<model.meshCount;++i) if(model.meshes[i].boneWeights)skinned.push_back(model.meshes[i]);
    Model animated=model;animated.meshes=skinned.data();animated.meshCount=(int)skinned.size();
    UpdateModelAnimation(animated,pose,0.0f);
#if RAYLIB_VERSION_MAJOR == 5 && RAYLIB_VERSION_MINOR == 5
    // Raylib 5.5 transforms normals as positions in its CPU skinning path.
    // Remove that erroneous weighted translation; vertex skinning stays upstream.
    // Remove this compatibility block when upgrading to a version with the fix.
    for(const Mesh &mesh:skinned) {
        if(!mesh.animNormals)continue;
        for(int v=0;v<mesh.vertexCount;++v) {
            Vector3 offset{};
            for(int j=0;j<4;++j) {
                int slot=v*4+j;float weight=mesh.boneWeights[slot];
                if(weight==0)continue;
                Matrix m=mesh.boneMatrices[mesh.boneIds[slot]];
                offset=Vector3Add(offset,Vector3Scale({m.m12,m.m13,m.m14},weight));
            }
            Vector3 n=Vector3Normalize(Vector3Subtract({mesh.animNormals[v*3],mesh.animNormals[v*3+1],mesh.animNormals[v*3+2]},offset));
            std::memcpy(mesh.animNormals+v*3,&n,sizeof(n));
        }
        UpdateMeshBuffer(mesh,2,mesh.animNormals,mesh.vertexCount*3*sizeof(float),0);
    }
#endif
}
void ModelAsset::draw(Material scene,Matrix placement) {
    for(int i=0;i<model.meshCount;++i) {
        int index=model.meshMaterial[i];Material material=model.materials[index];
        MaterialMap maps[MATERIAL_MAP_BRDF+2]{};
        std::copy(material.maps,material.maps+MATERIAL_MAP_BRDF+1,maps);
        material.maps=maps;material.shader=scene.shader;
        maps[MATERIAL_MAP_NORMAL].texture=scene.maps[MATERIAL_MAP_NORMAL].texture;
        maps[MATERIAL_MAP_SPECULAR].texture=detailMaps[index];
        DrawMesh(model.meshes[i],material,MatrixMultiply(model.transform,placement));
    }
}
void ModelAsset::unload() {
    // UnloadModel releases map descriptors, not the textures it imported.
    std::unordered_set<unsigned> textures;
    for(int i=0;i<model.materialCount;++i) for(int map=0;map<=MATERIAL_MAP_BRDF;++map) {
        Texture2D t=model.materials[i].maps[map].texture;
        if(t.id && t.id!=rlGetTextureIdDefault() && textures.insert(t.id).second)UnloadTexture(t);
    }
    for(Texture2D t:detailMaps)UnloadTexture(t);
    detailMaps.clear();sampledPose.clear();
    if(animations)UnloadModelAnimations(animations,animationCount);
    if(model.meshes)UnloadModel(model);
    model={};animations=nullptr;animationCount=0;
}
