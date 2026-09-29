#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsHeldActor.generated.h"

struct Sim;
class UAnimSequence;
class UPointLightComponent;
class UPoseableMeshComponent;
class USkeletalMeshComponent;

// The revolver in the player's hand, placed and posed as the raylib build
// draws it (src/port/held.h): the clip and how far through it, the drum's turn
// across shots, the muzzle flash. The model is the repository's
// assets/models/revolver.glb, which the editor imports on first open
// (Plugins/Backrooms/Content/Python); Project Settings > Game > Backrooms names
// the assets.
UCLASS(Blueprintable, Transient)
class BACKROOMS_API ABackroomsHeldActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsHeldActor();

	// Origin places the sim's storey frame in the world.
	void Show(const Sim& Game, const FVector& Origin);

	// Where the revolver got to: not imported, imported wrong, or shown and
	// how big. The HUD prints it until the gun is known to work on the Mac.
	const FString& GetStatus() const { return Status; }

private:
	bool Load();

	// Plays the clip, hidden; the gun copies its pose and turns the drum.
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<USkeletalMeshComponent> Animator;
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UPoseableMeshComponent> Gun;
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UPointLightComponent> Flash;

	// Idle, Reload, Shoot: RevolverClip's order.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Clips;

	TArray<FName> DrumBones;
	FName Handle;
	FString Status = TEXT("revolver: not held yet");
	// Which GLB axis (0 x, 1 y, 2 z) each of the mesh's axes holds, its units
	// per metre, and +1 or -1 as that mapping keeps or mirrors a rotation.
	// Read from the mesh's bounds, so no importer convention is assumed.
	int32 Perm[3] = { 0, 1, 2 };
	float Units = 100.0f;
	float Sense = 1.0f;
	bool bLoaded = false;
	bool bMissing = false;
};
