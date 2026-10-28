#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BackroomsTypes.h"
#include "BackroomsSettings.generated.h"

class ABackroomsChunkActor;

// How much of the world shader's shadow tracing runs (BackroomsWorld.ush,
// U.Quality). High is the Web build's lighting; the others trace fewer
// shadows, which is most of the frame, and look a little flatter.
UENUM()
enum class EBackroomsLightQuality : uint8
{
	High,     // the Web build's: soft shadows from every tube within 10-15 m
	Medium,   // hard shadows, fading out by 8 m
	Low,      // no shadows from the tubes
};
class UInputAction;
class UInputMappingContext;

// Project Settings > Game > Backrooms. Saved to Config/DefaultGame.ini.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Backrooms"))
class BACKROOMS_API UBackroomsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// A run started without ?seed= or ?level= on the map's options.
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	int32 DefaultSeed = 1337;
	UPROPERTY(Config, EditAnywhere, Category = "Run", meta = (ClampMin = 0, ClampMax = 4))
	int32 DefaultLevel = 0;

	// Chunks drawn round the player: rings on the player's storey. The storeys
	// above and below stream only round the openings that show them
	// (shared/port/stream.h), out to the same reach.
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 4))
	int32 ReachOwnStorey = 2;
	// Chunk actors built per frame, so a level start does not stall a frame.
	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = 1))
	int32 BuildsPerFrame = 2;

	// What the subsystem spawns per chunk; a Blueprint subclass can add to it.
	UPROPERTY(Config, EditAnywhere, Category = "Streaming")
	TSoftClassPtr<ABackroomsChunkActor> ChunkClass;

	// The sim turns 0.0030 rad per raylib mouse pixel, and this multiplies
	// Unreal's mouse delta before it gets there. At 1 the first run on a Mac
	// turned too slowly. Flip the vertical if up and down come out reversed.
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = 0.1, ClampMax = 10))
	float LookScale = 2.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	// Your own input assets. An action set here is read for its control instead
	// of the built-in one, with the keys your mapping context gives it; controls
	// left empty keep the Web build's keys.
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> InputContext;
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TMap<EBackroomsControl, TSoftObjectPtr<UInputAction>> InputActions;

	// The revolver in hand: the repository's GLB, read and posed at run time
	// as the Web build poses it (shared/port/revolver.h), relative to the
	// repository's root.
	UPROPERTY(Config, EditAnywhere, Category = "Held")
	FString RevolverModel = TEXT("assets/models/revolver.glb");

	// Where the editor imports assets/sounds (backrooms_import.py), keeping its
	// folders: sounds/water/swim_1.ogg is <SoundFolder>/water/swim_1.
	UPROPERTY(Config, EditAnywhere, Category = "Sound")
	FString SoundFolder = TEXT("/Game/Backrooms/Sounds");
	// How far ahead of the speakers the game's mix is generated, in seconds. A
	// frame longer than this leaves a gap in the sound.
	UPROPERTY(Config, EditAnywhere, Category = "Sound", meta = (ClampMin = 0.03, ClampMax = 0.5))
	float SoundLatency = 0.1f;

	// Performance. LightQuality is read every frame; the frame cap and the
	// resolution are applied when a world starts, and the console's t.MaxFPS
	// and r.ScreenPercentage still override them.
	UPROPERTY(Config, EditAnywhere, Category = "Performance")
	EBackroomsLightQuality LightQuality = EBackroomsLightQuality::High;
	// Frames a second at most; 0 for no cap.
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = 0, ClampMax = 240))
	int32 MaxFPS = 60;
	// The world's resolution as a percentage of the window's; the HUD stays
	// sharp. The world shader costs per pixel, so this is the biggest lever.
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = 25, ClampMax = 100))
	int32 ScreenPercentage = 100;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
