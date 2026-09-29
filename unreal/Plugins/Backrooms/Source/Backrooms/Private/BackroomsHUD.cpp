#include "BackroomsHUD.h"
#include "BackroomsTypes.h"
#include "BackroomsWorldSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"

namespace
{
const TCHAR* WeaponNames[3] = { TEXT("REVOLVER"), TEXT("FLARE"), TEXT("TAPE DECK") };
}

void ABackroomsHUD::Line(const FString& Text, float X, float Y, const FLinearColor& Colour, bool bCentred, float Scale)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	float W = 0.0f, H = 0.0f;
	GetTextSize(Text, W, H, Font, Scale);
	const float Left = bCentred ? X - W * 0.5f : X;
	// A dark offset copy first, so the text reads over bright tile.
	DrawText(Text, FLinearColor(0, 0, 0, 0.8f), Left + 2.0f, Y + 2.0f, Font, Scale);
	DrawText(Text, Colour, Left, Y, Font, Scale);
}

void ABackroomsHUD::DrawHUD()
{
	Super::DrawHUD();
	const UBackroomsWorldSubsystem* Backrooms = GetWorld()->GetSubsystem<UBackroomsWorldSubsystem>();
	if (!Canvas || !Backrooms)
	{
		return;
	}
	const FBackroomsHud Hud = Backrooms->GetHud();
	if (!Hud.bRunning)
	{
		return;
	}
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	// Scaled from the raylib build's 850-pixel-high window.
	const float K = H / 850.0f;
	const FLinearColor Pale(0.92f, 0.9f, 0.8f), Dim(0.7f, 0.68f, 0.6f), Warn(0.95f, 0.45f, 0.35f);

	if (Hud.bDeathCard)
	{
		Line(Hud.DeathTitle.ToString(), W * 0.5f, H * 0.4f, Warn, true, 2.4f * K);
		Line(Hud.DeathBy.ToString(), W * 0.5f, H * 0.4f + 60.0f * K, Pale, true, 1.2f * K);
		Line(TEXT("ENTER or SPACE to begin again"), W * 0.5f, H * 0.4f + 110.0f * K, Dim, true, K);
		return;
	}
	if (Hud.bTitleScreen)
	{
		Line(TEXT("THE BACKROOMS"), W * 0.5f, H * 0.4f, Pale, true, 2.8f * K);
		Line(TEXT("ENTER or SPACE to begin"), W * 0.5f, H * 0.4f + 70.0f * K, Dim, true, K);
		return;
	}

	Line(FString::Printf(TEXT("%s  ·  storey %d"), *Hud.LevelName.ToString(), Hud.Storey), 24.0f * K, 20.0f * K, Dim, false, K);
	float Y = H - 150.0f * K;
	const float X = 24.0f * K, Step = 24.0f * K;
	Line(FString::Printf(TEXT("HEALTH %d%%   SANITY %d%%   BATTERY %d%%%s"), FMath::RoundToInt(Hud.Health * 100.0f),
		FMath::RoundToInt(Hud.Sanity * 100.0f), FMath::RoundToInt(Hud.Battery * 100.0f),
		Hud.bFlashlightOn ? TEXT("  (torch on)") : TEXT("")), X, Y, Hud.Health < 0.5f ? Warn : Pale, false, K);
	Y += Step;
	Line(FString::Printf(TEXT("%s   AMMO %d   FLARES %d"), WeaponNames[FMath::Clamp(Hud.Weapon, 0, 2)], Hud.Ammo, Hud.Flares),
		X, Y, Pale, false, K);
	Y += Step;
	Line(FString::Printf(TEXT("DOUBLOONS %d   ALMOND WATER %d   TAPES %d   KEYS %d"), Hud.Coins, Hud.AlmondWater, Hud.Tapes,
		Hud.Keys), X, Y, Dim, false, K);

	if (!Hud.Note.IsEmpty())
	{
		Line(Hud.Note.ToString(), W * 0.5f, H * 0.72f, Pale, true, 1.1f * K);
	}
	if (!Hud.SanityWarning.IsEmpty())
	{
		Line(Hud.SanityWarning.ToString(), W * 0.5f, H * 0.2f, Warn, true, 1.2f * K);
	}
	// Until the gun is seen working on the Mac: a screenshot of this line says
	// where it went wrong.
	Line(Backrooms->RevolverStatus(), W * 0.5f, 60.0f * K, Warn, true, 0.8f * K);
	if (Hud.bPaused)
	{
		Line(TEXT("PAUSED  ·  P to resume"), W * 0.5f, H * 0.45f, Pale, true, 1.8f * K);
	}
}
