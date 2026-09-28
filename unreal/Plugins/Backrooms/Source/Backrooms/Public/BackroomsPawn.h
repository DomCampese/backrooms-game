#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "BackroomsPawn.generated.h"

class UCameraComponent;

// The player's eye. It has no movement or collision of its own: in a run the
// sim moves the player and the controller puts this camera where the sim's
// view is; with the free camera the controller flies it. A Blueprint subclass
// (set as the game mode's Default Pawn Class) can carry viewmodels and effects.
UCLASS(Blueprintable)
class BACKROOMS_API ABackroomsPawn : public APawn
{
	GENERATED_BODY()

public:
	ABackroomsPawn();

	// FovY is vertical, degrees, as the sim keeps it; Unreal's camera takes the
	// horizontal angle, so this converts at the viewport's aspect.
	void SetView(const FTransform& View, float FovY, float Aspect);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UCameraComponent> Camera;
};
