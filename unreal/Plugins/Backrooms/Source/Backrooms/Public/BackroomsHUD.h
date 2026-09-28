#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BackroomsHUD.generated.h"

// A plain text HUD from UBackroomsWorldSubsystem::GetHud, so a run is playable
// before a UMG one exists: the meters and inventory bottom left, notes and
// warnings centred, the pause and death cards. Replace it with a Blueprint HUD
// class on a Blueprint subclass of the game mode.
UCLASS(Blueprintable)
class BACKROOMS_API ABackroomsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void Line(const FString& Text, float X, float Y, const FLinearColor& Colour, bool bCentred, float Scale = 1.0f);
};
