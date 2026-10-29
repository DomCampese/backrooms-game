#include "../core/fp_strict.h"
#include "records.h"
#include <cstdio>

Records loadRecords(const char *path) {
    Records b;
    if (FILE *bf = fopen(path, "r")) {
        if (fscanf(bf, "%d %d %d", &b.escapes, &b.kills, &b.metres) != 3) b.escapes = b.kills = b.metres = 0;
        if (fscanf(bf, "%d", &b.wins) != 1) b.wins = 0;
        if (fscanf(bf, "%d", &b.tapes) != 1) b.tapes = 0;
        if (fscanf(bf, "%d %d", &b.deepest, &b.longestRun) != 2) b.deepest = b.longestRun = 0;
        fclose(bf);
    }
    return b;
}

bool saveRecords(const char *path, const Records &b) {
    FILE *bf = fopen(path, "w");
    if (!bf) return false;
    fprintf(bf, "%d %d %d %d %d\n%d %d\n", b.escapes, b.kills, b.metres, b.wins, b.tapes, b.deepest, b.longestRun);
    fclose(bf);
    return true;
}
