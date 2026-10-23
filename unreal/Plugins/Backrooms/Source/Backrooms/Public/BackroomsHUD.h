#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BackroomsHUD.generated.h"

// The Web build's HUD: the title and death cards, the intro, the cards that
// come and go, the meters, the inventory and the hints, laid out by
// shared/port/hud.h as render.cpp lays them out, and measured and drawn with
// the engine's medium font. Replace it with a Blueprint HUD class on a
// Blueprint subclass of the game mode; GetHud gives a UMG HUD the same state.
UCLASS(Blueprintable)
class BACKROOMS_API ABackroomsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	// The torch's reminder shows until it is first used (Game::everFlashed).
	bool bEverFlashed = false;
};
