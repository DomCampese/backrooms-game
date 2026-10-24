#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include <vector>
#include "BackroomsHUD.generated.h"

struct HudDraw;
class UFont;

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
	// Draws a list from shared/port/hud.h; text is the font scaled so a line
	// is a draw's h pixels high (LineH: the font's own line height).
	void DrawList(const std::vector<HudDraw>& Draws, UFont* Font, float LineH);

	// The torch's reminder shows until it is first used (Game::everFlashed).
	bool bEverFlashed = false;
};
