#include "../core/fp_strict.h"
#include "mesh_builder.h"
#include <algorithm>
#include <cmath>

void addPropBox(MB &mb, float cx, float cz, float yaw, float hx, float hz, float y0, float y1,
                float u0, float v0, float u1, float v1,
                float tu0, float tv0, float tu1, float tv1, Rgba tint, float bevel) {
    float ca = cosf(yaw), sa = sinf(yaw);
    auto pt = [&](float lx, float lz) { return Vec3{ cx + lx * ca - lz * sa, 0, cz + lx * sa + lz * ca }; };
    bevel=std::min(bevel,std::min(std::min(hx,hz)*0.25f,(y1-y0)*0.25f));
    if (bevel>0.0001f) {
        Vec2 outline[8]={{-hx+bevel,-hz},{hx-bevel,-hz},{hx,-hz+bevel},{hx,hz-bevel},
                            {hx-bevel,hz},{-hx+bevel,hz},{-hx,hz-bevel},{-hx,-hz+bevel}};
        auto topUV=[&](Vec2 p) {return Vec2{tu0+(p.x/hx+1)*0.5f*(tu1-tu0),tv0+(p.y/hz+1)*0.5f*(tv1-tv0)};};
        for(int i=0;i<8;++i) {
            Vec2 a=outline[i],b=outline[(i+1)%8];
            Vec3 aa=pt(a.x,a.y),bb=pt(b.x,b.y);
            Vec3 n{bb.z-aa.z,0,aa.x-bb.x};float len=sqrtf(n.x*n.x+n.z*n.z);n.x/=len;n.z/=len;
            Vec3 at=pt(a.x*(hx-bevel)/hx,a.y*(hz-bevel)/hz);
            Vec3 bt=pt(b.x*(hx-bevel)/hx,b.y*(hz-bevel)/hz);
            mb.quad({aa.x,y0,aa.z},{bb.x,y0,bb.z},{bb.x,y1-bevel,bb.z},{aa.x,y1-bevel,aa.z},n,
                    {u0,v1},{u1,v1},{u1,v0},{u0,v0},tint);
            mb.quad({aa.x,y1-bevel,aa.z},{bb.x,y1-bevel,bb.z},{bt.x,y1,bt.z},{at.x,y1,at.z},
                    {n.x*0.7071f,0.7071f,n.z*0.7071f},topUV(a),topUV(b),topUV(b),topUV(a),tint);
            mb.tri({cx,y1,cz},{at.x,y1,at.z},{bt.x,y1,bt.z},{0,1,0},
                   {(tu0+tu1)*0.5f,(tv0+tv1)*0.5f},topUV(a),topUV(b),tint);
        }
        return;
    }
    Vec3 corners[5] = { pt(-hx, -hz), pt(hx, -hz), pt(hx, hz), pt(-hx, hz), pt(-hx, -hz) };
    for (int f = 0; f < 4; f++) {
        Vec3 a = corners[f], b = corners[f + 1];
        Vec3 n = { b.z - a.z, 0, -(b.x - a.x) };
        float nl = sqrtf(n.x * n.x + n.z * n.z);
        n.x /= nl; n.z /= nl;
        float mx = (a.x + b.x) * 0.5f - cx, mz = (a.z + b.z) * 0.5f - cz;
        if (n.x * mx + n.z * mz < 0) { n.x = -n.x; n.z = -n.z; }
        mb.quad({a.x,y0,a.z},{b.x,y0,b.z},{b.x,y1,b.z},{a.x,y1,a.z}, n,
                {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tint);
    }
    mb.quad({corners[0].x,y1,corners[0].z},{corners[1].x,y1,corners[1].z},
            {corners[2].x,y1,corners[2].z},{corners[3].x,y1,corners[3].z},{0,1,0},
            {tu0,tv0},{tu1,tv0},{tu1,tv1},{tu0,tv1}, tint);
}

void addSolidBox(MB &mb, float x0, float y0, float z0, float x1, float y1, float z1, Rgba t) {
    const Vec2 u = PLAIN_UV;
    mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},u,u,u,u,t);
    mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},u,u,u,u,t);
    mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},u,u,u,u,t);
    mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},u,u,u,u,t);
    mb.quad({x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1},{0,1,0},u,u,u,u,t);
    mb.quad({x0,y0,z1},{x1,y0,z1},{x1,y0,z0},{x0,y0,z0},{0,-1,0},u,u,u,u,t);
}
