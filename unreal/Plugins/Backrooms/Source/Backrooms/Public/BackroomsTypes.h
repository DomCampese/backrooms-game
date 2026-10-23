#pragma once
// The sim's controls as an editor enum, and the state a HUD reads.
#include "CoreMinimal.h"
#include "BackroomsTypes.generated.h"

// The sim's controls (InputFrame, shared/sim/input_frame.h), one Enhanced Input
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
// (UBackroomsWorldSubsystem::GetHud). Meters run 0..1. Not FBackroomsHud:
// Unreal names types without their prefix, ignoring case, and that name is
// ABackroomsHUD's.
USTRUCT(BlueprintType)
struct FBackroomsHudState
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
