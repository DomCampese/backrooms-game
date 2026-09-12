// Native integration checks and reproducible viewmodel captures.
#include "game.h"
#include "raymath.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>

static void capture(Game &g, const char *name) {
    g.updateLook();
    // Fill the complete visible ring; three calls previously left black holes
    // in shots after moving the camera or changing level.
    for (int i=0;i<7;++i) g.streamChunks();
    g.updateOccupancy();
    for (int i=0;i<3;++i) { g.renderScene(4); g.renderUI(4); }
    TakeScreenshot(name);
}
int main() {
    setenv("BACKROOMS_SHOT","test.png",1);
    setenv("BACKROOMS_SEED","1337",1);
    Game g; g.init(); g.frame=80; g.cleanShot=true; g.captureTime=4;
    EnableCursor();
    // Holding sprint empty must give one recovery interval, not frame chatter.
    for (int i=0;i<700;++i) g.updateSprint(true,true,false,1.0f/60);
    bool sawRest=false, sawResume=false;
    g.stamina=0; g.sprintExhausted=false;
    for (int i=0;i<120;++i) {
        g.updateSprint(true,true,false,1.0f/60);
        if (!g.sprinting) sawRest=true;
        if (g.sprinting) { assert(g.stamina>0.24f || sawResume); sawResume=true; }
    }
    assert(sawRest && sawResume);
    g.updateSprint(true,true,true,1.0f/60); assert(!g.sprinting);
    g.updateSprint(true,false,false,1.0f/60); assert(!g.sprinting);
    g.beginDescent(0); assert(g.stamina==1 && !g.sprintExhausted);

    // A full battery does not consume a pickup; revisiting with charge missing does.
    bool testedBattery=false;
    for (int x=-35;x<35 && !testedBattery;++x) for (int z=-35;z<35 && !testedBattery;++z) {
        if (g.pickupAt(x,z)!=Pickup::Battery) continue;
        g.px=x*CELL+1; g.pz=z*CELL+1; g.py=g.world.floorY(x,z);
        uint64_t key=Game::cellKey2(x,z); g.battery=1;
        g.updateInteraction(); assert(!g.taken.count(key));
        g.battery=0.4f; g.updateInteraction();
        assert(g.taken.count(key) && g.battery>0.8f); testedBattery=true;
    }
    assert(testedBattery);
    g.applyLevel(0); g.px=15;g.pz=15;g.py=0;g.eyeY=1.62f;g.yaw=0.8f;g.pitch=0;
    g.weapon=WEAPON_REVOLVER;capture(g,"revolver.png");
    g.recoil=0.7f;g.muzzleT=0.06f;g.muzzleSmoke=0.8f;capture(g,"muzzle.png");
    g.recoil=0;g.muzzleT=0;g.muzzleSmoke=0;g.reloadT=0.9f;capture(g,"reload.png");
    g.reloadT=0;g.weapon=WEAPON_FLARE;capture(g,"flare-held.png");
    g.weapon=WEAPON_DECK;capture(g,"deck-held.png");
    g.drinkT=1;capture(g,"drink.png");g.drinkT=0;
    g.weapon=WEAPON_REVOLVER;
    bool testedWall=false;
    for (int x=4;x<15 && !testedWall;++x) for (int z=4;z<15 && !testedWall;++z) {
        if (g.world.wallWVal(x,z)!=WALL_SOLID || g.world.pillarAt(x,z) || g.world.propAt(x,z)) continue;
        g.px=x*CELL+0.09f+Game::PR;g.pz=z*CELL+1;g.py=0;g.eyeY=1.62f;g.yaw=PI;g.pitch=0;
        capture(g,"wall-clearance.png");testedWall=true;
    }
    assert(testedWall);
    g.chalk.push_back({{g.px,g.py+0.016f,g.pz},g.yaw});
    g.pitch=-0.9f;capture(g,"chalk-arrow.png");
    // Model-space shading must place a dropped deck at the same exposure as the floor.
    g.applyLevel(2);g.px=95;g.pz=79;g.yaw=1.2f;g.pitch=-0.2f;g.weapon=WEAPON_DECK;
    g.deck.carried=false;g.deck.playing=true;g.deck.x=96;g.deck.z=81;g.deck.y=g.world.floorY(cellOf(96),cellOf(81));
    capture(g,"deck-world.png");
    // Inspect several real prop sites rather than relying on the empty spawn room.
    g.deck.carried=true;g.weapon=WEAPON_REVOLVER;g.applyLevel(0);
    const int kinds[]={PROP_COUCH,PROP_DESK,PROP_ARMOIRE,PROP_CABINET};
    for(int kind:kinds) {
        bool found=false;
        for(int x=0;x<70 && !found;++x) for(int z=0;z<70 && !found;++z) {
            if(g.world.propAt(x,z)!=kind) continue;
            g.px=x*CELL+1;g.pz=z*CELL+3.4f;g.py=g.world.floorY(x,z);g.eyeY=g.py+1.35f;
            g.yaw=-PI/2;g.pitch=-0.20f;
            char name[48];snprintf(name,sizeof(name),"prop-%d.png",kind);capture(g,name);found=true;
        }
        assert(found);
    }
    for(const auto &entry:g.world.chunks) for(const auto &mesh:entry.second.meshes)
        assert(mesh.vertexCount<=65535);
    printf("PASS sprint recovery, crouch/stationary gating, restart reset, battery retention; 13 visual captures\n");
    g.shutdown();
}
