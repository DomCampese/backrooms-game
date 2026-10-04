// Replays a trace recorded by the game (BACKROOMS_RECORD=path) through the sim
// alone, with no window, renderer or raylib, and compares the state after
// every frame with the recording (shared/sim/trace.h). Exit 0 when every digest
// matches, 1 at the first that does not.
//
//   tools/sandbox-build.sh replay
//   ./replay run.trace
#include "../shared/sim/trace.h"
#include <cstdio>

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: replay TRACE\n", stderr);
        return 2;
    }
    ReplayResult r = replayTrace(argv[1]);
    printf("%s %s\n", r.ok ? "ok  " : "FAIL", r.report.c_str());
    return r.ok ? 0 : 1;
}
