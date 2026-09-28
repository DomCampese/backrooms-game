#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BackroomsGameMode.generated.h"

// Starts the game from the map's options:
//   ?seed=S&level=N            a run (the default): the sim plays from Level 0,
//                              or from Level N as BACKROOMS_LEVEL does
//   ?mode=free&level=N&visit=V a free camera over the greybox of one level
// The seed and level default to Project Settings > Game > Backrooms; the free
// camera's visit to the one a raylib capture of that level shows (1 on Level 0,
// 0 elsewhere). A Blueprint subclass can swap the pawn and controller classes.
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
