#pragma once
// Which streamed chunks a camera can see, nearest first: those of its own
// storey inside the view frustum, then those of other storeys that an opening
// in view shows. Outside a feature's footprint the slab between two storeys is
// closed (ceiling below, floor above, fascias round the edge), so every line
// of sight into another storey crosses the plane of the upper floor inside
// that footprint; a chunk of another storey is visible only if its box,
// projected from the eye onto that plane, overlaps the footprint. Drawing
// every chunk round every link in the frustum put 22 chunks of other storeys
// behind the 9 of this one and cost a tenth of the Web build's frame.
#include "../core/world.h"
#include <functional>
#include <vector>

struct VisibleChunk {
    int storey, cx, cz;
    float yOff;          // drawn this far up or down, metres: a whole number of pitches
    float distance2;     // from the body to the chunk's box centre, squared
};

// The view as render.cpp frames it. The frustum is tested from the body
// (`body`, facing `fwd`, the sim's), the portals from the camera (`eye`).
struct ChunkViewer {
    Vec3 body, fwd;
    Vec3 eye, up;
    float fovY;          // vertical, degrees
    int width, height;   // the frame, pixels
};

// `loaded(storey, cx, cz)`: the platform has that chunk to draw.
std::vector<VisibleChunk> visibleChunks(World &w, const ChunkViewer &v,
                                        const std::function<bool(int, int, int)> &loaded);
