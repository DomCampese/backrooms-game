#include "object_meshes.h"
#include "mesh_upload.h"
#include "port/objects.h"

Mesh buildCanMesh() { return uploadMesh(canMesh()); }
Mesh buildDeckMesh() { return uploadMesh(deckMesh()); }
Mesh buildReelMesh() { return uploadMesh(reelMesh()); }
Mesh buildDeckLampMesh() { return uploadMesh(deckLampMesh()); }
Mesh buildFlareMesh() { return uploadMesh(flareMesh()); }
Mesh buildCrateMesh() { return uploadMesh(crateMesh()); }
Mesh buildCrateLidMesh() { return uploadMesh(crateLidMesh()); }
