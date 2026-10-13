#pragma once
#include "CoreMinimal.h"
#include "core/layout.h"

class UBackroomsLevelLook;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;

// A chunk's live fittings, and their panels: instance i of Panels is
// Fittings[i], its custom data 0 the panel's brightness (1 full).
struct FBackroomsLitFittings
{
	TArray<LightFitting> Fittings;
	UInstancedStaticMeshComponent* Panels = nullptr;
};

namespace BackroomsChunkBuild
{
// Builds chunk (Cx, Cz) of storey W.qs into Mesh, in that storey's frame: the
// greybox, one mesh section per surface, drawn with Look's materials or its
// colours; and, attached to Mesh, one instanced static mesh component per
// fixture kind (Look's mesh or a plain box), per prop kind where Look gives a
// mesh, and for the light panels (added to Instances, the live ones in Lit).
// Look may be null: the plain greybox. bCollision gives every solid section
// query collision, for the sim's bullets.
void Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look, UProceduralMeshComponent& Mesh,
	bool bCollision, TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances, FBackroomsLitFittings& Lit);
}
