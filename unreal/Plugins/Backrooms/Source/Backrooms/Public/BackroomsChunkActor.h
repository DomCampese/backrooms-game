#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsChunkActor.generated.h"

struct GreyboxMesh;
class UProceduralMeshComponent;

// One chunk of one storey, drawn as core's greybox (src/port/greybox.h): a
// mesh section per surface kind, each a flat colour. The subsystem places the
// actor at its storey's height; the mesh is in that storey's own frame.
UCLASS(NotBlueprintable, Transient)
class BACKROOMS_API ABackroomsChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsChunkActor();

	void Build(const GreyboxMesh& Greybox);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Mesh;
};
