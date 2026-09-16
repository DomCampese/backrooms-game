#include "revolver.h"
#include "raymath.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace {
Vector3 transformPoint(Vector3 v,const Transform &t) {
    return Vector3Add(Vector3RotateByQuaternion(Vector3Multiply(v,t.scale),t.rotation),t.translation);
}
}
void Revolver::load() {
    if(!asset.load("models/revolver.glb")) {
        TraceLog(LOG_ERROR,"Cannot load the revolver GLB");std::exit(EXIT_FAILURE);
    }
    idle=asset.clip("Idle");reload=asset.clip("Reload");shoot=asset.clip("Shoot");
    handle=asset.bone("DEF_RevolverHandle");
    int cylinder=asset.bone("DEF_Cylinder");
    if(idle<0 || reload<0 || shoot<0 || handle<0 || cylinder<0) {
        TraceLog(LOG_ERROR,"Revolver GLB lacks required clips or named joints");std::exit(EXIT_FAILURE);
    }
    spinningBones.push_back(cylinder);
    for(int i=0;i<asset.boneCount();++i)
        if(std::strncmp(asset.boneName(i),"DEF_Bullet",10)==0)spinningBones.push_back(i);
    pose(0,0,6);
}
void Revolver::pose(float reloadTime,float cooldown,int ammo) {
    int clip=idle;float progress=0;
    if(reloadTime>0) {clip=reload;progress=std::clamp(1-reloadTime/1.8f,0.0f,1.0f);}
    else if(cooldown>0) {clip=shoot;progress=std::clamp(1-cooldown/.42f,0.0f,1.0f);}
    if(clip==lastClip && progress==lastProgress && ammo==lastAmmo)return;
    lastClip=clip;lastProgress=progress;lastAmmo=ammo;
    asset.sample(clip,progress);
    const Transform root=asset.sampledPose[handle];
    muzzlePosition=transformPoint({0,.038805f,.237063f},root);
    // The prepared Shoot clip is one cycle. Preserve the cumulative drum index
    // across shots, and blend it back to the authored reload's starting index.
    float turns=6-std::clamp(ammo,0,6)-(clip==shoot?1:0);
    if(clip==reload)turns*=1-std::min(progress*(1.1666666f/.15f),1.0f);
    Vector3 pivot=transformPoint({0,.038805f,.033062f},root);
    Vector3 axis=Vector3RotateByQuaternion({0,0,1},root.rotation);
    Quaternion rotation=QuaternionFromAxisAngle(axis,turns*PI/3);
    for(int bone:spinningBones) {
        Transform &t=asset.sampledPose[bone];
        t.translation=Vector3Add(pivot,Vector3RotateByQuaternion(Vector3Subtract(t.translation,pivot),rotation));
        t.rotation=QuaternionMultiply(rotation,t.rotation);
    }
    asset.update();
}
