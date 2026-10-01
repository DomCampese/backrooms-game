// Native integration checks and reproducible viewmodel captures.
#include "game.h"
#include "raymath.h"
#include "sfx.h"
struct EmbeddedAsset { const char *path; const unsigned char *data; size_t size; };
#include "sounds.generated.h"
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); \
    std::exit(EXIT_FAILURE); } } while (0)
#define CHECK_NEAR(a,b,eps) do { if (!(std::fabs(double(a)-double(b)) <= (eps))) { \
    std::fprintf(stderr,"FAIL %s:%d: %s (%g) vs %s (%g), |d|>%g\n",__FILE__,__LINE__,#a,(double)(a),#b,(double)(b),(double)(eps)); \
    std::exit(EXIT_FAILURE); } } while (0)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <cstring>


// A tick with nothing pressed, in the window the harness runs in.
static InputFrame noInput(Game &g) {
    InputFrame in;
    in.screenFov = g.baseFov();
    return in;
}
static int captureCount = 0;
static void capture(Game &g, const char *name) {
    ++captureCount;
    g.sim.updateLook(noInput(g));
    // Fill the complete visible ring; three calls previously left black holes
    // in shots after moving the camera or changing level. And the storeys seen
    // through an opening, which stream after your own storey's ring: seven
    // calls filled the ring and left the floor below an atrium black.
    for (int i=0;i<24;++i) g.streamChunks();
    g.updateOccupancy();
    for (int i=0;i<3;++i) { g.renderScene(4); g.renderUI(4); }
    TakeScreenshot(name);
}
int main() {
    setenv("BACKROOMS_SHOT","test.png",1);
    setenv("BACKROOMS_SEED","1337",1);
    Game g; g.init(); g.frame=80; g.cleanShot=true; g.captureTime=4;
    Sim &sim = g.sim;
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
    for (int i=0;i<700;++i) sim.updateSprint(true,true,false,1.0f/60);
    bool sawRest=false, sawResume=false;
    sim.stamina=0; sim.sprintExhausted=false;
    for (int i=0;i<120;++i) {
        sim.updateSprint(true,true,false,1.0f/60);
        if (!sim.sprinting) sawRest=true;
        if (sim.sprinting) { CHECK(sim.stamina>0.24f || sawResume); sawResume=true; }
    }
    CHECK(sawRest && sawResume);
    sim.updateSprint(true,true,true,1.0f/60); CHECK(!sim.sprinting);
    sim.updateSprint(true,false,false,1.0f/60); CHECK(!sim.sprinting);
    sim.beginDescent(0); CHECK(sim.stamina==1 && !sim.sprintExhausted);

    // Poolrooms: deep water floats, dives, resurfaces and releases onto land.
    // Test at three update rates, and exercise real generated floors.
    {
        sim.applyLevel(2,GetTime()); sim.inMenu=false;
        int deepX=0,deepZ=0; bool found=false;
        int wet=0,deep=0;
        for (int x=16;x<48;++x) for (int z=16;z<48;++z) {
            if (sim.world.poolAt(x,z)) ++wet;
            if (sim.world.floorY(x,z)<-2.0f && !sim.world.pillarAt(x,z)) {
                ++deep; deepX=x;deepZ=z;found=true;
            }
        }
        CHECK(found && wet>600 && deep>30);
        for (float hz : {30.0f,60.0f,144.0f}) {
            sim.px=deepX*CELL+1;sim.pz=deepZ*CELL+1;
            sim.py=-0.7f;sim.vy=-3;sim.grounded=false;
            // holding JUMP floats you at the surface
            for (int i=0;i<(int)(hz*6);++i) CHECK(sim.updateSwimming(1/hz,true));
            CHECK_NEAR(sim.py,WATER_Y-1.35f,0.015f);
            CHECK(!sim.grounded && sim.swimming);
            // letting go is the dive: you sink, and the floor stops you
            for (int i=0;i<(int)(hz*3);++i) sim.updateSwimming(1/hz,false);
            CHECK(sim.py+1.62f<WATER_Y-0.5f);
            CHECK(sim.py>=sim.world.floorY(deepX,deepZ));
            for (int i=0;i<(int)(hz*6);++i) sim.updateSwimming(1/hz,true);
            CHECK_NEAR(sim.py,WATER_Y-1.35f,0.02f);
            CHECK(fabsf(sim.vy)<0.03f);
        }
        sim.eyeY=sim.py+1.62f;sim.yaw=0.5f;sim.pitch=0;
        capture(g,"pool-swimming.png");
        sim.py=-2.65f;sim.eyeY=sim.py+1.62f;
        capture(g,"pool-underwater.png");
        sim.px=64;sim.pz=65;sim.py=0;
        CHECK(!sim.updateSwimming(1.0f/60,false));
        sim.ent.st=EState::Chase;sim.fear=0.8f;
        sim.updateEntity(1,100);CHECK(sim.ent.st==EState::Hidden && sim.fear<0.01f);
        sim.sanity=0.4f;sim.updateAmbience(1,100);CHECK(sim.sanity>0.4f);
        // A real movement frame must stop at a submerged riser, then climb
        // it at the surface; a full-height wall must still block both states.
        ChunkData &basin=sim.world.data(4,4);
        memset(basin.wallN,0,sizeof(basin.wallN)); memset(basin.wallW,0,sizeof(basin.wallW));
        memset(basin.pillar,0,sizeof(basin.pillar)); memset(basin.prop,0,sizeof(basin.prop));
        memset(basin.elev,0,sizeof(basin.elev)); memset(basin.pool,0,sizeof(basin.pool));
        basin.pool[5][5]=basin.pool[6][5]=1;
        basin.elev[5][5]=-28;basin.elev[6][5]=-6;
        sim.px=139.95f;sim.pz=139;sim.py=-2.7f;sim.vy=0;sim.velx=4;sim.velz=0;
        sim.grounded=false;sim.swimming=true;sim.updateMovement(noInput(g),0.05f,GetTime());
        CHECK(sim.px<140 && sim.py<-2.5f);
        sim.py=WATER_Y-1.35f;sim.vy=0;sim.velx=4;sim.updateMovement(noInput(g),0.05f,GetTime());
        CHECK(sim.px>140 && sim.grounded && !sim.swimming);
        CHECK_NEAR(sim.py,-0.6f,0.001f);CHECK(sim.swimClimb>0.5f);
        sim.px=141.95f;sim.velx=4;sim.updateMovement(noInput(g),0.05f,GetTime());
        CHECK(sim.px>142 && sim.grounded);CHECK_NEAR(sim.py,0,0.001f);
        basin.wallW[6][5]=WALL_SOLID;
        sim.px=139.5f;sim.py=WATER_Y-1.35f;sim.velx=4;sim.grounded=false;
        sim.updateMovement(noInput(g),0.05f,GetTime());CHECK(sim.px<139.6f);
        sim.applyLevel(0,GetTime());sim.beginDescent(0);
    }

    // A full battery does not consume a pickup; revisiting with charge missing does.
    bool testedBattery=false;
    for (int x=-35;x<35 && !testedBattery;++x) for (int z=-35;z<35 && !testedBattery;++z) {
        if (sim.pickupAt(x,z)!=Pickup::Battery) continue;
        sim.px=x*CELL+1; sim.pz=z*CELL+1; sim.py=sim.world.floorY(x,z);
        uint64_t key=Sim::cellKey2(x,z); sim.battery=1;
        sim.updateInteraction(noInput(g)); CHECK(!sim.taken.count(key));
        sim.battery=0.4f; sim.updateInteraction(noInput(g));
        CHECK(sim.taken.count(key) && sim.battery>0.8f); testedBattery=true;
    }
    CHECK(testedBattery);
    sim.applyLevel(0,GetTime()); sim.px=15;sim.pz=15;sim.py=0;sim.eyeY=1.62f;sim.yaw=0.8f;sim.pitch=0;
    sim.weapon=WEAPON_REVOLVER;sim.ammo=3;
    sim.updateAim(true,.2f);CHECK(sim.aiming && sim.aimBlend==1 && !sim.canReload());
    capture(g,"iron-sights.png");
    sim.gunCd=SHOT_INTERVAL*0.86f;sim.recoil=.8f;capture(g,"iron-sights-fire.png");sim.gunCd=0;sim.recoil=0;
    sim.updateSprint(true,true,false,.016f);CHECK(!sim.sprinting);
    sim.updateAim(false,.08f);CHECK(!sim.aiming && !sim.canReload());
    sim.updateAim(false,.08f);CHECK(sim.aimBlend==0 && sim.canReload());
    sim.reloadT=1;sim.updateAim(true,.2f);CHECK(!sim.aiming && sim.aimBlend==0);
    sim.reloadT=0;sim.weapon=WEAPON_FLARE;sim.updateAim(true,.2f);CHECK(!sim.aiming);
    sim.weapon=WEAPON_REVOLVER;sim.paused=true;sim.updateAim(true,.2f);CHECK(!sim.aiming);
    sim.paused=false;sim.ammo=6;
    // Portrait FOV lock. raylib derives fovX from fovy*aspect, so these numbers
    // are the whole contract: every window at or wider than the authored
    // 1440x850 must be untouched (the regression sweeps compare those frames),
    // and a portrait phone must not stay at the ~36-degree keyhole the fixed
    // 70 made on a 0.46 screen.
    {
        auto fovX=[&](int w,int h,float aim){
            return 2*atanf(tanf(windowFovY(w,h,aim)*DEG2RAD*0.5f)*(float)w/h)*RAD2DEG;};
        CHECK_NEAR(windowFovY(1440,850,0),70,0.01f);    // authored window
        CHECK_NEAR(fovX(1440,850,0),fovX(390,844,0),0.01f);     // portrait locks fovX
        CHECK(fovX(390,844,0)>55);                              // was ~36 before
        CHECK(windowFovY(1920,1080,0)==70);             // wide: untouched
        CHECK(windowFovY(3440,1440,0)==70);             // ultrawide: untouched
        CHECK(windowFovY(0,0,0)==70);                   // no window yet
        // The action terms ride on top of the base, and the smoothing still
        // eases: at the authored window one dt step toward aim moves fov from
        // 70 down toward 62 (never instantly), and releasing walks it back up.
        // The targets themselves are fixed numbers in updateMovement and the
        // sweep frames above pin the base; this pins that they still compose.
        sim.aimBlend=1;sim.sprinting=false;sim.slide=0;sim.px=15;sim.pz=15;sim.py=0;
        sim.updateMovement(noInput(g),1.0f/60,GetTime());CHECK(sim.fov<69.5f && sim.fov>62.0f);
        sim.aimBlend=0;sim.updateMovement(noInput(g),1.0f/60,GetTime());CHECK(sim.fov>68.0f && sim.fov<70.0f);   // easing back up
        // Sprint is driven through the InputFrame at the end of the harness.
    }
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
    for (float t : {0.2f,0.45f,0.85f}) {
        g.revolver.pose(1.8f*(1-t),0,0);
        int live=0,spent=0;
        for (int i=0;i<g.revolver.asset.boneCount();++i) {
            const char *name=g.revolver.asset.boneName(i);
            if (strncmp(name,"DEF_Bullet",10)!=0) continue;
            Vector3 scale=g.revolver.asset.sampledPose[i].scale;
            CHECK(scale.x<0.01f || fabsf(scale.x-1)<0.001f);
            if (scale.x>0.9f) {
                if (strncmp(name,"DEF_BulletFired",15)==0) ++spent; else ++live;
            }
        }
        CHECK((t<0.5f && spent==6 && live==0) || (t>0.8f && live==6 && spent==0));
    }
    // Assert the rule, not a proxy for it: the viewmodel has to stay inside the
    // 0.34 m collision radius, and the hold offset and scale in drawHeldWeapon
    // are what turn a model-space radius into that distance. The bare
    // maxRadius<.28f this replaces had no stated margin, and raylib 6.0's
    // UpdateModelAnimation interpolates where 5.5's did not, so the sampled
    // reload reaches ~5% further at its extreme — 0.2942 m, which tripped the
    // proxy while the actual clearance was still 44 mm. Failing on a rule the
    // code does not have is worse than not checking, because the next person
    // relaxes the number instead of reading it.
    CHECK(0.155f + maxRadius * 0.48f < 0.34f);
    printf("Imported reload maximum model-space radius: %.4f m\n",maxRadius);
    sim.ammo=6;sim.weapon=WEAPON_REVOLVER;capture(g,"revolver.png");
    {
        float savedPitch=sim.pitch;
        sim.pitch=-0.5f; capture(g,"bullet-before.png");
        sim.fireBullet(); sim.updateBullets(0.05f);
        CHECK(!sim.bulletImpacts.empty());
        capture(g,"bullet-impact.png");
        sim.bullets.clear(); sim.bulletImpacts.clear(); sim.pitch=savedPitch;
        sim.updateSqueeze(true,0.2f); capture(g,"squeeze.png");
        sim.squeezing=false; sim.squeezeBlend=0;
    }
    sim.ammo=5;sim.gunCd=SHOT_INTERVAL*0.81f;
    sim.recoil=0.7f;sim.muzzleT=0.06f;sim.muzzleSmoke=0.8f;capture(g,"muzzle.png");
    sim.recoil=0;sim.gunCd=0;sim.muzzleT=0;sim.muzzleSmoke=0;sim.reloadT=0.9f;capture(g,"reload.png");
    for(int i=1;i<=5;++i) {
        sim.reloadT=1.8f*(1-i/6.0f);
        char name[48];snprintf(name,sizeof(name),"reload-%d.png",i);capture(g,name);
    }
    sim.reloadT=0;sim.ammo=6;sim.weapon=WEAPON_FLARE;capture(g,"flare-held.png");
    sim.weapon=WEAPON_DECK;capture(g,"deck-held.png");
    sim.drinkT=1;capture(g,"drink.png");sim.drinkT=0;
    sim.weapon=WEAPON_REVOLVER;
    bool testedWall=false;
    for (int x=4;x<15 && !testedWall;++x) for (int z=4;z<15 && !testedWall;++z) {
        if (sim.world.wallWVal(x,z)!=WALL_SOLID || sim.world.pillarAt(x,z) || sim.world.propAt(x,z)) continue;
        sim.px=x*CELL+0.09f+Sim::PR;sim.pz=z*CELL+1;sim.py=0;sim.eyeY=1.62f;sim.yaw=PI;sim.pitch=0;
        capture(g,"wall-clearance.png");sim.updateAim(true,.2f);capture(g,"iron-sights-wall.png");
        sim.updateAim(false,.2f);testedWall=true;
    }
    CHECK(testedWall);
    // Chalk is kept per level (Sim::chalk is indexed by level).
    sim.chalk[sim.level].push_back({{sim.px,sim.py+0.016f,sim.pz},sim.yaw});
    sim.pitch=-0.9f;capture(g,"chalk-arrow.png");
    // Model-space shading must place a dropped deck at the same exposure as the floor.
    sim.applyLevel(2,GetTime());sim.px=95;sim.pz=79;sim.yaw=1.2f;sim.pitch=-0.2f;sim.weapon=WEAPON_DECK;
    sim.deck.carried=false;sim.deck.playing=true;sim.deck.x=96;sim.deck.z=81;sim.deck.y=sim.world.floorY(cellOf(96),cellOf(81));
    capture(g,"deck-world.png");
    // Inspect several real prop sites rather than relying on the empty spawn room.
    // Level 0 is barren now (lore: "randomly segmented empty rooms"), so each
    // kind is looked for on a level that still furnishes it.
    sim.deck.carried=true;sim.weapon=WEAPON_REVOLVER;
    const int kinds[]={PROP_COUCH,PROP_BOXES,PROP_ARMOIRE,PROP_CABINET};
    const int kindLevel[]={4,0,3,1};
    for(int ki=0;ki<4;++ki) {
        int kind=kinds[ki];
        sim.applyLevel(kindLevel[ki],GetTime());
        bool found=false;
        for(int x=0;x<70 && !found;++x) for(int z=0;z<70 && !found;++z) {
            if(sim.world.propAt(x,z)!=kind) continue;
            sim.px=x*CELL+1;sim.pz=z*CELL+3.4f;sim.py=sim.world.floorY(x,z);sim.eyeY=sim.py+1.35f;
            sim.yaw=-PI/2;sim.pitch=-0.20f;
            char name[48];snprintf(name,sizeof(name),"prop-%d.png",kind);capture(g,name);found=true;
        }
        CHECK(found);
    }
    for(const auto &entry:sim.world.chunks) {
        const ChunkMeshes *baked=g.chunkMeshes.find(sim.world,sim.world.storey,
                                                    (int32_t)(entry.first>>32),(int32_t)entry.first);
        if(baked) for(const auto &mesh:baked->meshes) CHECK(mesh.vertexCount<=65535);
    }
    // ---- Level 0 lore. Assert the rules, not the pictures: nothing on the
    // level but cartons and fallen ceiling, no windows anywhere, and the
    // Manila Room exactly as the article has it — 8x8 m, a door on each
    // wall, a table in the middle, a wooden floor and no tubes over it.
    {
        sim.applyLevel(0,GetTime());
        int bad=0, windows=0, manilas=0;
        for(int x=-80;x<80;++x) for(int z=-80;z<80;++z) {
            uint8_t pk=sim.world.propAt(x,z);
            if(pk && pk!=PROP_BOXES && pk!=PROP_FALLEN_TILE && pk!=PROP_MANILA_TABLE) bad++;
            if(sim.world.wallNVal(x,z)==WALL_WINDOW || sim.world.wallWVal(x,z)==WALL_WINDOW) windows++;
        }
        for(auto &entry:sim.world.chunks) manilas+=entry.second.manila;
        printf("Level 0: %d furnished cells, %d windows, %d Manila Rooms in %zu chunks\n",
               bad, windows, manilas, sim.world.chunks.size());
        CHECK(bad==0); CHECK(windows==0); CHECK(manilas>0);
        sim.world.manilaTest=true; sim.applyLevel(0,GetTime());          // one pinned east of spawn
        ChunkData &md=sim.world.data(1,0);
        CHECK(md.manila);
        int doors=0;
        for(int t=MANILA_LO;t<=MANILA_HI;++t) {
            doors+=(md.wallN[t][MANILA_LO]==WALL_DOOR)+(md.wallN[t][MANILA_HI+1]==WALL_DOOR)
                  +(md.wallW[MANILA_LO][t]==WALL_DOOR)+(md.wallW[MANILA_HI+1][t]==WALL_DOOR);
            CHECK(md.elev[t][MANILA_LO]==0);
        }
        CHECK(doors==4);
        CHECK(md.prop[7][7]==PROP_MANILA_TABLE);
        CHECK(sim.world.manilaAt(16+MANILA_LO,MANILA_LO) && !sim.world.manilaAt(16+MANILA_LO-1,MANILA_LO));
        float rx=0,rz=0;
        CHECK(sim.world.manilaNear(48,16,rx,rz)); CHECK(rx==48.0f && rz==16.0f);
        // no tube inside its walls: the chandelier lights it (shader mask +
        // lightAtCPU mirror agree once Game sets the extras)
        float mask[4]={rx-4,rz-4,rx+4,rz+4}, lamp[4]={0,0,0,0};
        setLightExtrasCPU(mask,lamp);
        const LevelCfg &c0=LEVELS[0];
        float inRoom=lightAtCPU(rx+2,1.0f,rz+2,1.0f,c0.ls,c0.wallH-0.12f,0.0f,c0.lightMul,0.04f);
        float mask0[4]={1e6f,1e6f,-1e6f,-1e6f};
        setLightExtrasCPU(mask0,lamp);
        float unmasked=lightAtCPU(rx+2,1.0f,rz+2,1.0f,c0.ls,c0.wallH-0.12f,0.0f,c0.lightMul,0.04f);
        printf("Manila Room light at a corner seat: %.3f masked vs %.3f with tubes\n", inRoom, unmasked);
        // The rule is "the room's own tubes are out". lightAtCPU has no walls,
        // so the tubes outside it still reach this seat and the tone curve
        // compresses the rest: measured 0.768 against 0.906. Assert a clear
        // drop with margin rather than a ratio the CPU model cannot produce.
        CHECK(inRoom < unmasked - 0.08f);
        sim.px=44.8f;sim.pz=18.6f;sim.py=0;sim.eyeY=1.62f;sim.yaw=-0.45f;sim.pitch=0;
        sim.updateManila(0.0f,GetTime());
        CHECK(sim.manilaNear && sim.inManila);
        capture(g,"manila-room.png");
        { // the HUD layer: a note held up to read, then the hum's migraine at full throb
          double rs=sim.runStart; sim.runStart=-60; g.cleanShot=false;
          sim.noteT=5; sim.notePage=1; capture(g,"manila-note.png"); sim.noteT=0;
          sim.migraine=1; capture(g,"migraine.png"); sim.migraine=0;
          g.cleanShot=true; sim.runStart=rs; }
        sim.world.manilaTest=false;
    }
    // ---- Level 1 lore. Puddles "in inconsistent areas" (a few percent of the
    // slab, not a sheen on all of it), and supply crates that are there, keep
    // out of doorways, "appear and disappear" when a blackout ends, and open.
    {
        sim.applyLevel(1,GetTime());
        int wet=0, n=0;
        for(float x=-80;x<80;x+=1.37f) for(float z=-80;z<80;z+=1.41f) { n++; wet += carpetWetCPU(x,z,LEVELS[1].wetFrom)>0.5f; }
        float pct=100.0f*wet/n;
        CHECK(LEVELS[1].gloss<=0.10f);          // below the shader's specular cut: no blanket sheen
        std::vector<uint64_t> a0;
        for(int a=-40;a<40;++a) for(int b=-40;b<40;++b) if(sim.crateAt(a,b)) {
            a0.push_back(Sim::cellKey2(a,b));
            const uint8_t e[4]={sim.world.wallNVal(a,b),sim.world.wallNVal(a,b+1),sim.world.wallWVal(a,b),sim.world.wallWVal(a+1,b)};
            for(uint8_t w:e) CHECK(w!=WALL_DOOR && w!=WALL_EXIT && w!=WALL_LOCKED);
        }
        sim.crateEpoch++;
        int same=0, after=0;
        for(int a=-40;a<40;++a) for(int b=-40;b<40;++b) if(sim.crateAt(a,b)) {
            after++;
            for(uint64_t k:a0) same += k==Sim::cellKey2(a,b);
        }
        printf("Level 1: puddles on %.1f%% of the floor; %zu crates, %d after a blackout, %d in the same place\n",
               pct, a0.size(), after, same);
        CHECK(pct>0.5f && pct<7.0f);
        CHECK(!a0.empty() && after>0 && same*4<(int)a0.size());
        sim.crateEpoch--;                         // back to the epoch a0 was taken in
        int ca=(int)(uint32_t)(a0[0]>>32), cb=(int)(uint32_t)(a0[0]&0xFFFFFFFFu);
        sim.deckNoteT=0;
        sim.openCrate(ca,cb);
        CHECK(sim.cratesOpened.count(Sim::cellKey2(ca,cb))==1 && sim.deckNoteT>0);
        sim.px=ca*CELL+1.0f; sim.pz=cb*CELL+3.2f; sim.py=0; sim.eyeY=1.62f; sim.yaw=-PI/2; sim.pitch=-0.35f;
        capture(g,"level1-crate.png");
    }
    // ---- step height. The generator relaxes every terrace to within MAX_STEP,
    // so nothing it produces exercises the riser blocker; a rule that never
    // fires is not a rule that works, so force a drop and check it directly.
    {
        sim.applyLevel(0,GetTime());
        int ci=24, ck=24;                       // inside chunk (1,1), clear of the spawn room
        int cx=fdiv(ci,CCELLS), cz=fdiv(ck,CCELLS);
        ChunkData &cd=sim.world.data(cx,cz);
        int li=ci-cx*CCELLS, lk=ck-cz*CCELLS;
        cd.wallN[li][lk]=cd.wallW[li][lk]=cd.wallN[li][lk+1]=cd.wallW[li+1][lk]=0;
        cd.pillar[li][lk]=cd.prop[li][lk]=cd.pool[li][lk]=0;
        cd.elev[li][lk]=25;                     // a 2.5 m terrace, the atrium's full depth
        CHECK(sim.world.floorY(ci,ck)>MAX_STEP);
        // it emits a blocker topping out at its own floor
        AABB boxes[MAX_NEARBY_AABBS]; int n=sim.world.gatherCellAABBs(ci,ck,boxes,MAX_NEARBY_AABBS,0);
        bool riser=false;
        for(int i=0;i<n;++i) if(fabsf(boxes[i].top-sim.world.floorY(ci,ck))<0.001f) riser=true;
        CHECK(riser);
        // from below you are stopped at the face; from on top you walk over it
        float bx=ci*CELL+1, bz=ck*CELL-0.2f, ox=bx, oz=bz;
        sim.world.collideCircle(bx,bz,Sim::PR,0.0f);
        CHECK(bz<oz-0.01f && fabsf(bx-ox)<0.5f);
        bx=ci*CELL+1; bz=ck*CELL+1; ox=bx; oz=bz;
        sim.world.collideCircle(bx,bz,Sim::PR,sim.world.floorY(ci,ck));
        CHECK(fabsf(bx-ox)<0.001f && fabsf(bz-oz)<0.001f);
        // and no route crosses it, so the pack has to go round rather than through
        CHECK(!sim.world.canStep(ci,ck-1,ci,ck) && !sim.world.canStep(ci,ck,ci,ck-1));
        cd.elev[li][lk]=0;
    }

    // ---- a doorway has jambs, and a locked door is a wall.
    //
    // Three separate things decide whether you can walk through a cell edge —
    // blocksEdge, the jamb boxes, and the mesher that draws them — and they are
    // written in different places. Force each case rather than hunting for one
    // in the map: a rule that depends on the generator happening to produce a
    // shape is a rule that stops being tested the day it stops producing it.
    {
        sim.applyLevel(0,GetTime());
        int ci=28, ck=28;                        // clear of the spawn room
        int cx=fdiv(ci,CCELLS), cz=fdiv(ck,CCELLS);
        ChunkData &cd=sim.world.data(cx,cz);
        int li=ci-cx*CCELLS, lk=ck-cz*CCELLS;
        // Snapshot before clearing. unlockEdge rebakes the chunk, so anything
        // left flattened here is flattened in every capture taken afterwards —
        // a harness that quietly edits the world it is also photographing.
        struct Saved { uint8_t wn, ww, wn1, pi, pr, pi1, pr1; } saved[4];
        for (int d=-1;d<=2;++d) {
            Saved &s=saved[d+1];
            s.wn=cd.wallN[li+d][lk]; s.ww=cd.wallW[li+d][lk]; s.wn1=cd.wallN[li+d][lk+1];
            s.pi=cd.pillar[li+d][lk];   s.pr=cd.prop[li+d][lk];
            s.pi1=cd.pillar[li+d][lk-1];s.pr1=cd.prop[li+d][lk-1];
        }
        for (int d=-1;d<=2;++d) {                // clear the neighbourhood
            cd.pillar[li+d][lk]=cd.prop[li+d][lk]=0;
            cd.pillar[li+d][lk-1]=cd.prop[li+d][lk-1]=0;
            cd.wallN[li+d][lk]=cd.wallW[li+d][lk]=cd.wallN[li+d][lk+1]=WALL_NONE;
        }
        auto boxesAt=[&](int a,int b){ static AABB bx[MAX_NEARBY_AABBS];
            return sim.world.gatherCellAABBs(a,b,bx,MAX_NEARBY_AABBS,0); };
        float z0=ck*CELL, x0=ci*CELL;

        // A doorway: you go through the 1.3 m opening and not through the jambs.
        cd.wallN[li][lk]=WALL_DOOR;
        CHECK(!blocksEdge(sim.world.wallNVal(ci,ck)));          // passable
        CHECK(boxesAt(ci,ck)==2);                             // exactly its two jambs
        float bx=x0+1.0f, bz=z0-0.25f, oz=bz;                 // dead centre of the opening
        sim.world.collideCircle(bx,bz,Sim::PR,0.0f);
        CHECK(fabsf(bz-oz)<0.001f);                           // walks straight through
        bx=x0+0.1f; bz=z0-0.25f; oz=bz;                       // into the west jamb
        sim.world.collideCircle(bx,bz,Sim::PR,0.0f);
        CHECK(bz<oz-0.01f);                                   // pushed back out

        // Two side by side are one wide opening: the mesher drops the jamb
        // between them, so the collision has to drop it too or you walk into a
        // pier that is not there.
        cd.wallN[li+1][lk]=WALL_DOOR;
        CHECK(boxesAt(ci,ck)==1 && boxesAt(ci+1,ck)==1);      // outer jambs only
        bx=x0+CELL; bz=z0-0.25f; oz=bz;                       // the line between them
        sim.world.collideCircle(bx,bz,Sim::PR,0.0f);
        CHECK(fabsf(bz-oz)<0.001f);
        cd.wallN[li+1][lk]=WALL_NONE;

        // A locked door is a wall until its key turns, and it stops light too.
        cd.wallN[li][lk]=WALL_LOCKED;
        CHECK(blocksEdge(sim.world.wallNVal(ci,ck)) && blocksLight(sim.world.wallNVal(ci,ck)));
        CHECK(!sim.world.canStep(ci,ck-1,ci,ck));
        bx=x0+1.0f; bz=z0-0.25f; oz=bz;                       // the middle is solid now
        sim.world.collideCircle(bx,bz,Sim::PR,0.0f);
        CHECK(bz<oz-0.01f);
        sim.world.unlockEdge(ci,ck,false);
        CHECK(sim.world.wallNVal(ci,ck)==WALL_DOOR);            // and opens for good
        CHECK(sim.world.canStep(ci,ck-1,ci,ck));
        bx=x0+1.0f; bz=z0-0.25f; oz=bz;
        sim.world.collideCircle(bx,bz,Sim::PR,0.0f);
        CHECK(fabsf(bz-oz)<0.001f);
        // ...and the *geometry* has to move with it. This is the half that was
        // actually broken: the mesher read the raw wall array, so an unlocked
        // door went on drawing its locked leaf over an edge you could now walk
        // through, and a shifted doorway kept its opening on screen over an
        // edge that had been sealed. Neither reads as a mesher bug — the first
        // reads as "doors have no collision". Bake the chunk, change what the
        // overlays say the wall is, bake again, and the wall mesh must differ.
        sim.world.unlockedDoors.clear();           // start from a clean overlay
        cd.wallN[li][lk]=WALL_NONE;
        auto verts=[&](int slot){ g.chunkMeshes.ensure(sim.world,cx,cz);
            return g.chunkMeshes.find(sim.world,sim.world.qs,cx,cz)->meshes[slot].vertexCount; };
        sim.world.rebuildChunk(cx,cz);
        int openVerts=verts(MESH_WALLS);
        sim.world.shiftEdge(ci,ck,false);          // the building closes it behind you
        CHECK(verts(MESH_WALLS) > openVerts);    // a wall appeared
        sim.world.shifted.clear();
        // The locked and open branches emit the *same* three wall boxes — the
        // jambs and the header are identical, and what differs is the leaf, its
        // handle and the threshold strip, all of which go into MESH_PROPS. So
        // watch the props slot here: asserted against MESH_WALLS this passes
        // whatever the mesher does, which is the useless kind of green.
        cd.wallN[li][lk]=WALL_LOCKED;
        sim.world.rebuildChunk(cx,cz);
        int lockedProps=verts(MESH_PROPS);
        sim.world.unlockEdge(ci,ck,false);         // and it rebakes both chunks itself
        CHECK(verts(MESH_PROPS) < lockedProps);  // the leaf went

        sim.world.unlockedDoors.clear();
        for (int d=-1;d<=2;++d) {                // put the neighbourhood back
            Saved &s=saved[d+1];
            cd.wallN[li+d][lk]=s.wn; cd.wallW[li+d][lk]=s.ww; cd.wallN[li+d][lk+1]=s.wn1;
            cd.pillar[li+d][lk]=s.pi;    cd.prop[li+d][lk]=s.pr;
            cd.pillar[li+d][lk-1]=s.pi1; cd.prop[li+d][lk-1]=s.pr1;
        }
        sim.world.rebuildChunk(cx,cz);
    }

    // Projectiles cross the whole frame segment, stop at geometry, and choose
    // the nearest actor rather than the first actor in the array.
    {
        Game p;
        for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z)
            p.sim.world.chunks[World::key(x,z)]={};
        Mesh &wall=p.chunkMeshes.slot(0,0,0).meshes[MESH_WALLS];
        wall.vertexCount=6; wall.triangleCount=2;
        wall.vertices=(float *)MemAlloc(18*sizeof(float));
        const float v[]={4,0,0, 4,3,2, 4,3,0, 4,0,0, 4,0,2, 4,3,2};
        memcpy(wall.vertices,v,sizeof(v));
        p.sim.px=1; p.sim.pz=1; p.sim.eyeY=0.6f; p.sim.fwd={1,0,0};
        p.sim.dogs[0].st=DState::Prowl; p.sim.dogs[0].x=6; p.sim.dogs[0].z=1; p.sim.dogs[0].hp=3;
        p.sim.fireBullet(); p.sim.updateBullets(0.1f);
        CHECK(p.sim.bullets.size()==1 && p.sim.bullets[0].remaining==0);
        CHECK_NEAR(p.sim.bullets[0].pos.x,4,0.001f);
        CHECK(p.sim.dogs[0].hp==3 && p.sim.bulletImpacts.size()==1);
        p.sim.updateBullets(0.3f); CHECK(p.sim.bullets.empty() && p.sim.bulletImpacts.empty());
        MemFree(wall.vertices); wall={};
        p.sim.dogs[1]=p.sim.dogs[0]; p.sim.dogs[1].x=3;
        p.sim.ent.st=EState::Stalk; p.sim.ent.x=8; p.sim.ent.z=1; p.sim.ent.hp=3;
        p.sim.fireBullet(); p.sim.updateBullets(0.1f);
        CHECK(p.sim.dogs[1].hp==2 && p.sim.dogs[0].hp==3 && p.sim.ent.hp==3);
        p.sim.bullets.clear(); p.sim.bulletImpacts.clear();
        p.sim.fwd=normalize({1,2,0}); p.sim.fireBullet(); p.sim.updateBullets(0.1f);
        CHECK(p.sim.dogs[1].hp==2 && p.sim.dogs[0].hp==3 && p.sim.ent.hp==3);
        p.sim.bullets.clear(); p.sim.fwd={1,0,0}; p.sim.fireBullet(); p.sim.updateBullets(0.001f);
        CHECK_NEAR(p.sim.bullets[0].pos.x,1.22f,0.001f); // finite travel, not hitscan
        p.sim.bullets.clear(); p.sim.dogs[0].st=p.sim.dogs[1].st=DState::Gone; p.sim.ent.st=EState::Hidden;
        p.sim.fireBullet(); p.sim.updateBullets(1); p.sim.updateBullets(1); CHECK(p.sim.bullets.empty());
        p.sim.world.chunks[World::key(0,0)].wallW[0][0]=WALL_SOLID;
        p.sim.px=0.30f; p.sim.py=0;
        p.sim.updateSqueeze(true,0.1f); CHECK(p.sim.squeezing);
        float x=p.sim.px,z=p.sim.pz; p.sim.world.collideCircle(x,z,0.12f,0);
        CHECK_NEAR(x,p.sim.px,0.001f);
        p.sim.updateSqueeze(false,0.1f); CHECK(p.sim.squeezing); // no room to expand
        CHECK_NEAR(p.sim.px,0.30f,0.001f);
        p.sim.px=1; p.sim.updateSqueeze(false,0.1f); CHECK(!p.sim.squeezing);
    }
    {
        Pcm pcm=gulpPcm();
        int peak=0, jump=0;
        for (size_t i=1;i<pcm.size();++i) {
            peak=std::max(peak,std::abs((int)pcm[i]));
            jump=std::max(jump,std::abs((int)pcm[i]-pcm[i-1]));
        }
        CHECK(peak>500 && peak<8000 && jump<1500);
        CHECK(pcm[0]==0 && pcm.back()==0);
        Wave w={}; w.frameCount=(unsigned)pcm.size(); w.sampleRate=SAMPLE_RATE; w.sampleSize=16; w.channels=1;
        w.data=pcm.data(); ExportWave(w,"drinking.wav");
    }

    // ---- the catch ends the run, and only out of a committed lunge. The bare
    // proximity test this replaced fired the instant you came inside 1.25 m,
    // silently — survivable when being caught was free, unfair once it is not.
    {
        sim.applyLevel(0,GetTime()); sim.beginDescent(0);
        sim.inMenu=false; sim.deathT=0; sim.hidden=false; sim.deathCount=0;
        sim.px=40; sim.pz=40; sim.py=0; sim.yaw=0; sim.pitch=0; sim.updateLook(noInput(g));
        sim.ent.st=EState::Chase; sim.ent.hp=3; sim.ent.stagger=0; sim.ent.dispY=0;
        // well inside reach, but he has not committed and cannot yet
        sim.ent.x=sim.px+1.0f; sim.ent.z=sim.pz; sim.ent.lunge=0; sim.ent.lungeCd=5.0f;
        sim.updateEntity(0.001f, 100.0);
        CHECK(!sim.inMenu && sim.deathT<=0);
        // from the tell's range with the cooldown clear, he commits rather than grabbing
        sim.ent.st=EState::Chase; sim.ent.x=sim.px+2.3f; sim.ent.z=sim.pz;
        sim.ent.lunge=0; sim.ent.lungeCd=0;
        sim.updateEntity(0.001f, 100.0);
        CHECK(sim.ent.lunge>0 && !sim.inMenu);
        // a landed commit costs health, not the run: you are shoved clear, get a
        // moment of immunity, and he reels from the swing
        sim.health=1; sim.hurtT=0;
        sim.ent.x=sim.px+1.0f; sim.ent.z=sim.pz; sim.ent.lunge=Sim::LUNGE_TIME;
        sim.updateEntity(0.001f, 100.0);
        CHECK(!sim.inMenu && sim.deathT<=0 && fabsf(sim.health-(1-Sim::ENTITY_HIT))<1e-4f);
        CHECK(sim.hurtT>0 && sim.ent.lunge<=0 && sim.ent.stagger>0 && sim.velx<0);
        // inside the grace a second commit cannot land
        sim.ent.x=sim.px+1.0f; sim.ent.z=sim.pz; sim.ent.lunge=Sim::LUNGE_TIME;
        sim.updateEntity(0.001f, 100.0);
        CHECK(fabsf(sim.health-(1-Sim::ENTITY_HIT))<1e-4f);
        // health regenerates only after REGEN_DELAY without being touched
        float h0=sim.health;
        sim.updateHealth(Sim::REGEN_DELAY-0.5f);
        CHECK(sim.health==h0 && sim.hurtT<=0);
        sim.updateHealth(1.0f);
        CHECK(sim.health>h0 && sim.health<1);
        for (int i=0;i<60;++i) sim.updateHealth(1.0f);
        CHECK(sim.health==1);
        // one hit is survivable, the second is not: from the one-hit-down state a
        // landed commit ends the run, with the card's numbers frozen
        sim.health=1-Sim::ENTITY_HIT; sim.hurtT=0;
        sim.ent.x=sim.px+1.0f; sim.ent.z=sim.pz; sim.ent.lunge=Sim::LUNGE_TIME;
        sim.distWalked=250; sim.killCount=2;
        sim.updateEntity(0.001f, 100.0);
        // deathCount must survive the beginDescent inside dieRun: it counts the
        // runs this session has cost you, and resetting it there made the card
        // report your first death every single time.
        CHECK(sim.inMenu && sim.deathT>0 && sim.deathCount==1);
        CHECK(strcmp(sim.deathBy,"PIRATE CLARK")==0);
        CHECK(sim.deathM==250 && sim.deathKills==2 && sim.deathTime>99.0f);
        // ...and the world behind the card is a fresh descent, not the one that killed you
        CHECK(sim.level==0 && sim.coins==0 && sim.ent.st==EState::Hidden);
        // ...and look at the card it puts up, since it is the only screen in the
        // game that renders on the title screen rather than over a live run
        sim.deathBy="PIRATE CLARK"; sim.deathLevel=0; sim.deathTime=247; sim.deathM=612;
        sim.deathKills=2; sim.deathCount=3; sim.best.deepest=3; sim.best.longestRun=430;
        sim.deathT=Sim::DEATH_CARD-1.8f; sim.inMenu=true;
        capture(g,"death-card.png");
        sim.deathT=0; sim.inMenu=false;
    }

    // ---- the hunter actually moves now (ENT-01/ENT-02). Look at these: a
    // walk cycle that does not read as a walk is worse than no walk cycle.
    {
        sim.applyLevel(0,GetTime());
        // A fixed spot drifts behind a wall whenever the generator changes, and
        // these shots then silently show an empty corridor. Find one with a
        // clear 7 m line down +x instead.
        float sx=41, sz=41;
        for (int k=0;k<400;++k) {
            float cx=(k%20)*CELL+41, cz=(k/20)*CELL+41;
            if (sim.world.lineOfSight(cx,cz,cx+7.5f,cz) && sim.world.floorY(cellOf(cx),cellOf(cz))==0
                && sim.world.floorY(cellOf(cx+7),cellOf(cz))==0) { sx=cx; sz=cz; break; }
        }
        sim.px=sx; sim.pz=sz; sim.py=0; sim.eyeY=1.62f; sim.pitch=0; sim.deathT=0; sim.inMenu=false;
        sim.ent.x=sim.px+7.0f; sim.ent.z=sim.pz; sim.ent.dispY=0; sim.ent.hp=3; sim.ent.stagger=0;
        sim.yaw=0; sim.updateLook(noInput(g));
        // stalking, head still down the corridor, mid-stride at four phases
        sim.ent.st=EState::Stalk; sim.ent.gaze=0;
        for (int i=0;i<4;++i) {
            sim.ent.gait = i * 0.5f;   // quarter-cycle steps: render.cpp's phase is gait*0.5
            char n[48]; snprintf(n,sizeof(n),"clark-walk-%d.png",i);
            capture(g,n);
        }
        // gaze tips over: his head comes round, which is the tell
        sim.ent.gaze=1.2f; sim.ent.gait=0.5f; capture(g,"clark-noticed.png");
        // and the chase lean, and the harder lean of a committed lunge
        sim.ent.st=EState::Chase; sim.ent.lunge=0; capture(g,"clark-chase.png");
        sim.ent.lunge=Sim::LUNGE_TIME; capture(g,"clark-lunge.png");
        // one hit down: no bar, just the edges still red until you heal
        { // with the HUD layer on (the harness shoots clean) and past the intro card
          double rs=sim.runStart; sim.runStart=-60; g.cleanShot=false;
          sim.health=1-Sim::ENTITY_HIT; sim.hurtT=0; capture(g,"wounded.png");
          sim.health=1; capture(g,"unwounded.png");
          sim.runStart=rs; g.cleanShot=true; }
        // Level 0 is Pirate Clark's; the Smiler hunts Level 1 and the Red Halls.
        CHECK(strcmp(sim.hunterName(),"PIRATE CLARK")==0);
        {
            sim.applyLevel(1,GetTime());
            CHECK(strcmp(sim.hunterName(),"A SMILER")==0);
            float qx=41, qz=41;
            for (int k=0;k<400;++k) {
                float cx=(k%20)*CELL+41, cz=(k/20)*CELL+41;
                if (sim.world.lineOfSight(cx,cz,cx+7.5f,cz) && sim.world.floorY(cellOf(cx),cellOf(cz))==0
                    && sim.world.floorY(cellOf(cx+7),cellOf(cz))==0) { qx=cx; qz=cz; break; }
            }
            sim.px=qx; sim.pz=qz; sim.py=0; sim.eyeY=1.62f; sim.yaw=0; sim.pitch=0;
            sim.ent.x=sim.px+7.0f; sim.ent.z=sim.pz; sim.ent.dispY=0; sim.ent.st=EState::Chase; sim.ent.lunge=0;
            capture(g,"smiler-chase.png");
            sim.applyLevel(4,GetTime()); CHECK(strcmp(sim.hunterName(),"THE PARTYGOER")==0);
            sim.applyLevel(0,GetTime());
            sim.px=sx; sim.pz=sz; sim.ent.x=sim.px+7.0f; sim.ent.z=sim.pz; sim.yaw=0; sim.updateLook(noInput(g));
        }
        sim.ent.st=EState::Hidden; sim.ent.lunge=0;
        // The pack, mid-bound. Captured on Level 0 rather than in the Red Halls
        // they actually live in: the Red Halls sit at mean luma 12 and a black
        // dog against it is unreviewable. This shot is for the run cycle only.
        sim.px=sx; sim.pz=sz; sim.yaw=0; sim.updateLook(noInput(g));
        sim.dogs[0].st=DState::Charge; sim.dogs[0].x=sim.px+5.0f; sim.dogs[0].z=sim.pz;
        sim.dogs[0].dispY=0; sim.dogs[0].hp=2; sim.dogs[0].gait=0.5f;
        for (int i=0;i<2;++i) {
            sim.dogs[0].gait = i * 0.5f;
            char n[48]; snprintf(n,sizeof(n),"pack-bound-%d.png",i);
            capture(g,n);
        }
        sim.dogs[0].st=DState::Gone;
    }

    // ---- he no longer always walks out of the fog (ENT-03). Measured, not
    // asserted by eye: the claim "sometimes he is already round the corner"
    // needs a number behind it.
    {
        sim.applyLevel(0,GetTime()); sim.beginDescent(0); sim.inMenu=false; sim.deathT=0;
        sim.px=40; sim.pz=40; sim.yaw=0.8f; sim.pitch=0; sim.updateLook(noInput(g));
        int near=0, unseen=0, nearUnseen=0, total=400;
        float dmin=1e9f, dmax=0;
        for (int i=0;i<total;++i) {
            sim.ent.st=EState::Hidden; sim.ent.nextSpawn=0;
            sim.updateEntity(0.001f, 100.0);
            float dx=sim.ent.x-sim.px, dz=sim.ent.z-sim.pz;
            float d=sqrtf(dx*dx+dz*dz);
            dmin=fminf(dmin,d); dmax=fmaxf(dmax,d);
            bool los = sim.world.lineOfSight(sim.px,sim.pz,sim.ent.x,sim.ent.z);
            if (d < Sim::SPAWN_FAR_MIN) near++;
            if (!los) unseen++;
            if (d < Sim::SPAWN_FAR_MIN && !los) nearUnseen++;
        }
        printf("  ENT-03 arrivals: %d%% inside %.0fm, %d%% out of sight, %d%% BOTH, range %.1f-%.1f m\n",
               near*100/total, (double)Sim::SPAWN_FAR_MIN, unseen*100/total,
               nearUnseen*100/total, dmin, dmax);
        // The frightening case — close, and already behind something — has to be
        // a real share of arrivals, and the old walk-out-of-the-fog one has to
        // survive alongside it: replacing one fixed ritual with another buys
        // nothing.
        CHECK(nearUnseen > total/5 && near < total*9/10);
        CHECK(dmax >= Sim::SPAWN_FAR_MIN);
        sim.ent.st=EState::Hidden;
    }

    // ---- the pack hunts by sound, not by what you are standing behind (ENT-04)
    {
        sim.still=true;  sim.deck.playing=false; sim.deck.carried=true;
        CHECK(sim.packDeaf());                    // dead still: they lose you, cover or not
        sim.still=false; CHECK(!sim.packDeaf());    // moving: they have you
        sim.still=true;  sim.deck.playing=true;      // still, but the tape is running in your coat
        CHECK(!sim.packDeaf());
        sim.deck.carried=false; CHECK(sim.packDeaf());   // ...set it down and it is the deck they want
        sim.deck.playing=false; sim.deck.carried=true;
    }

    // ---- the building moves when you are not looking (PAC-03)
    {
        sim.applyLevel(0,GetTime()); sim.beginDescent(0); sim.inMenu=false;
        sim.px=40; sim.pz=40; sim.py=0; sim.eyeY=1.62f; sim.updateLook(noInput(g));
        size_t before = sim.world.shifted.size();
        int moved=0;
        for (int i=0;i<40;++i) if (sim.shiftAWall()) moved++;
        CHECK(moved > 0 && sim.world.shifted.size() == before + (size_t)moved);
        // every edge it shifted must now read as a wall through the same
        // accessors collision, the pathfinder and the mesher all use...
        for (uint64_t k : sim.world.shifted) {
            bool west = (k & 1ull) != 0;
            int a = (int)(uint32_t)((k >> 1) >> 32), b = (int)(uint32_t)((k >> 1) & 0xFFFFFFFFull);
            CHECK(blocksEdge(west ? sim.world.wallWVal(a,b) : sim.world.wallNVal(a,b)));
            // ...and it must not have been one you could see it happen to
            float cx=a*CELL+1.0f, cz=b*CELL+1.0f;
            CHECK(!sim.world.lineOfSight(sim.px,sim.pz,cx,cz));
        }
        printf("  PAC-03: %d doorways walled off out of sight, all opaque to wallNVal/wallWVal\n", moved);
        sim.world.shifted.clear();
    }

    // ---- and the grip meter is an ending now, not a difficulty setting (STK-03)
    {
        sim.applyLevel(0,GetTime()); sim.beginDescent(0); sim.inMenu=false; sim.deathT=0; sim.deathCount=0;
        sim.sanity=0.0f;
        sim.updateAmbience(0.001f, 200.0);
        CHECK(sim.inMenu && sim.deathT>0);
        CHECK(strcmp(sim.deathBy,"THE PLACE ITSELF")==0);
        CHECK(strcmp(sim.deathTitle,"YOU STOPPED KEEPING TRACK")==0);   // its own card, not the catch's
        CHECK(sim.sanity>0.9f);   // beginDescent gave it back
        sim.deathT=0; sim.inMenu=false;
        // and the last tenth is a slide you can feel, not a cliff
        sim.sanity=0.05f; sim.updateAmbience(0.001f, 300.0);
        CHECK(sim.slide>0.4f && sim.slide<1.0f);
        sim.sanity=0.5f; sim.updateAmbience(0.001f, 300.0);
        CHECK(sim.slide==0.0f);
    }

    // A low view at a tiled Poolrooms wall exposes shadow lookup leaking
    // across the wall when texture relief is used as the ray-origin normal.
    sim.applyLevel(2,GetTime());
    bool corner=false;
    for (int a=2;a<30 && !corner;++a) for (int b=2;b<30 && !corner;++b) {
        if (sim.world.wallNVal(a,b)!=WALL_SOLID || sim.world.poolAt(a,b) || sim.world.pillarAt(a,b)) continue;
        sim.px=a*CELL+1; sim.pz=b*CELL+0.65f; sim.py=sim.world.floorY(a,b); sim.eyeY=sim.py+1.62f;
        sim.yaw=-PI/2; sim.pitch=-0.9f; sim.ammo=6; sim.reloadT=0; sim.weapon=WEAPON_REVOLVER;
        capture(g,"pool-wall-corner.png"); corner=true;
    }
    CHECK(corner);
    // The Poolrooms are no longer one layout per chunk. Over a patch of the
    // level every lore room type must turn up, and each gets a capture:
    // glowing windows, a staircase descending into deep water, flooded tunnels.
    {
        bool win=false, stairs=false, tunnel=false, hall=false;
        int wet=0, cells=0;
        for (int a=1;a<96;++a) for (int b=1;b<96;++b) {
            ++cells; if (sim.world.poolAt(a,b)) ++wet;
            float e=sim.world.floorY(a,b);
            if (!win && sim.world.wallNVal(a,b)==WALL_WINDOW && !sim.world.poolAt(a,b+1) && !sim.world.pillarAt(a,b+1)) {
                sim.px=a*CELL+1; sim.pz=(b+2)*CELL+0.2f; sim.py=sim.world.floorY(a,b+1); sim.eyeY=sim.py+1.62f;
                sim.yaw=-PI/2; sim.pitch=0.05f;
                capture(g,"pool-windows.png"); win=true;
            }
            // three treads in a row, each one step (0.4 m) below the last
            if (!stairs && sim.world.poolAt(a,b) && fabsf(e+0.4f)<0.01f &&
                fabsf(sim.world.floorY(a+1,b)+0.8f)<0.01f && fabsf(sim.world.floorY(a+2,b)+1.2f)<0.01f &&
                !sim.world.poolAt(a-1,b) && !sim.world.pillarAt(a-1,b)) {
                sim.px=(a-1)*CELL+0.4f; sim.pz=b*CELL+1; sim.py=sim.world.floorY(a-1,b); sim.eyeY=sim.py+1.62f;
                sim.yaw=0; sim.pitch=-0.35f;
                capture(g,"pool-stairs.png"); stairs=true;
            }
            // a one-cell flooded corridor: walls both sides, open ahead and behind
            if (!tunnel && sim.world.poolAt(a,b) && sim.world.wallWVal(a,b)==WALL_SOLID && sim.world.wallWVal(a+1,b)==WALL_SOLID &&
                sim.world.poolAt(a,b+1) && sim.world.poolAt(a,b+2) && sim.world.wallNVal(a,b+1)==WALL_NONE &&
                sim.world.wallNVal(a,b+2)==WALL_NONE && sim.world.floorY(a,b)>-1.0f) {
                sim.px=a*CELL+1; sim.pz=b*CELL+0.5f; sim.py=sim.world.floorY(a,b); sim.eyeY=sim.py+1.62f;
                sim.yaw=PI/2; sim.pitch=0;
                capture(g,"pool-tunnel.png"); tunnel=true;
            }
            if (!hall && sim.world.pillarAt(a,b) && sim.world.poolAt(a,b+1) && sim.world.floorY(a,b+1)<-2.0f) hall=true;
        }
        CHECK(win && stairs && tunnel && hall);
        CHECK(wet*2>cells);   // still a flooded level, not a dry one with puddles
    }
    // Isolated pillar: exposes the old hard contact rectangle and the bright
    // square where shadow rays skipped their first/last occupancy cells.
    sim.applyLevel(0,GetTime()); sim.world.unloadAll();
    auto &pillarRoom=sim.world.data(0,0);
    std::memset(pillarRoom.wallN,0,sizeof(pillarRoom.wallN));
    std::memset(pillarRoom.wallW,0,sizeof(pillarRoom.wallW));
    std::memset(pillarRoom.pillar,0,sizeof(pillarRoom.pillar));
    std::memset(pillarRoom.prop,0,sizeof(pillarRoom.prop));
    std::memset(pillarRoom.elev,0,sizeof(pillarRoom.elev));
    pillarRoom.pillar[4][4]=1;
    sim.px=5.5f;sim.pz=5.5f;sim.py=0;sim.eyeY=1.62f;sim.yaw=PI/4;sim.pitch=0;
    sim.inMenu=false;sim.weapon=WEAPON_REVOLVER;sim.fov=70;sim.flashOn=false;sim.flashCur=0;
    sim.blackoutCur=1;sim.ent.st=EState::Hidden;sim.entDarkCur=0;sim.aimBlend=0;sim.reloadT=0;
    capture(g,"pillar-contact.png");

    // ---- every embedded recording decodes: a bad or truncated .ogg would
    // otherwise load as a silent zero-length sound and play as nothing at all.
    {
        int n=0;
        for (const EmbeddedAsset &a : soundAssets) {
            Wave w=LoadWaveFromMemory(".ogg",a.data,(int)a.size);
            CHECK(w.frameCount>0 && w.sampleRate>0);
            UnloadWave(w); ++n;
        }
        CHECK(n>=12);   // 11 water clips and the LEVEL FUN loop
    }

    // ---- a ceiling balloon found where the renderer draws it can be shot:
    // render.cpp now places them through balloonAt, the function bullets test.
    {
        sim.applyLevel(4,GetTime());
        int ba=0,bb=0; Vec3 bp{}; bool found=false;
        for (int a=0;a<60 && !found;++a) for (int b=0;b<60 && !found;++b)
            if (sim.balloonAt(a,b,bp)) { ba=a; bb=b; found=true; }
        CHECK(found);
        CHECK(sim.popBalloonAt(bp));
        Vec3 again;
        CHECK(!sim.balloonAt(ba,bb,again));
        sim.poppedBalloons.clear(); sim.confetti.clear();
        sim.applyLevel(0,GetTime());
    }

    // ---- storeys (Level 0). The floors are generated blind of each other and
    // joined by features both of them stamp from one pure function, and the
    // player changes frame halfway up a flight. Everything below asserts a
    // mechanism, not a map: the two halves agree, collision and ground height
    // agree across the boundary where the switch happens, and a real movement
    // loop takes you up a flight, onto the next floor, and back.
    {
        sim.applyLevel(0,GetTime()); sim.inMenu=false; sim.deathT=0;
        World &w=sim.world;
        const float H=w.storeyH;
        CHECK(H>4.0f && H<5.0f && w.storey==0);
        int nWell=0,nStair=0,nAtr=0,nAtrStair=0,checkedPts=0;
        struct Found { VertFeat f; int cx,cz; bool ok=false; } straight, atrium, well;
        for (int cz=-5;cz<=5;++cz) for (int cx=-5;cx<=5;++cx) for (int p=-2;p<=2;++p) {
            VertFeat f; if (!w.pairFeature(cx,cz,p,f)) continue;
            nWell+=f.kind==VK_STAIRWELL; nStair+=f.kind==VK_STAIR;
            nAtr+=f.kind==VK_ATRIUM && f.stairU<0; nAtrStair+=f.kind==VK_ATRIUM && f.stairU>=0;
            if (p==0 && f.kind==VK_STAIR && !straight.ok) straight={f,cx,cz,true};
            if (p==0 && f.kind==VK_ATRIUM && f.stairU<0 && !atrium.ok) atrium={f,cx,cz,true};
            if (p==0 && f.kind==VK_STAIRWELL && !well.ok) well={f,cx,cz,true};
            // (1) cell for cell: open above below exactly where there is a hole
            // above, and a flight below exactly where the hole can be walked
            for (int uc=0;uc<f.wu;++uc) for (int vc=0;vc<f.lv;++vc) {
                Vector3 c=toRl(w.featureWorld(f,cx,cz,(uc+0.5f)*CELL,0,(vc+0.5f)*CELL));
                int ci=cellOf(c.x), ck=cellOf(c.z);
                uint8_t lo,hi;
                { StoreyScope s(w,p); lo=w.vflagAt(ci,ck); }
                { StoreyScope s(w,p+1); hi=w.vflagAt(ci,ck); }
                CHECK(((lo&VF_OPENUP)!=0)==((hi&VF_HOLE)!=0));
                // a flight coming up through a hole can be walked down from
                // above, and a walkable hole always has something under it
                if ((lo&VF_STAIR) && (lo&VF_OPENUP)) CHECK(hi&VF_WALKHOLE);
                if (hi&VF_WALKHOLE) CHECK(lo&VF_OPENUP);
                CHECK((lo&VF_KEEP) && (hi&VF_KEEP));
            }
            if (f.kind==VK_ATRIUM && f.stairU<0) continue;
            // (2) where the frame changes — the stretch of flight at half a
            // storey — the storey above and the storey below must agree about
            // what is under your feet and what stops you, or the switch is a
            // step, a drop or a wall that was not there a frame ago.
            for (float u=0.2f; u<f.wu*CELL; u+=0.4f) for (float v=0.1f; v<f.lv*CELL; v+=0.2f) {
                Vector3 pt=toRl(w.featureWorld(f,cx,cz,u,0,v));
                float gl; { StoreyScope s(w,p); if (!(w.vflagAt(cellOf(pt.x),cellOf(pt.z))&VF_STAIR)) continue;
                            gl=w.groundAt(pt.x,pt.z,H*0.5f+0.3f); }
                if (fabsf(gl-H*0.5f)>0.35f) continue;
                float gh; { StoreyScope s(w,p+1); gh=w.groundAt(pt.x,pt.z,H*0.5f+0.3f-H); }
                CHECK_NEAR(gl,gh+H,0.002f);
                for (int d=0;d<8;++d) {
                    float a=d*PI/4, x1=pt.x+cosf(a)*0.9f, z1=pt.z+sinf(a)*0.9f, x2=x1, z2=z1;
                // only probes that start inside the footprint, clear of its
                // walls: one that starts on the far side of a shaft wall is
                // somewhere you cannot get to from the stairs
                float pu, pv; w.featureLocal(f,cx,cz,x1,z1,pu,pv);
                if (pu<WT+0.02f || pv<WT+0.02f || pu>f.wu*CELL-WT-0.02f || pv>f.lv*CELL-WT-0.02f) continue;
                    { StoreyScope s(w,p);   w.collideCircle(x1,z1,Sim::PR,gl); }
                    { StoreyScope s(w,p+1); w.collideCircle(x2,z2,Sim::PR,gl-H); }
                    CHECK_NEAR(x1,x2,0.01f); CHECK_NEAR(z1,z2,0.01f);
                }
                checkedPts++;
            }
        }
        printf("Storeys: %d stairwells, %d stairs, %d atria (%d with a flight) over 121 chunks x 5 pairs; "
               "%d boundary points agree\n", nWell, nStair, nAtr+nAtrStair, nAtrStair, checkedPts);
        CHECK(nWell>0 && nStair>0 && nAtr>0 && nAtrStair>0 && checkedPts>50);
        CHECK(straight.ok && atrium.ok && well.ok);

        // (3) climb a straight flight with the real mover, Clark close behind.
        const VertFeat &sf=straight.f;
        float su=sf.wu*CELL*0.5f;
        Vector3 foot=toRl(w.featureWorld(sf,straight.cx,straight.cz,su,0,0.8f));
        Vector3 up1=toRl(w.featureWorld(sf,straight.cx,straight.cz,su,0,1.8f));
        float dx=up1.x-foot.x, dz=up1.z-foot.z;
        sim.px=foot.x; sim.pz=foot.z; sim.py=0; sim.vy=0; sim.grounded=true; sim.fallFrom=0;
        sim.health=1; sim.hurtT=0; sim.swimming=false; sim.squeezing=false;
        sim.ent.st=EState::Chase; sim.ent.x=sim.px-dx*2.5f; sim.ent.z=sim.pz-dz*2.5f; sim.ent.dispY=0; sim.entDist=2.5f;
        int climbFrames=0; bool rose=false; float lastPy=0, worstDrop=0;
        for (; climbFrames<400 && !(w.storey==1 && sim.py>-0.02f); ++climbFrames) {
            sim.velx=dx*4.0f; sim.velz=dz*4.0f;
            sim.updateMovement(noInput(g),1.0f/30,GetTime());
            if (w.storey==0) { worstDrop=fmaxf(worstDrop,lastPy-sim.py); lastPy=sim.py; }
            if (w.storey==1 && !rose) {
                rose=true;
                // He came up after you: on the flight, below you, in the new frame.
                CHECK(sim.ent.st==EState::Chase);
                CHECK(sim.ent.dispY < sim.py-1.0f);
                CHECK(w.vflagAt(cellOf(sim.ent.x),cellOf(sim.ent.z)) & VF_WALKHOLE);
            }
        }
        CHECK(rose && w.storey==1);
        CHECK(worstDrop<MAX_STEP);                 // no step down on the way up
        CHECK(fabsf(sim.py)<0.05f && sim.grounded);   // standing on the floor above
        CHECK(sim.health==1);                        // walking up stairs is not a fall
        CHECK(sim.storeyNoted && sim.deckNoteT>0);     // and the first time, it says so
        sim.ent.st=EState::Hidden;
        // ...and back down the same flight
        for (int i=0;i<400 && !(w.storey==0 && sim.py<0.05f && sim.py>-0.05f &&
                                 w.vflagAt(cellOf(sim.px),cellOf(sim.pz))==VF_KEEP);++i) {
            sim.velx=-dx*4.0f; sim.velz=-dz*4.0f;
            sim.updateMovement(noInput(g),1.0f/30,GetTime());
        }
        CHECK(w.storey==0 && fabsf(sim.py)<0.05f && sim.health==1);

        // (4) over an atrium's railing: a storey's fall, into the hall below
        const VertFeat &af=atrium.f;
        Vector3 over=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,af.lv*CELL*0.5f));
        sim.changeStorey(1,100);
        CHECK(w.storey==1 && (w.vflagAt(cellOf(over.x),cellOf(over.z))&VF_HOLE));
        // the rail round it stops a body, not a look, and no route crosses it
        {
            Vector3 edge=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,-0.3f));
            Vector3 in=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,0.6f));
            float bx=edge.x,bz=edge.z; w.collideCircle(bx,bz,Sim::PR,0.0f);
            CHECK(fabsf(bx-edge.x)+fabsf(bz-edge.z)>0.1f);
            // A normal jump peaks above the knee wall; walking remains blocked.
            bx=edge.x;bz=edge.z;w.collideCircle(bx,bz,Sim::PR,.72f);
            CHECK_NEAR(bx,edge.x,.001f);CHECK_NEAR(bz,edge.z,.001f);
            for(int hz : {30,60,144}) {
                sim.px=edge.x;sim.pz=edge.z;sim.py=0;sim.vy=5.6f;sim.grounded=false;
                sim.health=1;sim.hurtT=0;sim.fallFrom=0;sim.swimming=false;
                float dx=in.x-edge.x,dz=in.z-edge.z,len=sqrtf(dx*dx+dz*dz);
                bool crossed=false;
                for(int frame=0;frame<hz;++frame) {
                    sim.velx=dx/len*4;sim.velz=dz/len*4;sim.updateMovement(noInput(g),1.0f/hz,GetTime());
                    if(w.vflagAt(cellOf(sim.px),cellOf(sim.pz))&VF_HOLE) { crossed=true;break; }
                }
                CHECK(crossed);
                if(w.storey!=1) sim.changeStorey(1-w.storey,100);
            }
            CHECK(w.lineOfSight(edge.x,edge.z,in.x,in.z));
            CHECK(!w.canStep(cellOf(edge.x),cellOf(edge.z),cellOf(in.x),cellOf(in.z)));
        }
        sim.px=over.x; sim.pz=over.z; sim.py=0; sim.vy=0; sim.grounded=false; sim.fallFrom=0;
        sim.health=1; sim.hurtT=0; sim.velx=sim.velz=0;
        for (int i=0;i<200 && !(w.storey==0 && sim.grounded);++i) sim.updateMovement(noInput(g),1.0f/30,GetTime());
        CHECK(w.storey==0 && sim.grounded && fabsf(sim.py)<0.05f);
        float fallDmg=1-sim.health;
        printf("  a storey's fall through an atrium took %.2f of the health meter\n", fallDmg);
        CHECK(fallDmg>0.3f && fallDmg<0.5f && !sim.inMenu);

        // (5) every floor is its own set of rooms and of things left in them
        {
            int same=0, total=0;
            std::vector<int> a0;
            for (int a=-20;a<20;++a) for (int b=-20;b<20;++b) a0.push_back((int)sim.pickupAt(a,b));
            sim.changeStorey(1,100);
            size_t q=0;
            for (int a=-20;a<20;++a) for (int b=-20;b<20;++b,++q) {
                int k=(int)sim.pickupAt(a,b);
                if (k || a0[q]) { total++; if (k==a0[q]) same++; }
            }
            CHECK(total>4 && same*2<total);
            sim.changeStorey(-1,100);
        }

        // (6) what it looks like: the foot of a stairwell, a flight halfway up,
        // and an atrium from the balcony above
        sim.ent.st=EState::Hidden; sim.fear=0;
        {
            const VertFeat &wf=well.f;
            Vector3 at=toRl(w.featureWorld(wf,well.cx,well.cz,1.0f,0,0.7f)), to=toRl(w.featureWorld(wf,well.cx,well.cz,1.0f,0,1.7f));
            sim.px=at.x; sim.pz=at.z; sim.py=0; sim.eyeY=1.62f; sim.pitch=0.25f; sim.yaw=atan2f(to.z-at.z,to.x-at.x);
            capture(g,"storey-stairwell.png");
        }
        {
            Vector3 at=toRl(w.featureWorld(sf,straight.cx,straight.cz,su,0,4.5f));
            sim.px=at.x; sim.pz=at.z; sim.py=w.groundAt(at.x,at.z,3.0f); sim.eyeY=sim.py+1.62f; sim.pitch=0.05f;
            sim.yaw=atan2f(dz,dx);
            capture(g,"storey-stair.png");
        }
        {
            sim.changeStorey(1,100);
            Vector3 at=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,-1.0f));
            Vector3 to=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,1.0f));
            sim.px=at.x; sim.pz=at.z; sim.py=0; sim.eyeY=1.62f; sim.pitch=-0.55f; sim.yaw=atan2f(to.z-at.z,to.x-at.x);
            capture(g,"storey-atrium.png");
            sim.changeStorey(-1,100);
        }
        {   // the same atrium from the floor of the hall, looking up at the balcony
            Vector3 at=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,-1.2f));
            Vector3 to=toRl(w.featureWorld(af,atrium.cx,atrium.cz,af.wu*CELL*0.5f,0,1.0f));
            sim.px=at.x; sim.pz=at.z; sim.py=0; sim.eyeY=1.62f; sim.pitch=0.42f; sim.yaw=atan2f(to.z-at.z,to.x-at.x);
            capture(g,"storey-atrium-below.png");
        }
        {   // the head of the straight flight, from the floor it arrives at
            sim.changeStorey(1,100);
            Vector3 at=toRl(w.featureWorld(sf,straight.cx,straight.cz,su,0,11.2f));
            Vector3 to=toRl(w.featureWorld(sf,straight.cx,straight.cz,su,0,9.0f));
            sim.px=at.x; sim.pz=at.z; sim.py=0; sim.eyeY=1.62f; sim.pitch=-0.38f; sim.yaw=atan2f(to.z-at.z,to.x-at.x);
            capture(g,"storey-stair-top.png");
            sim.changeStorey(-1,100);
        }
        sim.applyLevel(0,GetTime());
        CHECK(w.storey==0);
    }

    // Both orientations and both approaches must activate white Level 0 exits.
    // Put both orientations at one cell to catch an else-if masking the west door.
    for (int west=0; west<2; ++west) for (int side : {-1,1}) {
        sim.applyLevel(0,GetTime()); sim.escapeT=0; sim.coins=0;
        int ci=7,ck=7;
        while (sim.world.cursedExit(ci,ck)) ++ci;
        auto &d=sim.world.data(0,0);
        d.wallN[ci][ck]=d.wallW[ci][ck]=WALL_EXIT;
        sim.px=ci*CELL+(west ? side*.5f : 1.0f);
        sim.pz=ck*CELL+(west ? 1.0f : side*.5f);
        int count=sim.escapeCount;
        sim.updateExits(100);
        CHECK(sim.level==1 && sim.escapeCount==count+1 && sim.escapeT>0);
    }
    // Aligned tall courts preserve a closed cap, matching holes on every
    // intermediate storey, and a real floor at the bottom of a long fall.
    for(int floors : {3,5,7}) {
        sim.applyLevel(0,GetTime());auto &w=sim.world;bool found=false;
        for(int cx=-10;cx<=10 && !found;++cx) for(int cz=-10;cz<=10 && !found;++cz) {
            VertFeat f;
            if(!w.pairFeature(cx,cz,0,f) || f.kind!=VK_ATRIUM || f.x0!=4 || f.z0!=4 || f.lv!=4) continue;
            int count=0;VertFeat next;
            while(count<7 && w.pairFeature(cx,cz,count,next) && next.x0==4 && next.z0==4 && next.lv==4) ++count;
            if(count!=floors-1) continue;
            int ci=cx*CCELLS+5,ck=cz*CCELLS+5;
            for(int st=0;st<floors;++st) {
                StoreyScope sc(w,st);auto flags=w.vflagAt(ci,ck);
                CHECK(bool(flags&VF_HOLE)==(st>0));
                CHECK(bool(flags&VF_OPENUP)==(st<floors-1));
            }
            { StoreyScope sc(w,floors-1);
              CHECK_NEAR(w.groundAt(ci*CELL+1,ck*CELL+1,0),-(floors-1)*w.storeyH,.001f); }
            sim.px=cx*CHUNK+12;sim.pz=cz*CHUNK+12;sim.py=0;sim.eyeY=1.62f;
            sim.pitch=.85f;sim.yaw=.3f;sim.ent.st=EState::Hidden;
            for(int i=0;i<80;++i) g.streamChunks();
            CHECK(g.chunkMeshes.find(w,floors-1,cx,cz));
            char name[40];snprintf(name,sizeof(name),"stacked-court-%d.png",floors);
            capture(g,name);
            for(int st=1;st<floors;++st) sim.changeStorey(1,100);
            sim.px=cx*CHUNK+12;sim.pz=cz*CHUNK+17;sim.py=0;sim.eyeY=1.62f;
            sim.pitch=-.65f;sim.yaw=-PI*.5f;
            for(int i=0;i<80;++i) g.streamChunks();
            snprintf(name,sizeof(name),"stacked-court-top-%d.png",floors);
            capture(g,name);found=true;
        }
        CHECK(found);
    }
    printf("PASS white doors from both axes/sides and 3/5/7-storey court caps/holes/streaming\n");

    sim.applyLevel(0,GetTime());sim.ent.st=EState::Stalk;sim.ent.x=sim.px+10;sim.ent.z=sim.pz;
    sim.ent.gaze=-100;sim.updateEntity(1.0f/60,100);CHECK(sim.ambience.growl==0);
    sim.ent.st=EState::Chase;sim.ent.lungeCd=10;sim.updateEntity(1.0f/60,100);
    CHECK(sim.ambience.growl==0);
    // ...and the synth is fed that mix, as the last sound the entity asked for.
    CHECK(!sim.audio.empty() && sim.audio.back().kind==AudioEvent::AMBIENCE && sim.audio.back().mix.growl==0);

    // Sprint arrives through the InputFrame, so its pull on the FOV is driven
    // live: one step from the base toward base + 9.
    {
        sim.aiming=false; sim.aimBlend=0; sim.slide=0; sim.stamina=1; sim.sprintExhausted=false;
        sim.squeezing=false; sim.crouchCur=0;
        InputFrame run=noInput(g); run.forward=run.sprint=true;
        sim.fov=run.screenFov; sim.updateMovement(run,1.0f/60,GetTime());
        CHECK(sim.sprinting && sim.fov>run.screenFov+0.1f && sim.fov<run.screenFov+9);
        sim.updateMovement(noInput(g),1.0f/60,GetTime()); CHECK(!sim.sprinting);
    }

    // The sim's rayBox stands in for raylib's GetRayCollisionBox on actor
    // bodies; a shot's distance and impact normal depend on it matching.
    {
        Rng r(7);
        int hits=0;
        for (int i=0;i<200000;++i) {
            Vec3 c{r.f01()*10-5, r.f01()*2, r.f01()*10-5};
            float rad=0.2f+r.f01()*0.5f, h=0.5f+r.f01()*1.5f;
            Box3 b{{c.x-rad,c.y,c.z-rad},{c.x+rad,c.y+h,c.z+rad}};
            Vec3 o = i%7==0 ? Vec3{c.x+(r.f01()*2-1)*rad, c.y+r.f01()*h, c.z+(r.f01()*2-1)*rad}
                            : Vec3{r.f01()*16-8, r.f01()*4-1, r.f01()*16-8};
            Vec3 d=normalize({r.f01()*2-1, i%11==0 ? 0.0f : r.f01()*2-1, r.f01()*2-1});
            RayHit a=rayBox({o,d},b);
            RayCollision e=GetRayCollisionBox(toRl(Ray3{o,d}),toRl(b));
            Vec3 en=fromRl(e.normal);
            CHECK(a.hit==e.hit);
            CHECK(memcmp(&a.distance,&e.distance,sizeof(float))==0);
            CHECK(memcmp(&a.normal,&en,sizeof(Vec3))==0);
            hits+=e.hit;
        }
        CHECK(hits>1000);
    }

    // ---- headless captures must not be able to black out (BUG-08)
    CHECK(sim.noBlackout && sim.nextBlackout >= Sim::BLACKOUT_NEVER);

    printf("PASS Poolrooms buoyancy at 30/60/144 Hz, diving, resurfacing, ledges and refuge,\n"
           "     sprint recovery, crouch/stationary gating, restart reset, battery retention,\n"
           "     animation continuity, held aim/reload gating, step-height blocking,\n"
           "     projectile travel/occlusion, squeeze clearance, drinking audio, doorway jambs and locked doors you cannot walk\n"
           "     through, the catch ending the run only out of a committed\n"
           "     lunge, arrivals that are not all from the fog, a pack that hunts by sound,\n"
           "     a building that moves out of sight, the grip meter as an ending,\n"
           "     deterministic captures, storeys that agree with each other, a flight climbed\n"
           "     and descended by the real mover, Clark on the stairs behind you, a fall that hurts;\n"
           "     %d visual captures\n", captureCount);
    g.shutdown();
}
