#pragma once
// Which chunks a view streams in, nearest first: the storey you are on in
// rings, then on the storeys above and below only the chunks round an opening
// that links to them, then up or down a tall court one chunk a storey. Every
// other chunk of another storey is behind a closed slab. Both builds walk this
// order and decide for themselves what building a chunk is and when a frame
// has built enough.
#include "../core/world.h"
#include <functional>

struct StreamChunk {
    int cx, cz, storey;
    // On the storey just above or below, which the shadow grid carries
    // (bytes 1 and 2): when it arrives, rebuild the grid, or the light that
    // should fall down the opening waits until the grid next moves.
    bool inShadowGrid;
};

// The walk for a view in chunk (pcx, pcz) of world.storey, out to `rings`
// chunks on its own storey. `want` is called once per chunk with world.qs at
// the chunk's storey, and returns false to stop the walk.
void streamChunks(World &w, int pcx, int pcz, int rings, const std::function<bool(const StreamChunk &)> &want);
