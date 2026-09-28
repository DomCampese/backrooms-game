#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BackroomsGameMode.generated.h"

// Starts a level from the map's options and puts a free camera in it:
// ?level=N&seed=S&visit=V, defaults 0, 1337 and the visit a raylib capture of
// that level shows (1 on Level 0, 0 elsewhere).
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
