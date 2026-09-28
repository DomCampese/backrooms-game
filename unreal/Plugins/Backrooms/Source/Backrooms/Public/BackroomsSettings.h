#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BackroomsTypes.h"
#include "BackroomsSettings.generated.h"

class ABackroomsChunkActor;
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

	// The sim turns 0.0030 rad per raylib mouse pixel; this scales Unreal's
	// mouse delta to match. Flip the vertical if up and down come out reversed.
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	float LookScale = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	// Your own input assets. An action set here is read for its control instead
	// of the built-in one, with the keys your mapping context gives it; controls
	// left empty keep the raylib build's keys.
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> InputContext;
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TMap<EBackroomsControl, TSoftObjectPtr<UInputAction>> InputActions;

	// The look for a level, loaded, or null.
	UBackroomsLevelLook* LookFor(int32 Level) const;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
