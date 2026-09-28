#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsChunkActor.generated.h"

struct World;
class UBackroomsLevelLook;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;

// One chunk of one storey, spawned by UBackroomsWorldSubsystem at its storey's
// height. The mesh is in that storey's own frame: the greybox with the level's
// look (BackroomsLevelLook.h), plus its prop and fitting meshes. A Blueprint
// subclass can add to it; set it in Project Settings > Game > Backrooms.
UCLASS(Blueprintable, Transient)
class BACKROOMS_API ABackroomsChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsChunkActor();

	void Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look);

private:
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Instances;
};
