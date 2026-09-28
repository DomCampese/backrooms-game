#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BackroomsGameMode.generated.h"

// Starts the game from the map's options:
//   ?seed=S&level=N            a run (the default): the sim plays from Level 0,
//                              or from Level N as BACKROOMS_LEVEL does
//   ?mode=free&level=N&visit=V a free camera over the greybox of one level
// Defaults: seed 1337, level 0, and for the free camera the visit a raylib
// capture of that level shows (1 on Level 0, 0 elsewhere).
UCLASS()
class BACKROOMS_API ABackroomsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABackroomsGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

private:
	FTransform Spawn;
};
