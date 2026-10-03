#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsTypes.h"
#include "BackroomsHeldActor.generated.h"

struct Sim;
struct RevolverPose;
class UAnimSequence;
class UBackroomsLevelLook;
class UPointLightComponent;
class UPoseableMeshComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

// What the player holds, placed as the raylib build draws it (src/port/held.h).
// The revolver is posed too: the clip and how far through it, the drum's turn
// across shots, the muzzle flash. Its model is the repository's
// assets/models/revolver.glb, which the editor imports on first open
// (Plugins/Backrooms/Content/Python); Project Settings > Game > Backrooms names
// the assets. The can, the tape deck and a flare take the level look's mesh for
// that item, or its plain shape, as the scene actor draws them on the floor.
UCLASS(Blueprintable, Transient)
class BACKROOMS_API ABackroomsHeldActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsHeldActor();

	// Origin places the sim's storey frame in the world.
	void Show(const Sim& Game, const UBackroomsLevelLook* Look, const FVector& Origin);

private:
	void ShowRevolver(const Sim& Game, const FVector& Origin);
	void ShowItem(const Sim& Game, const UBackroomsLevelLook* Look, const FVector& Origin);
	void ReportReload(const RevolverPose& Pose, const UAnimSequence* Clip);
	bool Load();

	// Plays the clip, hidden; the gun copies its pose and turns the drum.
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<USkeletalMeshComponent> Animator;
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UPoseableMeshComponent> Gun;
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UPointLightComponent> Flash;
	// The can, the deck or a flare.
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UStaticMeshComponent> Item;
	UPROPERTY(Transient)
	TObjectPtr<const UBackroomsLevelLook> ItemLook;
	EBackroomsItem ItemKind = EBackroomsItem::Count;

	// Idle, Reload, Shoot: RevolverClip's order.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Clips;

	TArray<FName> DrumBones;
	FName Handle;
	// Where the revolver got to (not imported, imported wrong, or shown and how
	// big), logged as "Backrooms:" each time it changes.
	FString Status = TEXT("revolver: not held yet");
	// The drum's place when the current reload began, for ReportReload.
	FVector DrumAtStart = FVector::ZeroVector;
	bool bReloading = false;
	bool bReported = false;
	// Which GLB axis (0 x, 1 y, 2 z) each of the mesh's axes holds, its units
	// per metre, and +1 or -1 as that mapping keeps or mirrors a rotation.
	// Read from the mesh's bounds, so no importer convention is assumed.
	int32 Perm[3] = { 0, 1, 2 };
	float Units = 100.0f;
	float Sense = 1.0f;
	bool bLoaded = false;
	bool bMissing = false;
};
