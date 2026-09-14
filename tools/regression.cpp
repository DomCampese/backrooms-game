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
    // Exercise the imported animation continuously, including its endpoint seam.
    auto vertices = [&]() {
        std::vector<float> result;
        for(const auto &mesh:g.revolver.meshes)
            result.insert(result.end(),mesh.vertices,mesh.vertices+mesh.vertexCount*3);
        return result;
    };
    g.revolver.pose(0,0,6); auto idle=vertices();
    float maxRadius=0;
    for(int frame=0;frame<=180;++frame) {
        g.revolver.pose(1.8f*(1-frame/181.0f),0,0);
        for(const auto &mesh:g.revolver.meshes) for(int v=0;v<mesh.vertexCount;++v) {
            Vector3 p{mesh.vertices[v*3],mesh.vertices[v*3+1],mesh.vertices[v*3+2]};
            assert(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
            maxRadius=fmaxf(maxRadius,Vector3Length(p));
        }
    }
    g.revolver.pose(.000001f,0,0);auto end=vertices();
    for(size_t i=0;i<idle.size();++i) assert(fabsf(idle[i]-end[i])<.001f);
    for(int ammo=0;ammo<6;++ammo) {
        g.revolver.pose(0,.000001f,ammo);auto fired=vertices();
        g.revolver.pose(0,0,ammo);auto resting=vertices();
        for(size_t i=0;i<fired.size();++i) assert(fabsf(fired[i]-resting[i])<.002f);
    }
    assert(maxRadius<.28f); // With the existing 0.48 scale and hold offset, stays inside 0.34 m.
    printf("Imported reload maximum model-space radius: %.4f m\n",maxRadius);
    g.ammo=6;g.weapon=WEAPON_REVOLVER;capture(g,"revolver.png");
    g.ammo=5;g.gunCd=.34f;
    g.recoil=0.7f;g.muzzleT=0.06f;g.muzzleSmoke=0.8f;capture(g,"muzzle.png");
    g.recoil=0;g.gunCd=0;g.muzzleT=0;g.muzzleSmoke=0;g.reloadT=0.9f;capture(g,"reload.png");
    for(int i=1;i<=5;++i) {
        g.reloadT=1.8f*(1-i/6.0f);
        char name[48];snprintf(name,sizeof(name),"reload-%d.png",i);capture(g,name);
    }
    g.reloadT=0;g.ammo=6;g.weapon=WEAPON_FLARE;capture(g,"flare-held.png");
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
    printf("PASS sprint recovery, crouch/stationary gating, restart reset, battery retention; animation continuity; 18 visual captures\n");
    g.shutdown();
}
