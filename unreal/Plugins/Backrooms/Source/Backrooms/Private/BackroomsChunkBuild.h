#pragma once
#include "CoreMinimal.h"

struct World;
class UBackroomsRender;
class UProceduralMeshComponent;

namespace BackroomsChunkBuild
{
// Builds chunk (Cx, Cz) of storey W.qs into Mesh, in that storey's frame,
// from the Web build's mesher (shared/port/chunk_mesh.h): each mesh slot as a
// masked section and, where the slot has translucent faces, a blended one,
// drawn with Render's world materials. bCollision gives the slots the Web
// build's bullets hit (MeshTracer) query collision.
void Build(World& W, int32 Cx, int32 Cz, const UBackroomsRender& Render, UProceduralMeshComponent& Mesh, bool bCollision);
}
