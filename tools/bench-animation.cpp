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
    g.px=15;g.pz=15;g.py=0;g.eyeY=1.62f;g.yaw=.8f;g.pitch=0;
    g.weapon=WEAPON_REVOLVER;g.updateLook();
    for(int i=0;i<7;++i)g.streamChunks();g.updateOccupancy();
    for(int frame=0;frame<300;++frame) {
        int cycle=frame%150;
        g.reloadT=cycle<108?1.8f*(1-cycle/108.0f):0;
        g.gunCd=cycle>=108?.42f*(1-(cycle-108)%25/25.0f):0;
        g.ammo=cycle<108?0:5-(cycle-108)/25;
        double start=GetTime();
        g.renderScene(4);g.renderUI(4);
        if(frame>=60)g.frameSamples.push_back((float)((GetTime()-start)*1000));
    }
    g.shutdown();
}
