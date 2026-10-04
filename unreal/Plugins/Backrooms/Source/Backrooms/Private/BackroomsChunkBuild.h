#pragma once
#include "CoreMinimal.h"

struct World;
class UBackroomsLevelLook;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;

namespace BackroomsChunkBuild
{
// Builds chunk (Cx, Cz) of storey W.qs into Mesh, in that storey's frame: the
// greybox, one mesh section per surface, drawn with Look's materials or its
// colours; and, attached to Mesh, one instanced static mesh component per
// fixture kind (Look's mesh or a plain box), and per prop kind and for the
// light fittings where Look gives a mesh (added to Instances).
// Look may be null: the plain greybox. bCollision gives every solid section
// query collision, for the sim's bullets.
void Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look, UProceduralMeshComponent& Mesh,
	bool bCollision, TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances);
}
