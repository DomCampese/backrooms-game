#include "revolver.h"
#include "revolver.generated.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace {
// Explicit little-endian reads: embedded bytes need not be aligned.
struct Reader {
    const unsigned char *p, *end;
    unsigned integer() {
        assert(end-p>=4);
        unsigned v=unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
        p+=4; return v;
    }
    float real() { unsigned u=integer(); float v; std::memcpy(&v,&u,4); return v; }
};
Texture2D texture(const char *type,const unsigned char *bytes,int size) {
    Image image=LoadImageFromMemory(type,bytes,size);
    assert(image.data);
    Texture2D t=LoadTextureFromImage(image); UnloadImage(image);
    GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR); return t;
}
Vector3 transform(const float *m,Vector3 v,float w) {
    return {m[0]*v.x+m[1]*v.y+m[2]*v.z+m[3]*w,
            m[4]*v.x+m[5]*v.y+m[6]*v.z+m[7]*w,
            m[8]*v.x+m[9]*v.y+m[10]*v.z+m[11]*w};
}
}
void Revolver::load() {
    Reader r{revolver_mesh,revolver_mesh+sizeof(revolver_mesh)};
    for(int part=0;part<2;++part) {
        Mesh &m=meshes[part]; m.vertexCount=r.integer(); unsigned indices=r.integer();
        assert(m.vertexCount>0 && m.vertexCount<65536 && indices%3==0);
        m.triangleCount=indices/3;
        m.vertices=(float*)MemAlloc(m.vertexCount*3*sizeof(float));
        m.normals=(float*)MemAlloc(m.vertexCount*3*sizeof(float));
        m.texcoords=(float*)MemAlloc(m.vertexCount*2*sizeof(float));
        m.colors=(unsigned char*)MemAlloc(m.vertexCount*4);
        std::memset(m.colors,255,m.vertexCount*4);
        for(int i=0;i<m.vertexCount;++i) {
            Vertex v{{r.real(),r.real(),r.real()},{r.real(),r.real(),r.real()},0};
            m.texcoords[i*2]=r.real(); m.texcoords[i*2+1]=r.real(); v.joint=r.integer();
            assert(v.joint<17); bind[part].push_back(v);
            std::memcpy(m.vertices+i*3,&v.position,3*sizeof(float));
            std::memcpy(m.normals+i*3,&v.normal,3*sizeof(float));
        }
        m.indices=(unsigned short*)MemAlloc(indices*sizeof(unsigned short));
        assert(r.end-r.p>=indices*2);
        for(unsigned i=0;i<indices;++i) {m.indices[i]=r.p[0]|(unsigned(r.p[1])<<8);r.p+=2;assert(m.indices[i]<m.vertexCount);}
        UploadMesh(&m,true);
    }
    assert(r.p==r.end);
    Reader poses{revolver_poses,revolver_poses+sizeof(revolver_poses)};
    for(auto &clip:clips) {
        unsigned count=poses.integer(); clip.duration=poses.real();
        clip.frames.resize(count);
        for(auto &frame:clip.frames) for(float &v:frame) v=poses.real();
    }
    assert(poses.p==poses.end);
    albedo[0]=texture(".jpg",revolver_gun,sizeof(revolver_gun));
    albedo[1]=texture(".jpg",revolver_ammo,sizeof(revolver_ammo));
    detail[0]=texture(".png",revolver_gun_detail,sizeof(revolver_gun_detail));
    detail[1]=texture(".png",revolver_ammo_detail,sizeof(revolver_ammo_detail));
    pose(0,0,6);
}
void Revolver::pose(float reload,float cooldown,int ammo) {
    int id=0;float time=0;
    if(reload>0) {
        // Reload already opens, ejects, inserts and closes. Appending the separate
        // Open/Close clips snaps it shut twice. Rescale the complete clip to 1.8 s.
        id=1; time=std::clamp(1-reload/1.8f,0.0f,1.0f)*clips[id].duration;
    } else if(cooldown>0) {id=2;time=std::clamp(1-cooldown/.42f,0.0f,1.0f)*clips[id].duration;}
    if(id==lastClip && time==lastTime && ammo==lastAmmo) return;
    lastClip=id;lastTime=time;lastAmmo=ammo;
    const Clip &clip=clips[id];
    float frame=clip.duration>0?std::clamp(time/clip.duration,0.0f,1.0f)*(clip.frames.size()-1):0;
    int a=(int)frame,b=std::min(a+1,(int)clip.frames.size()-1);float f=frame-a;
    std::array<float,17*12> matrices;
    for(unsigned i=0;i<matrices.size();++i) matrices[i]=clip.frames[a][i]*(1-f)+clip.frames[b][i]*f;
    muzzlePosition=transform(matrices.data(),{.302063f,.078805f,0},1);
    // The source Shoot action contains six shots. We play its first shot and
    // retain the accumulated cylinder index instead of snapping its texture back.
    float turns=6-std::clamp(ammo,0,6)-(id==2?1:0);
    if(id==1) turns*=1-std::min(time/.15f,1.0f);
    float angle=turns*PI/3;
    Vector3 pivot=transform(matrices.data(),{.098062f,.078805f,0},1);
    Vector3 axis=Vector3Normalize(transform(matrices.data(),{1,0,0},0));
    for(int part=0;part<2;++part) {
        Mesh &m=meshes[part];
        for(int i=0;i<m.vertexCount;++i) {
            const Vertex &v=bind[part][i];const float *matrix=matrices.data()+v.joint*12;
            Vector3 p=transform(matrix,v.position,1),n=Vector3Normalize(transform(matrix,v.normal,0));
            if(v.joint==4 || part==1) {
                p=Vector3Add(pivot,Vector3RotateByAxisAngle(Vector3Subtract(p,pivot),axis,angle));
                n=Vector3RotateByAxisAngle(n,axis,angle);
            }
            std::memcpy(m.vertices+i*3,&p,3*sizeof(float));std::memcpy(m.normals+i*3,&n,3*sizeof(float));
        }
        UpdateMeshBuffer(m,0,m.vertices,m.vertexCount*3*sizeof(float),0);
        UpdateMeshBuffer(m,2,m.normals,m.vertexCount*3*sizeof(float),0);
    }
}
void Revolver::draw(Material material,Matrix transform) {
    // Borrow the scene shader/occupancy map, then restore shared texture bindings.
    Texture2D diffuse=material.maps[MATERIAL_MAP_DIFFUSE].texture;
    Texture2D specular=material.maps[MATERIAL_MAP_SPECULAR].texture;
    MaterialMap *maps=material.maps;
    for(int part=0;part<2;++part) {
        maps[MATERIAL_MAP_DIFFUSE].texture=albedo[part];
        maps[MATERIAL_MAP_SPECULAR].texture=detail[part];
        DrawMesh(meshes[part],material,transform);
    }
    maps[MATERIAL_MAP_DIFFUSE].texture=diffuse;
    maps[MATERIAL_MAP_SPECULAR].texture=specular;
}
void Revolver::unload() {
    for(int i=0;i<2;++i) {UnloadMesh(meshes[i]);UnloadTexture(albedo[i]);UnloadTexture(detail[i]);}
}
