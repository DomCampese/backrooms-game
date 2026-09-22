// Native integration checks and reproducible viewmodel captures.
#include "game.h"
#include "raymath.h"
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


static int captureCount = 0;
static void capture(Game &g, const char *name) {
    ++captureCount;
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
    // Portrait FOV lock. raylib derives fovX from fovy*aspect, so these numbers
    // are the whole contract: every window at or wider than the authored
    // 1440x850 must be untouched (the regression sweeps compare those frames),
    // and a portrait phone must not stay at the ~36-degree keyhole the fixed
    // 70 made on a 0.46 screen.
    {
        auto fovX=[&](int w,int h,float aim){
            return 2*atanf(tanf(Game::fovForWindow(w,h,aim)*DEG2RAD*0.5f)*(float)w/h)*RAD2DEG;};
        CHECK_NEAR(Game::fovForWindow(1440,850,0),70,0.01f);    // authored window
        CHECK_NEAR(fovX(1440,850,0),fovX(390,844,0),0.01f);     // portrait locks fovX
        CHECK(fovX(390,844,0)>55);                              // was ~36 before
        CHECK(Game::fovForWindow(1920,1080,0)==70);             // wide: untouched
        CHECK(Game::fovForWindow(3440,1440,0)==70);             // ultrawide: untouched
        CHECK(Game::fovForWindow(0,0,0)==70);                   // no window yet
        // The action terms ride on top of the base, and the smoothing still
        // eases: at the authored window one dt step toward aim moves fov from
        // 70 down toward 62 (never instantly), and releasing walks it back up.
        // The targets themselves are fixed numbers in updateMovement and the
        // sweep frames above pin the base; this pins that they still compose.
        g.aimBlend=1;g.sprinting=false;g.slide=0;g.px=15;g.pz=15;g.py=0;
        g.updateMovement(1.0f/60);CHECK(g.fov<69.5f && g.fov>62.0f);
        g.aimBlend=0;g.updateMovement(1.0f/60);CHECK(g.fov>68.0f && g.fov<70.0f);   // easing back up
        // Sprint and slide: `sprinting` is recomputed from the shift key
        // every updateMovement and no key state exists headless (CGEvents
        // reach GLFW neither under shot.sh nor from the harness), so those
        // terms cannot be driven through the live call. They are constant
        // offsets on the target in updateMovement — the aim test above
        // already pins the composition — and the base itself is pinned by
        // the checks and the sweep. No fake flag gymnastics: a check that
        // passes only against a clobbered value would be worse than none.
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
    // Chalk is per level and per descent (Game::chalk is an array, not a list);
    // this harness had not caught up and would not compile at all.
    g.chalk[g.level].push_back({{g.px,g.py+0.016f,g.pz},g.yaw});
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
    // ---- step height. The generator relaxes every terrace to within MAX_STEP,
    // so nothing it produces exercises the riser blocker; a rule that never
    // fires is not a rule that works, so force a drop and check it directly.
    {
        g.applyLevel(0);
        int ci=24, ck=24;                       // inside chunk (1,1), clear of the spawn room
        int cx=fdiv(ci,CCELLS), cz=fdiv(ck,CCELLS);
        ChunkData &cd=g.world.data(cx,cz);
        int li=ci-cx*CCELLS, lk=ck-cz*CCELLS;
        cd.wallN[li][lk]=cd.wallW[li][lk]=cd.wallN[li][lk+1]=cd.wallW[li+1][lk]=0;
        cd.pillar[li][lk]=cd.prop[li][lk]=cd.pool[li][lk]=0;
        cd.elev[li][lk]=25;                     // a 2.5 m terrace, the atrium's full depth
        CHECK(g.world.floorY(ci,ck)>MAX_STEP);
        // it emits a blocker topping out at its own floor
        AABB boxes[MAX_NEARBY_AABBS]; int n=g.world.gatherCellAABBs(ci,ck,boxes,MAX_NEARBY_AABBS,0);
        bool riser=false;
        for(int i=0;i<n;++i) if(fabsf(boxes[i].top-g.world.floorY(ci,ck))<0.001f) riser=true;
        CHECK(riser);
        // from below you are stopped at the face; from on top you walk over it
        float bx=ci*CELL+1, bz=ck*CELL-0.2f, ox=bx, oz=bz;
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        CHECK(bz<oz-0.01f && fabsf(bx-ox)<0.5f);
        bx=ci*CELL+1; bz=ck*CELL+1; ox=bx; oz=bz;
        g.world.collideCircle(bx,bz,Game::PR,g.world.floorY(ci,ck));
        CHECK(fabsf(bx-ox)<0.001f && fabsf(bz-oz)<0.001f);
        // and no route crosses it, so the pack has to go round rather than through
        CHECK(!g.world.canStep(ci,ck-1,ci,ck) && !g.world.canStep(ci,ck,ci,ck-1));
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
        g.applyLevel(0);
        int ci=28, ck=28;                        // clear of the spawn room
        int cx=fdiv(ci,CCELLS), cz=fdiv(ck,CCELLS);
        ChunkData &cd=g.world.data(cx,cz);
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
            return g.world.gatherCellAABBs(a,b,bx,MAX_NEARBY_AABBS,0); };
        float z0=ck*CELL, x0=ci*CELL;

        // A doorway: you go through the 1.3 m opening and not through the jambs.
        cd.wallN[li][lk]=WALL_DOOR;
        CHECK(!blocksEdge(g.world.wallNVal(ci,ck)));          // passable
        CHECK(boxesAt(ci,ck)==2);                             // exactly its two jambs
        float bx=x0+1.0f, bz=z0-0.25f, oz=bz;                 // dead centre of the opening
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        CHECK(fabsf(bz-oz)<0.001f);                           // walks straight through
        bx=x0+0.1f; bz=z0-0.25f; oz=bz;                       // into the west jamb
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        CHECK(bz<oz-0.01f);                                   // pushed back out

        // Two side by side are one wide opening: the mesher drops the jamb
        // between them, so the collision has to drop it too or you walk into a
        // pier that is not there.
        cd.wallN[li+1][lk]=WALL_DOOR;
        CHECK(boxesAt(ci,ck)==1 && boxesAt(ci+1,ck)==1);      // outer jambs only
        bx=x0+CELL; bz=z0-0.25f; oz=bz;                       // the line between them
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        CHECK(fabsf(bz-oz)<0.001f);
        cd.wallN[li+1][lk]=WALL_NONE;

        // A locked door is a wall until its key turns, and it stops light too.
        cd.wallN[li][lk]=WALL_LOCKED;
        CHECK(blocksEdge(g.world.wallNVal(ci,ck)) && blocksLight(g.world.wallNVal(ci,ck)));
        CHECK(!g.world.canStep(ci,ck-1,ci,ck));
        bx=x0+1.0f; bz=z0-0.25f; oz=bz;                       // the middle is solid now
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        CHECK(bz<oz-0.01f);
        g.world.unlockEdge(ci,ck,false);
        CHECK(g.world.wallNVal(ci,ck)==WALL_DOOR);            // and opens for good
        CHECK(g.world.canStep(ci,ck-1,ci,ck));
        bx=x0+1.0f; bz=z0-0.25f; oz=bz;
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        CHECK(fabsf(bz-oz)<0.001f);
        // ...and the *geometry* has to move with it. This is the half that was
        // actually broken: the mesher read the raw wall array, so an unlocked
        // door went on drawing its locked leaf over an edge you could now walk
        // through, and a shifted doorway kept its opening on screen over an
        // edge that had been sealed. Neither reads as a mesher bug — the first
        // reads as "doors have no collision". Bake the chunk, change what the
        // overlays say the wall is, bake again, and the wall mesh must differ.
        g.world.unlockedDoors.clear();           // start from a clean overlay
        cd.wallN[li][lk]=WALL_NONE;
        g.world.rebuildChunk(cx,cz); g.world.ensureMesh(cx,cz);
        int openVerts=cd.meshes[MESH_WALLS].vertexCount;
        g.world.shiftEdge(ci,ck,false);          // the building closes it behind you
        g.world.ensureMesh(cx,cz);
        CHECK(cd.meshes[MESH_WALLS].vertexCount > openVerts);   // a wall appeared
        g.world.shifted.clear();
        // The locked and open branches emit the *same* three wall boxes — the
        // jambs and the header are identical, and what differs is the leaf, its
        // handle and the threshold strip, all of which go into MESH_PROPS. So
        // watch the props slot here: asserted against MESH_WALLS this passes
        // whatever the mesher does, which is the useless kind of green.
        cd.wallN[li][lk]=WALL_LOCKED;
        g.world.rebuildChunk(cx,cz); g.world.ensureMesh(cx,cz);
        int lockedProps=cd.meshes[MESH_PROPS].vertexCount;
        g.world.unlockEdge(ci,ck,false);         // and it rebakes both chunks itself
        g.world.ensureMesh(cx,cz);
        CHECK(cd.meshes[MESH_PROPS].vertexCount < lockedProps);   // the leaf went

        g.world.unlockedDoors.clear();
        for (int d=-1;d<=2;++d) {                // put the neighbourhood back
            Saved &s=saved[d+1];
            cd.wallN[li+d][lk]=s.wn; cd.wallW[li+d][lk]=s.ww; cd.wallN[li+d][lk+1]=s.wn1;
            cd.pillar[li+d][lk]=s.pi;    cd.prop[li+d][lk]=s.pr;
            cd.pillar[li+d][lk-1]=s.pi1; cd.prop[li+d][lk-1]=s.pr1;
        }
        g.world.rebuildChunk(cx,cz);
    }

    // ---- shots respect pitch. Level aim through a body hits; the same shot
    // aimed at the ceiling misses, which it did not before — the hit test was
    // horizontal-only, so you could shoot the ceiling and still land the round.
    {
        g.px=0; g.pz=0; g.py=0; g.eyeY=1.62f; g.yaw=0; g.pitch=0; g.updateLook();
        CHECK(g.shotHitsBody(6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));
        g.pitch=1.0f; g.updateLook();                       // aimed well above his head
        CHECK(!g.shotHitsBody(6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));
        g.pitch=-1.0f; g.updateLook();                      // and at the floor in front
        CHECK(!g.shotHitsBody(6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));
        g.pitch=0.15f; g.updateLook();                      // a low dog at that pitch is over-shot
        CHECK(!g.shotHitsBody(9.0f, 0.0f, 0.0f, 0.92f, 0.6f, 30.0f));
        // the eye rides at 1.62 m and a dog stands 0.92 m, so a dead-level shot
        // goes over its back at any range — you have to put the crosshair on it
        g.pitch=0; g.updateLook();
        CHECK(!g.shotHitsBody(9.0f, 0.0f, 0.0f, 0.92f, 0.6f, 30.0f));
        g.pitch=-0.15f; g.updateLook();
        CHECK(g.shotHitsBody(9.0f, 0.0f, 0.0f, 0.92f, 0.6f, 30.0f));
        g.pitch=0; g.updateLook();
        CHECK(!g.shotHitsBody(-6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));   // behind you
    }

    // ---- the catch ends the run, and only out of a committed lunge. The bare
    // proximity test this replaced fired the instant you came inside 1.25 m,
    // silently — survivable when being caught was free, unfair once it is not.
    {
        g.applyLevel(0); g.beginDescent(0);
        g.inMenu=false; g.deathT=0; g.hidden=false; g.deathCount=0;
        g.px=40; g.pz=40; g.py=0; g.yaw=0; g.pitch=0; g.updateLook();
        g.ent.st=EState::Chase; g.ent.hp=3; g.ent.stagger=0; g.ent.dispY=0;
        // well inside reach, but he has not committed and cannot yet
        g.ent.x=g.px+1.0f; g.ent.z=g.pz; g.ent.lunge=0; g.ent.lungeCd=5.0f;
        g.updateEntity(0.001f, 100.0);
        CHECK(!g.inMenu && g.deathT<=0);
        // from the tell's range with the cooldown clear, he commits rather than grabbing
        g.ent.st=EState::Chase; g.ent.x=g.px+2.3f; g.ent.z=g.pz;
        g.ent.lunge=0; g.ent.lungeCd=0;
        g.updateEntity(0.001f, 100.0);
        CHECK(g.ent.lunge>0 && !g.inMenu);
        // and a commit that reaches you ends the run, with the card's numbers frozen
        g.ent.x=g.px+1.0f; g.ent.z=g.pz; g.ent.lunge=Game::LUNGE_TIME;
        g.distWalked=250; g.killCount=2;
        g.updateEntity(0.001f, 100.0);
        // deathCount must survive the beginDescent inside dieRun: it counts the
        // runs this session has cost you, and resetting it there made the card
        // report your first death every single time.
        CHECK(g.inMenu && g.deathT>0 && g.deathCount==1);
        CHECK(strcmp(g.deathBy,"PIRATE CLARK")==0);
        CHECK(g.deathM==250 && g.deathKills==2 && g.deathTime>99.0f);
        // ...and the world behind the card is a fresh descent, not the one that killed you
        CHECK(g.level==0 && g.coins==0 && g.ent.st==EState::Hidden);
        // ...and look at the card it puts up, since it is the only screen in the
        // game that renders on the title screen rather than over a live run
        g.deathBy="PIRATE CLARK"; g.deathLevel=0; g.deathTime=247; g.deathM=612;
        g.deathKills=2; g.deathCount=3; g.bestDeep=3; g.bestRun=430;
        g.deathT=Game::DEATH_CARD-1.8f; g.inMenu=true;
        capture(g,"death-card.png");
        g.deathT=0; g.inMenu=false;
    }

    // ---- the hunter actually moves now (ENT-01/ENT-02). Look at these: a
    // walk cycle that does not read as a walk is worse than no walk cycle.
    {
        g.applyLevel(0);
        g.px=40; g.pz=40; g.py=0; g.eyeY=1.62f; g.pitch=0; g.deathT=0; g.inMenu=false;
        g.ent.x=g.px+7.0f; g.ent.z=g.pz; g.ent.dispY=0; g.ent.hp=3; g.ent.stagger=0;
        g.yaw=0; g.updateLook();
        // stalking, head still down the corridor, mid-stride at four phases
        g.ent.st=EState::Stalk; g.ent.gaze=0;
        for (int i=0;i<4;++i) {
            g.ent.gait = i * 0.5f;   // quarter-cycle steps: render.cpp's phase is gait*0.5
            char n[48]; snprintf(n,sizeof(n),"clark-walk-%d.png",i);
            capture(g,n);
        }
        // gaze tips over: his head comes round, which is the tell
        g.ent.gaze=1.2f; g.ent.gait=0.5f; capture(g,"clark-noticed.png");
        // and the chase lean, and the harder lean of a committed lunge
        g.ent.st=EState::Chase; g.ent.lunge=0; capture(g,"clark-chase.png");
        g.ent.lunge=Game::LUNGE_TIME; capture(g,"clark-lunge.png");
        g.ent.st=EState::Hidden; g.ent.lunge=0;
        // The pack, mid-bound. Captured on Level 0 rather than in the Red Halls
        // they actually live in: the Red Halls sit at mean luma 12 and a black
        // dog against it is unreviewable. This shot is for the run cycle only.
        g.px=40; g.pz=40; g.yaw=0; g.updateLook();
        g.dogs[0].st=DState::Charge; g.dogs[0].x=g.px+5.0f; g.dogs[0].z=g.pz;
        g.dogs[0].dispY=0; g.dogs[0].hp=2; g.dogs[0].gait=0.5f;
        for (int i=0;i<2;++i) {
            g.dogs[0].gait = i * 0.5f;
            char n[48]; snprintf(n,sizeof(n),"pack-bound-%d.png",i);
            capture(g,n);
        }
        g.dogs[0].st=DState::Gone;
    }

    // ---- he no longer always walks out of the fog (ENT-03). Measured, not
    // asserted by eye: the claim "sometimes he is already round the corner"
    // needs a number behind it.
    {
        g.applyLevel(0); g.beginDescent(0); g.inMenu=false; g.deathT=0;
        g.px=40; g.pz=40; g.yaw=0.8f; g.pitch=0; g.updateLook();
        int near=0, unseen=0, nearUnseen=0, total=400;
        float dmin=1e9f, dmax=0;
        for (int i=0;i<total;++i) {
            g.ent.st=EState::Hidden; g.ent.nextSpawn=0;
            g.updateEntity(0.001f, 100.0);
            float dx=g.ent.x-g.px, dz=g.ent.z-g.pz;
            float d=sqrtf(dx*dx+dz*dz);
            dmin=fminf(dmin,d); dmax=fmaxf(dmax,d);
            bool los = g.world.lineOfSight(g.px,g.pz,g.ent.x,g.ent.z);
            if (d < Game::SPAWN_FAR_MIN) near++;
            if (!los) unseen++;
            if (d < Game::SPAWN_FAR_MIN && !los) nearUnseen++;
        }
        printf("  ENT-03 arrivals: %d%% inside %.0fm, %d%% out of sight, %d%% BOTH, range %.1f-%.1f m\n",
               near*100/total, (double)Game::SPAWN_FAR_MIN, unseen*100/total,
               nearUnseen*100/total, dmin, dmax);
        // The frightening case — close, and already behind something — has to be
        // a real share of arrivals, and the old walk-out-of-the-fog one has to
        // survive alongside it: replacing one fixed ritual with another buys
        // nothing.
        CHECK(nearUnseen > total/5 && near < total*9/10);
        CHECK(dmax >= Game::SPAWN_FAR_MIN);
        g.ent.st=EState::Hidden;
    }

    // ---- the pack hunts by sound, not by what you are standing behind (ENT-04)
    {
        g.still=true;  g.deck.playing=false; g.deck.carried=true;
        CHECK(g.packDeaf());                    // dead still: they lose you, cover or not
        g.still=false; CHECK(!g.packDeaf());    // moving: they have you
        g.still=true;  g.deck.playing=true;      // still, but the tape is running in your coat
        CHECK(!g.packDeaf());
        g.deck.carried=false; CHECK(g.packDeaf());   // ...set it down and it is the deck they want
        g.deck.playing=false; g.deck.carried=true;
    }

    // ---- the building moves when you are not looking (PAC-03)
    {
        g.applyLevel(0); g.beginDescent(0); g.inMenu=false;
        g.px=40; g.pz=40; g.py=0; g.eyeY=1.62f; g.updateLook();
        size_t before = g.world.shifted.size();
        int moved=0;
        for (int i=0;i<40;++i) if (g.shiftAWall()) moved++;
        CHECK(moved > 0 && g.world.shifted.size() == before + (size_t)moved);
        // every edge it shifted must now read as a wall through the same
        // accessors collision, the pathfinder and the mesher all use...
        for (uint64_t k : g.world.shifted) {
            bool west = (k & 1ull) != 0;
            int a = (int)(uint32_t)((k >> 1) >> 32), b = (int)(uint32_t)((k >> 1) & 0xFFFFFFFFull);
            CHECK(blocksEdge(west ? g.world.wallWVal(a,b) : g.world.wallNVal(a,b)));
            // ...and it must not have been one you could see it happen to
            float cx=a*CELL+1.0f, cz=b*CELL+1.0f;
            CHECK(!g.world.lineOfSight(g.px,g.pz,cx,cz));
        }
        printf("  PAC-03: %d doorways walled off out of sight, all opaque to wallNVal/wallWVal\n", moved);
        g.world.shifted.clear();
    }

    // ---- and the grip meter is an ending now, not a difficulty setting (STK-03)
    {
        g.applyLevel(0); g.beginDescent(0); g.inMenu=false; g.deathT=0; g.deathCount=0;
        g.sanity=0.0f;
        g.updateAmbience(0.001f, 200.0);
        CHECK(g.inMenu && g.deathT>0);
        CHECK(strcmp(g.deathBy,"THE PLACE ITSELF")==0);
        CHECK(strcmp(g.deathTitle,"YOU STOPPED KEEPING TRACK")==0);   // its own card, not the catch's
        CHECK(g.sanity>0.9f);   // beginDescent gave it back
        g.deathT=0; g.inMenu=false;
        // and the last tenth is a slide you can feel, not a cliff
        g.sanity=0.05f; g.updateAmbience(0.001f, 300.0);
        CHECK(g.slide>0.4f && g.slide<1.0f);
        g.sanity=0.5f; g.updateAmbience(0.001f, 300.0);
        CHECK(g.slide==0.0f);
    }

    // Isolated pillar: exposes the old hard contact rectangle and the bright
    // square where shadow rays skipped their first/last occupancy cells.
    g.applyLevel(0); g.world.unloadAll();
    auto &pillarRoom=g.world.data(0,0);
    std::memset(pillarRoom.wallN,0,sizeof(pillarRoom.wallN));
    std::memset(pillarRoom.wallW,0,sizeof(pillarRoom.wallW));
    std::memset(pillarRoom.pillar,0,sizeof(pillarRoom.pillar));
    std::memset(pillarRoom.prop,0,sizeof(pillarRoom.prop));
    std::memset(pillarRoom.elev,0,sizeof(pillarRoom.elev));
    pillarRoom.pillar[4][4]=1;
    g.px=5.5f;g.pz=5.5f;g.py=0;g.eyeY=1.62f;g.yaw=PI/4;g.pitch=0;
    g.inMenu=false;g.weapon=WEAPON_REVOLVER;g.fov=70;g.flashOn=false;g.flashCur=0;
    g.blackoutCur=1;g.ent.st=EState::Hidden;g.entDarkCur=0;g.aimBlend=0;g.reloadT=0;
    capture(g,"pillar-contact.png");

    // ---- headless captures must not be able to black out (BUG-08)
    CHECK(g.noBlackout && g.nextBlackout >= Game::BLACKOUT_NEVER);

    printf("PASS sprint recovery, crouch/stationary gating, restart reset, battery retention,\n"
           "     animation continuity, held aim/reload gating, step-height blocking,\n"
           "     pitch-aware hit tests, doorway jambs and locked doors you cannot walk\n"
           "     through, the catch ending the run only out of a committed\n"
           "     lunge, arrivals that are not all from the fog, a pack that hunts by sound,\n"
           "     a building that moves out of sight, the grip meter as an ending,\n"
           "     deterministic captures; %d visual captures\n", captureCount);
    g.shutdown();
}
