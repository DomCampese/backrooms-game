#include "sim/start.h"
#include "sim/trace.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void require(bool ok,const char *why) { if (!ok) { fprintf(stderr,"FAIL %s\n",why); std::exit(1); } }
static void start(Sim &s,int level,unsigned seed=1337) {
    SimStart config; config.menu=false; config.keepRecords=false; config.seed=seed;
    simBegin(s,config,0); s.applyLevel(level,0); simPlace(s,config);
}
int main() {
    int placed=0;
    for (int level=0;level<2;level++) for (unsigned seed=1;seed<=16;seed++) {
        Sim s; start(s,level,seed); int x=cellOf(s.px),z=cellOf(s.pz),oi,ok;
        if (!s.spawnMannequin(0)) continue;
        placed++;
        require(s.world.pathStep(x,z,cellOf(s.mannequin.pos.x),cellOf(s.mannequin.pos.z),oi,ok),"spawn reachable");
    }
    require(placed>=28,"enough reachable starts");
    Sim s; start(s,1); require(s.spawnMannequin(0),"spawn");
    auto &m=s.mannequin;
    s.px=m.pos.x-0.6f; s.pz=m.pos.z; s.py=m.pos.y; s.eyeY=m.pos.y+1.5f; s.fwd={1,0,0}; s.hurtT=100;
    InputFrame in; in.playing=true; in.observing=true; in.observationCos=0.9f;
    Vec3 pos=m.pos;
    for(int i=0;i<30;i++) s.updateMannequin(in,0.1f,i*0.1);
    require(m.pinned && m.pos.x==pos.x && m.pos.z==pos.z,"viewfinder pins without drift");
    in.observing=false; s.updateMannequin(in,0.1f,3);
    require(!m.pinned && m.pose==1 && m.pos.x==pos.x,"lowering camera changes pose with grace");
    for(int i=0;i<10;i++) s.updateMannequin(in,0.1f,3.1+i*0.1);
    require(m.pos.x!=pos.x || m.pos.z!=pos.z,"moves outside viewfinder");
    m.pos=pos; m.grace=0;
    auto &flare=s.litFlares[0]; flare.active=true; flare.onStorey=true; flare.burn=8;
    flare.x=pos.x; flare.z=pos.z; flare.y=pos.y;
    s.updateMannequin(in,0.1f,5);
    require(m.warded && m.pos.x==pos.x,"flare pins it");
    flare.active=false; in.observing=true; s.fwd={-1,0,0};
    s.updateMannequin(in,0.1f,6);
    require(!m.pinned,"camera pointing away cannot pin");
    m.pos=pos; m.stagger=1.2f; s.updateMannequin(in,0.1f,6.1);
    require(m.pos.x==pos.x,"bullet stagger stops movement");
    double due=m.nextSpawn; s.setPaused(true,7); s.setPaused(false,17);
    require(m.nextSpawn==due+10,"pause shifts spawn clock");
    m.storey=1; s.updateMannequin(in,0.1f,18);
    require(!m.active,"changing storey ends encounter");
    for(int level:{2,3,4}) { start(s,level); require(!s.spawnMannequin(0),"only first two levels"); }
    start(s,0); s.inManila=true; require(!s.spawnMannequin(0),"safe room");
    s.inManila=false; s.inMenu=true; require(!s.spawnMannequin(0),"no title encounter");
    s.inMenu=false; require(s.spawnMannequin(0),"respawn"); s.beginDescent(10); require(!m.active,"restart clears encounter");
    printf("mannequin-check: passed; reachable spawns %d/32\n",placed);
}
