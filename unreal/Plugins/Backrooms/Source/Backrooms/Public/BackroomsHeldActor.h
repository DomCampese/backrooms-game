#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "port/draw.h"
#include "port/revolver.h"
#include <vector>
#include "BackroomsHeldActor.generated.h"

struct Sim;
class UBackroomsRender;
class UProceduralMeshComponent;

// The revolver in hand, drawn as the Web build draws it: the repository's
// assets/models/revolver.glb, read at run time, posed and skinned by
// shared/port/revolver.h (raylib 5.5's arithmetic, so the reload, the shots and
// the drum's turn are the Web build's frame for frame), placed by heldWeapon
// and drawn through the world shader with its steel reflecting the room. The
// can, the deck and a flare in hand are the scene actor's (port/draw.h).
UCLASS(Transient)
class BACKROOMS_API ABackroomsHeldActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsHeldActor();

	// Origin places the sim's storey frame in the world. Adds the muzzle flash
	// and smoke to Draws when the revolver is shown. Nothing is in hand while
	// the camcorder is at the eye (bInHand false).
	void Show(const Sim& Game, bool bInHand, const UBackroomsRender& Render, const FVector& Origin, std::vector<SceneDraw>& Draws);

private:
	bool Load();

	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UProceduralMeshComponent> Gun;
	RevolverModel Model;
	bool bTried = false;
	bool bLoaded = false;
	// Each primitive's skinned vertices, kept until the pose changes.
	std::vector<std::vector<float>> Positions, Normals;
};
