#include "BackroomsPreviewActor.h"
#include "BackroomsChunkBuild.h"
#include "BackroomsCoords.h"
#include "BackroomsLevelLook.h"
#include "BackroomsSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "core/level_rules.h"
#include "core/world.h"

ABackroomsPreviewActor::ABackroomsPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetActorHiddenInGame(true);
}

void ABackroomsPreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (bAutoRebuild && GetWorld() && !GetWorld()->IsGameWorld())
	{
		Rebuild();
	}
}

void ABackroomsPreviewActor::BeginPlay()
{
	Super::BeginPlay();
	Clear();
}

void ABackroomsPreviewActor::Clear()
{
	for (const TObjectPtr<UInstancedStaticMeshComponent>& Component : Instances)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	for (const TObjectPtr<UProceduralMeshComponent>& Component : Meshes)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	Instances.Reset();
	Meshes.Reset();
}

void ABackroomsPreviewActor::Rebuild()
{
	Clear();
	const int32 Lv = FMath::Clamp(Level, 0, NLEVELS - 1);
	const UBackroomsLevelLook* LevelLook = Look ? Look.Get() : GetDefault<UBackroomsSettings>()->LookFor(Lv);
	TUniquePtr<World> Maze = MakeUnique<World>();
	Maze->seed = (uint32)Seed;
	Maze->level = Lv;
	Maze->visit = (uint32)FMath::Max(Visit, 0);
	Maze->wallH = LEVEL_RULES[Lv].wallH;
	Maze->storeyH = LEVEL_RULES[Lv].storeyH;
	const int32 Base = Maze->storeyH > 0.0f ? Storey : 0;
	Maze->setStorey(Base);

	const Vec3 At = BackroomsCoords::FromUnreal(GetActorLocation());
	const int32 Pcx = fdiv(cellOf(At.x), CCELLS), Pcz = fdiv(cellOf(At.z), CCELLS);
	const int32 Rel = bNeighbourStoreys && Maze->storeyH > 0.0f ? 1 : 0;
	for (int32 S = Base - Rel; S <= Base + Rel; S++)
	{
		StoreyScope Scope(*Maze, S);
		for (int32 Dz = -Radius; Dz <= Radius; Dz++)
		{
			for (int32 Dx = -Radius; Dx <= Radius; Dx++)
			{
				UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
				Mesh->SetupAttachment(RootComponent);
				// Where the game puts this chunk, whatever the actor's transform.
				Mesh->SetUsingAbsoluteLocation(true);
				Mesh->SetUsingAbsoluteRotation(true);
				Mesh->SetUsingAbsoluteScale(true);
				Mesh->SetWorldTransform(FTransform(FVector(0.0, 0.0, BackroomsCoords::StoreyZ(S, Maze->storeyH))));
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mesh->RegisterComponent();
				BackroomsChunkBuild::Build(*Maze, Pcx + Dx, Pcz + Dz, LevelLook, *Mesh, false, Instances);
				Meshes.Add(Mesh);
			}
		}
	}
}
