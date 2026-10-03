#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BackroomsTypes.h"
#include "BackroomsSettings.generated.h"

class ABackroomsChunkActor;
class UAnimSequence;
class USkeletalMesh;
class UBackroomsLevelLook;
class UInputAction;
class UInputMappingContext;

// Project Settings > Game > Backrooms. Saved to Config/DefaultGame.ini.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Backrooms"))
class BACKROOMS_API UBackroomsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// The look of each level, by index (0 Level 0 .. 4 LEVEL FUN). An empty slot
	// is drawn as the greybox.
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	TArray<TSoftObjectPtr<UBackroomsLevelLook>> LevelLooks;

	// A run started without ?seed= or ?level= on the map's options.
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	int32 DefaultSeed = 1337;
	UPROPERTY(Config, EditAnywhere, Category = "Run", meta = (ClampMin = 0, ClampMax = 4))
	int32 DefaultLevel = 0;

	// Chunks drawn round the player: rings on the player's storey, and on the
	// storeys above and below.
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 4))
	int32 ReachOwnStorey = 2;
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 0, ClampMax = 3))
	int32 ReachOtherStoreys = 1;
	// Chunk actors built per frame, so a level start does not stall a frame.
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 1))
	int32 BuildsPerFrame = 2;

	// What the subsystem spawns per chunk; a Blueprint subclass can add to it.
	UPROPERTY(Config, EditAnywhere, Category = "Streaming")
	TSoftClassPtr<ABackroomsChunkActor> ChunkClass;

	// A light that follows the camera, until the level's lighting is built (M5).
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	bool bCameraLight = true;

	// The sim turns 0.0030 rad per raylib mouse pixel, and this multiplies
	// Unreal's mouse delta before it gets there. At 1 the first run on a Mac
	// turned too slowly. Flip the vertical if up and down come out reversed.
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = 0.1, ClampMax = 10))
	float LookScale = 2.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	// Your own input assets. An action set here is read for its control instead
	// of the built-in one, with the keys your mapping context gives it; controls
	// left empty keep the raylib build's keys.
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> InputContext;
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TMap<EBackroomsControl, TSoftObjectPtr<UInputAction>> InputActions;

	// The revolver in hand. The editor imports assets/models/revolver.glb to
	// these paths the first time it opens the project
	// (Plugins/Backrooms/Content/Python/backrooms_import.py).
	UPROPERTY(Config, EditAnywhere, Category = "Held")
	TSoftObjectPtr<USkeletalMesh> RevolverMesh{ FSoftObjectPath(TEXT("/Game/Backrooms/Revolver/SK_Revolver.SK_Revolver")) };
	UPROPERTY(Config, EditAnywhere, Category = "Held")
	TSoftObjectPtr<UAnimSequence> RevolverIdle{ FSoftObjectPath(TEXT("/Game/Backrooms/Revolver/A_Revolver_Idle.A_Revolver_Idle")) };
	UPROPERTY(Config, EditAnywhere, Category = "Held")
	TSoftObjectPtr<UAnimSequence> RevolverReload{ FSoftObjectPath(TEXT("/Game/Backrooms/Revolver/A_Revolver_Reload.A_Revolver_Reload")) };
	UPROPERTY(Config, EditAnywhere, Category = "Held")
	TSoftObjectPtr<UAnimSequence> RevolverShoot{ FSoftObjectPath(TEXT("/Game/Backrooms/Revolver/A_Revolver_Shoot.A_Revolver_Shoot")) };
	// Turns the imported mesh before it is placed. The placement reads the
	// importer's axes from the mesh's bounds (ABackroomsHeldActor); if the gun
	// still comes out facing the wrong way, this corrects it without a code
	// change.
	UPROPERTY(Config, EditAnywhere, Category = "Held")
	FRotator RevolverMeshRotation = FRotator::ZeroRotator;

	// Where the editor imports assets/sounds (backrooms_import.py), keeping its
	// folders: sounds/water/swim_1.ogg is <SoundFolder>/water/swim_1.
	UPROPERTY(Config, EditAnywhere, Category = "Sound")
	FString SoundFolder = TEXT("/Game/Backrooms/Sounds");
	// How far ahead of the speakers the game's mix is generated, in seconds. A
	// frame longer than this leaves a gap in the sound.
	UPROPERTY(Config, EditAnywhere, Category = "Sound", meta = (ClampMin = 0.03, ClampMax = 0.5))
	float SoundLatency = 0.1f;

	// The look for a level, loaded, or null.
	UBackroomsLevelLook* LookFor(int32 Level) const;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
