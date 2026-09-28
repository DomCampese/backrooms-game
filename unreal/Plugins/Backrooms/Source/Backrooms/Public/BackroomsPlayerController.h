#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BackroomsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

// The sim's controls. Each is an Enhanced Input action, made here at start-up
// with the raylib build's keys, so the project needs no input assets and the
// keys can be remapped the Enhanced Input way later. Each frame the controller
// reads every action once, fills the sim's InputFrame as Game::readInput does
// (held keys, down edges, the mouse delta), steps the run and points the
// camera at the sim's view.
UCLASS(Config = Game)
class BACKROOMS_API ABackroomsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// The sim turns 0.0030 rad per raylib mouse pixel. Scale Unreal's mouse
	// delta to match, and flip the vertical if up and down come out reversed.
	UPROPERTY(Config, EditAnywhere, Category = "Backrooms")
	float LookScale = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Backrooms")
	bool bInvertLookY = false;

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// Where the free camera starts.
	void SetFreeCameraStart(const FTransform& Start);

	enum class EControl : uint8
	{
		Forward, Back, Left, Right, Sprint, Crouch, Squeeze, Jump,
		PickRevolver, PickFlare, PickDeck, Fire, Aim, Reload, Throw, Flashlight, Use, Drink, Chalk,
		Pause, DebugHud, DevBlackout, DevBanish, DevRefill, DevStoreyUp, DevStoreyDown, DevNextLevel,
		Begin, Look, Wheel,
		Count
	};

private:
	void MakeControls();
	void AddMappingContext();
	void ReadControls();
	bool Down(EControl C) const { return bHeld[(int32)C]; }
	bool Pressed(EControl C) const { return bHeld[(int32)C] && !bWasHeld[(int32)C]; }
	void FlyFreeCamera(float DeltaTime);

	UPROPERTY()
	TObjectPtr<UInputMappingContext> Controls;
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;
	bool bContextAdded = false;
	bool bHeld[(int32)EControl::Count] = {};
	bool bWasHeld[(int32)EControl::Count] = {};
	FVector2D LookDelta = FVector2D::ZeroVector;
	float Wheel = 0.0f;
	bool bDebugHud = false;
	float FreeYaw = 0.0f, FreePitch = 0.0f;   // radians, the sim's convention
};
