#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsPreviewActor.generated.h"

class UBackroomsLevelLook;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;

// Shows the generated maze in the editor viewport, without pressing Play, for
// working on a level's look and lighting. Drop one into a map; it builds the
// chunks round wherever it stands, at the height the game puts them (storey s
// at s pitches up, world origin where the game's is), so anything placed
// beside it lines up with the game. It removes itself when play begins, where
// the game streams the same chunks.
UCLASS(Blueprintable, HideCategories = (Collision, Physics, Input, Replication, LOD, Cooking, HLOD))
class BACKROOMS_API ABackroomsPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsPreviewActor();

	UPROPERTY(EditAnywhere, Category = "Backrooms", meta = (ClampMin = 0, ClampMax = 4))
	int32 Level = 0;
	UPROPERTY(EditAnywhere, Category = "Backrooms")
	int32 Seed = 1337;
	// Which arrival at the level: each visit is a different maze. A raylib
	// capture shows Level 0 at visit 1 and other levels at visit 0.
	UPROPERTY(EditAnywhere, Category = "Backrooms", meta = (ClampMin = 0))
	int32 Visit = 1;
	UPROPERTY(EditAnywhere, Category = "Backrooms")
	int32 Storey = 0;
	// Chunks (32 m) each way round the actor's position.
	UPROPERTY(EditAnywhere, Category = "Backrooms", meta = (ClampMin = 0, ClampMax = 3))
	int32 Radius = 1;
	// Also the storeys above and below, where the level has storeys.
	UPROPERTY(EditAnywhere, Category = "Backrooms")
	bool bNeighbourStoreys = false;
	// Tries a look out here; empty uses the level's look from the project
	// settings, and that empty draws the greybox.
	UPROPERTY(EditAnywhere, Category = "Backrooms")
	TObjectPtr<UBackroomsLevelLook> Look;
	// Rebuild whenever a property changes or the actor moves. Turn off while
	// dragging a large preview about.
	UPROPERTY(EditAnywhere, Category = "Backrooms")
	bool bAutoRebuild = true;

	UFUNCTION(CallInEditor, Category = "Backrooms")
	void Rebuild();
	UFUNCTION(CallInEditor, Category = "Backrooms")
	void Clear();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Instances;
};
