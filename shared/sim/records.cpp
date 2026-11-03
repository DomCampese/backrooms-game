#include "../core/fp_strict.h"
#include "records.h"
#include <cstdio>

// The level numbering the file's `deepest` uses. Files without the field
// predate the Red Halls' removal, when they were level 3 and LEVEL FUN and the
// theater were 4 and 5; OLD_LEVEL maps those numbers to these. A record of
// the Red Halls becomes Level 0, where the exits to them were.
constexpr int RECORDS_NUMBERING = 2;
constexpr int OLD_LEVEL[6] = { 0, 1, 2, 0, LEVEL_FUN, THEATER };

Records loadRecords(const char *path) {
    Records b;
    if (FILE *bf = fopen(path, "r")) {
        if (fscanf(bf, "%d %d %d", &b.escapes, &b.kills, &b.metres) != 3) b.escapes = b.kills = b.metres = 0;
        if (fscanf(bf, "%d", &b.wins) != 1) b.wins = 0;
        if (fscanf(bf, "%d", &b.tapes) != 1) b.tapes = 0;
        if (fscanf(bf, "%d %d", &b.deepest, &b.longestRun) != 2) b.deepest = b.longestRun = 0;
        int numbering = 1;
        if (fscanf(bf, "%d", &numbering) != 1) numbering = 1;
        fclose(bf);
        if (numbering < RECORDS_NUMBERING) b.deepest = OLD_LEVEL[b.deepest >= 0 && b.deepest < 6 ? b.deepest : 0];
        if (b.deepest < 0 || b.deepest >= NLEVELS) b.deepest = 0;
    }
    return b;
}

bool saveRecords(const char *path, const Records &b) {
    FILE *bf = fopen(path, "w");
    if (!bf) return false;
    fprintf(bf, "%d %d %d %d %d\n%d %d\n%d\n", b.escapes, b.kills, b.metres, b.wins, b.tapes, b.deepest, b.longestRun,
            RECORDS_NUMBERING);
    fclose(bf);
    return true;
}
