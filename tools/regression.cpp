// Native integration checks and reproducible viewmodel captures.
#include "game.h"
#include "raymath.h"
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); \
    std::exit(EXIT_FAILURE); } } while (0)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

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
    // These ordinary GLBs use no revolver names, custom formats, or preparation.
    // Exercise disk fallback, static geometry, mixed meshes, and weighted skinning.
    const char *fixtures=getenv("BACKROOMS_TEST_ASSET_DIR");CHECK(fixtures);
    ModelAsset fixture;
    std::string staticPath=std::string(fixtures)+"/static.glb";
    CHECK(fixture.load(staticPath.c_str()));CHECK(fixture.model.meshCount==1);
    CHECK(fixture.animationCount==0);fixture.unload();
    std::string mixedPath=std::string(fixtures)+"/mixed-weighted.glb";
    CHECK(fixture.load(mixedPath.c_str()));CHECK(fixture.model.meshCount==2);
    int translate=fixture.clip("Translate");CHECK(translate>=0);
    fixture.sample(translate,1);fixture.update();
    const Mesh &weighted=fixture.model.meshes[0];
    CHECK(fabsf(weighted.animVertices[0]-.2f)<.0001f);
    CHECK(fabsf(weighted.animVertices[1]-.3f)<.0001f);
    CHECK(fabsf(weighted.animNormals[0])<.0001f && fabsf(weighted.animNormals[1])<.0001f);
    CHECK(fabsf(weighted.animNormals[2]-1)<.0001f);
    CHECK(fixture.model.meshes[1].vertices[0]==0);fixture.unload();
    // Holding sprint empty must give one recovery interval, not frame chatter.
    for (int i=0;i<700;++i) g.updateSprint(true,true,false,1.0f/60);
    bool sawRest=false, sawResume=false;
    g.stamina=0; g.sprintExhausted=false;
    for (int i=0;i<120;++i) {
        g.updateSprint(true,true,false,1.0f/60);
        if (!g.sprinting) sawRest=true;
        if (g.sprinting) { CHECK(g.stamina>0.24f || sawResume); sawResume=true; }
    }
    CHECK(sawRest && sawResume);
    g.updateSprint(true,true,true,1.0f/60); CHECK(!g.sprinting);
    g.updateSprint(true,false,false,1.0f/60); CHECK(!g.sprinting);
    g.beginDescent(0); CHECK(g.stamina==1 && !g.sprintExhausted);

    // A full battery does not consume a pickup; revisiting with charge missing does.
    bool testedBattery=false;
    for (int x=-35;x<35 && !testedBattery;++x) for (int z=-35;z<35 && !testedBattery;++z) {
        if (g.pickupAt(x,z)!=Pickup::Battery) continue;
        g.px=x*CELL+1; g.pz=z*CELL+1; g.py=g.world.floorY(x,z);
        uint64_t key=Game::cellKey2(x,z); g.battery=1;
        g.updateInteraction(); CHECK(!g.taken.count(key));
        g.battery=0.4f; g.updateInteraction();
        CHECK(g.taken.count(key) && g.battery>0.8f); testedBattery=true;
    }
    CHECK(testedBattery);
    g.applyLevel(0); g.px=15;g.pz=15;g.py=0;g.eyeY=1.62f;g.yaw=0.8f;g.pitch=0;
    g.weapon=WEAPON_REVOLVER;g.ammo=3;
    g.updateAim(true,.2f);CHECK(g.aiming && g.aimBlend==1 && !g.canReload());
    capture(g,"iron-sights.png");
    g.gunCd=.36f;g.recoil=.8f;capture(g,"iron-sights-fire.png");g.gunCd=0;g.recoil=0;
    g.updateSprint(true,true,false,.016f);CHECK(!g.sprinting);
    g.updateAim(false,.08f);CHECK(!g.aiming && !g.canReload());
    g.updateAim(false,.08f);CHECK(g.aimBlend==0 && g.canReload());
    g.reloadT=1;g.updateAim(true,.2f);CHECK(!g.aiming && g.aimBlend==0);
    g.reloadT=0;g.weapon=WEAPON_FLARE;g.updateAim(true,.2f);CHECK(!g.aiming);
    g.weapon=WEAPON_REVOLVER;g.paused=true;g.updateAim(true,.2f);CHECK(!g.aiming);
    g.paused=false;g.ammo=6;
    // Exercise the imported animation continuously, including its endpoint seam.
    auto vertices = [&]() {
        std::vector<float> result;
        for(int meshIndex=0;meshIndex<g.revolver.asset.model.meshCount;++meshIndex)
        {
            const Mesh &mesh=g.revolver.asset.model.meshes[meshIndex];
            const float *positions=mesh.animVertices?mesh.animVertices:mesh.vertices;
            result.insert(result.end(),positions,positions+mesh.vertexCount*3);
        }
        return result;
    };
    g.revolver.pose(0,0,6); auto idle=vertices();
    float maxRadius=0;
    for(int frame=0;frame<=180;++frame) {
        g.revolver.pose(1.8f*(1-frame/181.0f),0,0);
        for(int meshIndex=0;meshIndex<g.revolver.asset.model.meshCount;++meshIndex) {
            const Mesh &mesh=g.revolver.asset.model.meshes[meshIndex];
            for(int v=0;v<mesh.vertexCount;++v) {
            Vector3 p{mesh.animVertices[v*3],mesh.animVertices[v*3+1],mesh.animVertices[v*3+2]};
            CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
            maxRadius=fmaxf(maxRadius,Vector3Length(p));
            }
        }
    }
    g.revolver.pose(.000001f,0,0);auto end=vertices();
    for(size_t i=0;i<idle.size();++i) CHECK(fabsf(idle[i]-end[i])<.001f);
    for(int ammo=0;ammo<6;++ammo) {
        g.revolver.pose(0,.000001f,ammo);auto fired=vertices();
        g.revolver.pose(0,0,ammo);auto resting=vertices();
        for(size_t i=0;i<fired.size();++i) CHECK(fabsf(fired[i]-resting[i])<.002f);
    }
    CHECK(maxRadius<.28f); // With the existing 0.48 scale and hold offset, stays inside 0.34 m.
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
        capture(g,"wall-clearance.png");g.updateAim(true,.2f);capture(g,"iron-sights-wall.png");
        g.updateAim(false,.2f);testedWall=true;
    }
    CHECK(testedWall);
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
        CHECK(found);
    }
    for(const auto &entry:g.world.chunks) for(const auto &mesh:entry.second.meshes)
        CHECK(mesh.vertexCount<=65535);
    printf("PASS sprint recovery, crouch/stationary gating, restart reset, battery retention; animation continuity; held aim/reload gating; 21 visual captures\n");
    g.shutdown();
}
