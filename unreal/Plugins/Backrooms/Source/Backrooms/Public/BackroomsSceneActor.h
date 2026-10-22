#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "port/draw.h"
#include <vector>
#include "BackroomsSceneActor.generated.h"

class UBackroomsRender;
class UProceduralMeshComponent;

// Draws, each frame, what the Web build draws round the chunks
// (shared/port/draw.h): the loose objects through the world shader, and
// raylib's unlit shapes and billboards (the hunter, the pack, flares, coins,
// balloons, chalk, rounds in flight, impacts, the mist, the muzzle flash) lit
// as render.cpp lights them. One procedural mesh, a section per material,
// rebuilt every frame in the world's coordinates.
UCLASS(Transient)
class BACKROOMS_API ABackroomsSceneActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsSceneActor();

	// Draws are in the sim's frame, which Origin places in the world; those
	// from FirstHeld on are in hand, which the room does not shadow. View is
	// the eye billboards face as raylib's do.
	void Show(const std::vector<SceneDraw>& Draws, size_t FirstHeld, const SimView& View, const UBackroomsRender& Render,
		const FVector& Origin);

private:
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UProceduralMeshComponent> Mesh;
};
