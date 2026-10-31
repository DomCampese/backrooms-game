#include "stream.h"
#include <algorithm>
#include <cstdlib>

void streamChunks(World &w, int pcx, int pcz, int rings, const std::function<bool(const StreamChunk &)> &want) {
    bool more = true;
    for (int r = 0; r <= rings && more; r++)
        for (int dx = -r; dx <= r && more; dx++)
            for (int dz = -r; dz <= r && more; dz++) {
                if (std::max(abs(dx), abs(dz)) != r) continue;
                more = want({ pcx + dx, pcz + dz, w.qs, false });
            }
    if (w.storeyH <= 0.0f || !more) return;
    // A feature's own chunk holds the opening; its neighbours are what is
    // visible past its edges.
    for (int rel = -1; rel <= 1 && more; rel += 2)
        for (int dx = -1; dx <= 1 && more; dx++)
            for (int dz = -1; dz <= 1 && more; dz++) {
                if (!w.linksStorey(pcx + dx, pcz + dz, rel)) continue;
                StoreyScope sc(w, w.storey + rel);
                for (int ex = -1; ex <= 1 && more; ex++)
                    for (int ez = -1; ez <= 1 && more; ez++) {
                        if (abs(dx + ex) > rings || abs(dz + ez) > rings) continue;
                        int cx = pcx + dx + ex, cz = pcz + dz + ez;
                        more = want({ cx, cz, w.qs, true });
                        // Through a tall court, one chunk a storey; an
                        // ordinary room stops the chain at once.
                        for (int depth = 2; depth <= STOREY_REACH && more; ++depth) {
                            StoreyScope prev(w, w.storey + (depth - 1) * rel);
                            VertFeat link;
                            if (!w.pairFeature(cx, cz, w.storey + (rel > 0 ? depth - 1 : -depth), link)) break;
                            StoreyScope next(w, w.storey + depth * rel);
                            more = want({ cx, cz, w.qs, false });
                        }
                    }
            }
}
