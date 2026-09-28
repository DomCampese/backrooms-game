#pragma once
// Editor-facing mirrors of core's enums, and the state a HUD reads. Each enum
// lists core's values in core's order; BackroomsTypes.cpp asserts they agree.
#include "CoreMinimal.h"
#include "BackroomsTypes.generated.h"

// GreySurface (src/port/greybox.h): what a greybox face is.
UENUM(BlueprintType)
enum class EBackroomsSurface : uint8
{
	Floor, Ceiling, Wall, Step, Stair, Pillar, Prop, Door, Exit, CursedExit, Glass, Rail, Water, Light, DeadLight,
	Count UMETA(Hidden)
};

// PropKind (src/core/world.h): what stands in a cell.
UENUM(BlueprintType)
enum class EBackroomsProp : uint8
{
	None UMETA(Hidden),
	Boxes, Cabinet, Table, FallenTile, Couch, Armoire, Lamp, Nightstand, Bed, Vending, PartyTable, Desk,
	Shelving, Cooler, Plant, ManilaTable,
	Count UMETA(Hidden)
};

// The sim's controls (InputFrame, src/sim/input_frame.h), one Enhanced Input
// action each.
UENUM(BlueprintType)
enum class EBackroomsControl : uint8
{
	Forward, Back, Left, Right, Sprint, Crouch, Squeeze, Jump,
	PickRevolver, PickFlare, PickDeck, Fire, Aim, Reload, Throw, Flashlight, Use, Drink, Chalk,
	Pause, DebugHud, DevBlackout, DevBanish, DevRefill, DevStoreyUp, DevStoreyDown, DevNextLevel,
	Begin, Look, Wheel,
	Count UMETA(Hidden)
};

// What a HUD shows, read from the sim after each frame
// (UBackroomsWorldSubsystem::GetHud). Meters run 0..1.
USTRUCT(BlueprintType)
struct FBackroomsHud
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") bool bRunning = false;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") bool bTitleScreen = false;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") bool bPaused = false;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Level = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") FText LevelName;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Storey = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") float Health = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") float Sanity = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") float Stamina = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") float Battery = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") bool bFlashlightOn = false;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Weapon = 0;   // 0 revolver, 1 flare, 2 tape deck
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Ammo = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Flares = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Coins = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 AlmondWater = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Tapes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") int32 Keys = 0;
	// One-line notes and their time left, seconds; empty when none shows.
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") FText Note;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") FText SanityWarning;
	// The death card: its headline and what did it, while it holds the title screen.
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") bool bDeathCard = false;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") FText DeathTitle;
	UPROPERTY(BlueprintReadOnly, Category = "Backrooms") FText DeathBy;
};
