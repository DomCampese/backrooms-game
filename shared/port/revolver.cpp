#include "../core/fp_strict.h"
#include "revolver.h"
#include "held.h"
#include <cstring>

namespace {
Vec3 transformPoint(Vec3 v, const GlbPose &t) {
    Vec3 s = { v.x * t.scale.x, v.y * t.scale.y, v.z * t.scale.z };
    Vec3 r = quatRotate(s, t.rotation);
    return { r.x + t.translation.x, r.y + t.translation.y, r.z + t.translation.z };
}
}

bool RevolverModel::load(const uint8_t *data, size_t size, std::string &error) {
    if (!loadGlb(data, size, glb, error)) return false;
    idle = glbClip(glb, "Idle");
    reload = glbClip(glb, "Reload");
    shoot = glbClip(glb, "Shoot");
    handle = glbBone(glb, "DEF_RevolverHandle");
    const int cylinder = glbBone(glb, "DEF_Cylinder");
    if (idle < 0 || reload < 0 || shoot < 0 || handle < 0 || cylinder < 0) {
        error = "the GLB lacks the Idle, Reload or Shoot clip, or the handle or cylinder joint";
        return false;
    }
    spinning.assign(1, cylinder);
    for (size_t i = 0; i < glb.bones.size(); i++)
        if (!strncmp(glb.bones[i].c_str(), "DEF_Bullet", 10) && strncmp(glb.bones[i].c_str(), "DEF_BulletFired", 15))
            spinning.push_back((int)i);
    lastClip = -1;
    pose(0, 0, 6);
    return true;
}

bool RevolverModel::pose(float reloadT, float gunCd, int ammo) {
    const RevolverPose p = revolverPose(reloadT, gunCd, ammo);
    const int clip = p.clip == RevolverClip::Reload ? reload : p.clip == RevolverClip::Shoot ? shoot : idle;
    if (clip == lastClip && p.progress == lastProgress && ammo == lastAmmo) return false;
    lastClip = clip; lastProgress = p.progress; lastAmmo = ammo;
    glbSample(glb, clip, p.progress, current);
    const GlbPose root = current[(size_t)handle];
    muzzle = transformPoint(REVOLVER_MUZZLE, root);
    const Vec3 pivot = transformPoint(REVOLVER_DRUM_PIVOT, root);
    const Vec3 axis = quatRotate({ 0, 0, 1 }, root.rotation);
    const GlbQuat rotation = quatFromAxisAngle(axis, p.drumTurns * 3.14159265358979323846f / 3);
    for (int bone : spinning) {
        GlbPose &t = current[(size_t)bone];
        Vec3 off = { t.translation.x - pivot.x, t.translation.y - pivot.y, t.translation.z - pivot.z };
        Vec3 r = quatRotate(off, rotation);
        t.translation = { pivot.x + r.x, pivot.y + r.y, pivot.z + r.z };
        t.rotation = quatMultiply(rotation, t.rotation);
    }
    return true;
}

void RevolverModel::skinned(int prim, std::vector<float> &pos, std::vector<float> &normal) const {
    glbSkin(glb, current, prim, pos, normal);
}
