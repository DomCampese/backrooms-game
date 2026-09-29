#include "revolver.h"
#include "port/held.h"
#include "vec_rl.h"
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
        if(std::strncmp(asset.boneName(i),"DEF_Bullet",10)==0 &&
           std::strncmp(asset.boneName(i),"DEF_BulletFired",15)!=0)spinningBones.push_back(i);
    pose(0,0,6);
}
void Revolver::pose(float reloadTime,float cooldown,int ammo) {
    // Which clip and where the drum stands are src/port/held.cpp's, which the
    // Unreal build reads too.
    const RevolverPose p=revolverPose(reloadTime,cooldown,ammo);
    const int clip=p.clip==RevolverClip::Reload?reload:p.clip==RevolverClip::Shoot?shoot:idle;
    const float progress=p.progress;
    if(clip==lastClip && progress==lastProgress && ammo==lastAmmo)return;
    lastClip=clip;lastProgress=progress;lastAmmo=ammo;
    asset.sample(clip,progress);
    const Transform root=asset.sampledPose[handle];
    muzzlePosition=transformPoint(toRl(REVOLVER_MUZZLE),root);
    Vector3 pivot=transformPoint(toRl(REVOLVER_DRUM_PIVOT),root);
    Vector3 axis=Vector3RotateByQuaternion({0,0,1},root.rotation);
    Quaternion rotation=QuaternionFromAxisAngle(axis,p.drumTurns*PI/3);
    for(int bone:spinningBones) {
        Transform &t=asset.sampledPose[bone];
        t.translation=Vector3Add(pivot,Vector3RotateByQuaternion(Vector3Subtract(t.translation,pivot),rotation));
        t.rotation=QuaternionMultiply(rotation,t.rotation);
    }
    asset.update();
}
