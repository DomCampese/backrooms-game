#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsTypes.h"
#include <vector>
#include "BackroomsSceneActor.generated.h"

struct SceneItem;
class UBackroomsLevelLook;
class UInstancedStaticMeshComponent;

// Draws what the sim puts in the world each frame (shared/port/scene.h): pickups,
// crates, the tape deck, flares, chalk, balloons, the hunter and the pack. One
// instanced mesh per kind (and per colour for balloons and confetti), with the
// level look's mesh or a plain shape at the thing's real size. The subsystem
// spawns one and updates it after every step.
UCLASS(Blueprintable, Transient)
class BACKROOMS_API ABackroomsSceneActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsSceneActor();

	// Items are in the sim's frame; Origin places that frame (the storey the
	// player is on) in the world.
	void Show(const std::vector<SceneItem>& Items, const UBackroomsLevelLook* Look, const FVector& Origin);

private:
	UInstancedStaticMeshComponent* Instances(EBackroomsItem Kind, uint8 Colour, const UBackroomsLevelLook* Look);

	// Keyed by kind * 256 + colour; rebuilt when the look changes.
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UInstancedStaticMeshComponent>> ByKind;
	UPROPERTY(Transient)
	TObjectPtr<const UBackroomsLevelLook> ShownLook;
};
