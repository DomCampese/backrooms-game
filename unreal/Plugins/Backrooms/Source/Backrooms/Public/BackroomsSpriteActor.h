#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsTypes.h"
#include <vector>
#include "BackroomsSpriteActor.generated.h"

struct ActorSprite;
class UBackroomsLevelLook;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

// The hunter and the pack as the Web build draws them: billboards cut from
// the game's own sprite sheets, the walk frames cross-faded and the row picked
// by shared/port/sprites.h. The editor builds the sheets and the two materials
// (backrooms_looks.py); until it has, or where a level look gives the hunter or
// the dog a mesh, the scene actor draws them instead.
UCLASS(Transient)
class BACKROOMS_API ABackroomsSpriteActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsSpriteActor();

	// Whether this actor draws Kind (the hunter or a dog) under Look.
	bool Draws(EBackroomsItem Kind, const UBackroomsLevelLook* Look);
	// Sprites are in the sim's frame, which Origin places in the world. Each
	// faces Camera about the vertical.
	void Show(const std::vector<ActorSprite>& Sprites, const UBackroomsLevelLook* Look, const FVector& Camera,
		const FVector& Origin);

private:
	// Loads the sheets and materials named in the settings, once.
	bool Ready();

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Body;
	// The Smiler's eyes and grin, unlit and sorted over the body.
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Glow;
	// One per SpriteSheet, then the glow.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	bool bTried = false;
};
