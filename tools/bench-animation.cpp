// Full-scene benchmark with continuous reload/shot animation. Compile against
// either revision, then compare the binaries with tools/bench.sh.
#include "game.h"
#include <cstdlib>
int main() {
    setenv("BACKROOMS_BENCH","1",1);
    setenv("BACKROOMS_SEED","1337",1);
    setenv("BACKROOMS_TIME","4",1);
    setenv("BACKROOMS_CLEAN","1",1);
    Game g;g.init();EnableCursor();
    Sim &sim=g.sim;
    sim.px=15;sim.pz=15;sim.py=0;sim.eyeY=1.62f;sim.yaw=.8f;sim.pitch=0;
    sim.weapon=WEAPON_REVOLVER;sim.updateLook(InputFrame{});
    for(int i=0;i<7;++i)g.streamChunks();g.updateOccupancy();
    for(int frame=0;frame<300;++frame) {
        int cycle=frame%150;
        sim.reloadT=cycle<108?RELOAD_TIME*(1-cycle/108.0f):0;
        sim.gunCd=cycle>=108?.42f*(1-(cycle-108)%25/25.0f):0;
        sim.ammo=cycle<108?0:5-(cycle-108)/25;
        double start=GetTime();
        g.renderScene(4);g.renderUI(4);
        if(frame>=60)g.frameSamples.push_back((float)((GetTime()-start)*1000));
    }
    g.shutdown();
}
