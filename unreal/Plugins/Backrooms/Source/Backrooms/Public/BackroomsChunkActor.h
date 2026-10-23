#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsChunkActor.generated.h"

struct World;
class UBackroomsRender;
class UProceduralMeshComponent;

// One chunk of one storey, spawned by UBackroomsWorldSubsystem at its storey's
// height. The mesh is in that storey's own frame: the Web build's chunk mesh
// (shared/port/chunk_mesh.h), drawn with its world shader (UBackroomsRender). A
// Blueprint subclass can add to it; set it in Project Settings > Game >
// Backrooms.
UCLASS(Blueprintable, Transient)
class BACKROOMS_API ABackroomsChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsChunkActor();

	void Build(World& W, int32 Cx, int32 Cz, const UBackroomsRender& Render);

private:
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UProceduralMeshComponent> Mesh;
};
