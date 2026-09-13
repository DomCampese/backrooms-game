// Native integration checks and reproducible viewmodel captures.
#include "game.h"
#include "raymath.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

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
        assert(g.world.floorY(ci,ck)>MAX_STEP);
        // it emits a blocker topping out at its own floor
        AABB boxes[MAX_NEARBY_AABBS]; int n=g.world.gatherCellAABBs(ci,ck,boxes,MAX_NEARBY_AABBS,0);
        bool riser=false;
        for(int i=0;i<n;++i) if(fabsf(boxes[i].top-g.world.floorY(ci,ck))<0.001f) riser=true;
        assert(riser);
        // from below you are stopped at the face; from on top you walk over it
        float bx=ci*CELL+1, bz=ck*CELL-0.2f, ox=bx, oz=bz;
        g.world.collideCircle(bx,bz,Game::PR,0.0f);
        assert(bz<oz-0.01f && fabsf(bx-ox)<0.5f);
        bx=ci*CELL+1; bz=ck*CELL+1; ox=bx; oz=bz;
        g.world.collideCircle(bx,bz,Game::PR,g.world.floorY(ci,ck));
        assert(fabsf(bx-ox)<0.001f && fabsf(bz-oz)<0.001f);
        // and no route crosses it, so the pack has to go round rather than through
        assert(!g.world.canStep(ci,ck-1,ci,ck) && !g.world.canStep(ci,ck,ci,ck-1));
        cd.elev[li][lk]=0;
    }

    // ---- shots respect pitch. Level aim through a body hits; the same shot
    // aimed at the ceiling misses, which it did not before — the hit test was
    // horizontal-only, so you could shoot the ceiling and still land the round.
    {
        g.px=0; g.pz=0; g.py=0; g.eyeY=1.62f; g.yaw=0; g.pitch=0; g.updateLook();
        assert(g.shotHitsBody(6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));
        g.pitch=1.0f; g.updateLook();                       // aimed well above his head
        assert(!g.shotHitsBody(6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));
        g.pitch=-1.0f; g.updateLook();                      // and at the floor in front
        assert(!g.shotHitsBody(6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));
        g.pitch=0.15f; g.updateLook();                      // a low dog at that pitch is over-shot
        assert(!g.shotHitsBody(9.0f, 0.0f, 0.0f, 0.92f, 0.6f, 30.0f));
        // the eye rides at 1.62 m and a dog stands 0.92 m, so a dead-level shot
        // goes over its back at any range — you have to put the crosshair on it
        g.pitch=0; g.updateLook();
        assert(!g.shotHitsBody(9.0f, 0.0f, 0.0f, 0.92f, 0.6f, 30.0f));
        g.pitch=-0.15f; g.updateLook();
        assert(g.shotHitsBody(9.0f, 0.0f, 0.0f, 0.92f, 0.6f, 30.0f));
        g.pitch=0; g.updateLook();
        assert(!g.shotHitsBody(-6.0f, 0.0f, 0.0f, 1.95f, 0.55f, 60.0f));   // behind you
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
        assert(!g.inMenu && g.deathT<=0);
        // from the tell's range with the cooldown clear, he commits rather than grabbing
        g.ent.st=EState::Chase; g.ent.x=g.px+2.3f; g.ent.z=g.pz;
        g.ent.lunge=0; g.ent.lungeCd=0;
        g.updateEntity(0.001f, 100.0);
        assert(g.ent.lunge>0 && !g.inMenu);
        // and a commit that reaches you ends the run, with the card's numbers frozen
        g.ent.x=g.px+1.0f; g.ent.z=g.pz; g.ent.lunge=Game::LUNGE_TIME;
        g.distWalked=250; g.killCount=2;
        g.updateEntity(0.001f, 100.0);
        // deathCount must survive the beginDescent inside dieRun: it counts the
        // runs this session has cost you, and resetting it there made the card
        // report your first death every single time.
        assert(g.inMenu && g.deathT>0 && g.deathCount==1);
        assert(strcmp(g.deathBy,"PIRATE CLARK")==0);
        assert(g.deathM==250 && g.deathKills==2 && g.deathTime>99.0f);
        // ...and the world behind the card is a fresh descent, not the one that killed you
        assert(g.level==0 && g.coins==0 && g.ent.st==EState::Hidden);
        // ...and look at the card it puts up, since it is the only screen in the
        // game that renders on the title screen rather than over a live run
        g.deathBy="PIRATE CLARK"; g.deathLevel=0; g.deathTime=247; g.deathM=612;
        g.deathKills=2; g.deathCount=3; g.bestDeep=3; g.bestRun=430;
        g.deathT=Game::DEATH_CARD-1.8f; g.inMenu=true;
        capture(g,"death-card.png");
        g.deathT=0; g.inMenu=false;
    }

    // ---- headless captures must not be able to black out (BUG-08)
    assert(g.noBlackout && g.nextBlackout >= Game::BLACKOUT_NEVER);

    printf("PASS sprint recovery, crouch/stationary gating, restart reset, battery retention,\n"
           "     step-height blocking, pitch-aware hit tests, the catch ending the run only out of\n"
           "     a committed lunge, deterministic captures; 14 visual captures\n");
    g.shutdown();
}
