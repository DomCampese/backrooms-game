#include "BackroomsChunkActor.h"
#include "BackroomsChunkBuild.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "ProceduralMeshComponent.h"

ABackroomsChunkActor::ABackroomsChunkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	// Queried by the sim's bullets (FUnrealTracer), against the triangles
	// themselves. Nothing simulates physics against it: core does collision.
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->bUseComplexAsSimpleCollision = true;
	RootComponent = Mesh;
}

void ABackroomsChunkActor::Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look)
{
	BackroomsChunkBuild::Build(W, Cx, Cz, Look, *Mesh, true, Instances);
}
