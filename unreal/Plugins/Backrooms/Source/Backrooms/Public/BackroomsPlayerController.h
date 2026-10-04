#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BackroomsTypes.h"
#include "BackroomsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

// The sim's controls. Each is an Enhanced Input action: the one assigned in
// Project Settings > Game > Backrooms, with the keys your mapping context gives
// it, or else one made here with the Web build's keys, so the project runs
// with no input assets at all. Each frame the controller reads every action
// once, fills the sim's InputFrame as Game::readInput does (held keys, down
// edges, the mouse delta), steps the run and points the camera at the sim's
// view.
UCLASS(Blueprintable)
class BACKROOMS_API ABackroomsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// Where the free camera starts.
	void SetFreeCameraStart(const FTransform& Start);

private:
	void MakeControls();
	void AddMappingContexts();
	void ReadControls();
	bool Down(EBackroomsControl C) const { return bHeld[(int32)C]; }
	bool Pressed(EBackroomsControl C) const { return bHeld[(int32)C] && !bWasHeld[(int32)C]; }
	void FlyFreeCamera(float DeltaTime);

	// The built-in mapping context, holding only the controls the settings leave
	// empty, and the project's own context if it names one.
	UPROPERTY()
	TObjectPtr<UInputMappingContext> Controls;
	UPROPERTY()
	TObjectPtr<UInputMappingContext> ProjectControls;
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;
	bool bContextAdded = false;
	bool bHeld[(int32)EBackroomsControl::Count] = {};
	bool bWasHeld[(int32)EBackroomsControl::Count] = {};
	FVector2D LookDelta = FVector2D::ZeroVector;
	float Wheel = 0.0f;
	bool bDebugHud = false;
	float FreeYaw = 0.0f, FreePitch = 0.0f;   // radians, the sim's convention
};
