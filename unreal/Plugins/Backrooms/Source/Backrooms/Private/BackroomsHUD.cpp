#include "BackroomsHUD.h"
#include "BackroomsWorldSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"
#include "port/camcorder.h"
#include "port/hud.h"
#include "sim/sim.h"

namespace
{
FLinearColor Colour(const Rgba& C)
{
	return FLinearColor(FColor(C.r, C.g, C.b, C.a));
}

// The canvas has no gradient fill, so a gradient is drawn in bands this many
// to its length.
constexpr int32 GradientBands = 48;

// The camcorder's date and time: the wall clock's, not the run's.
std::tm WallClock()
{
	const FDateTime Now = FDateTime::Now();
	std::tm Clock = {};
	Clock.tm_hour = Now.GetHour();
	Clock.tm_min = Now.GetMinute();
	Clock.tm_sec = Now.GetSecond();
	Clock.tm_mon = Now.GetMonth() - 1;
	Clock.tm_mday = Now.GetDay();
	return Clock;
}
}

void ABackroomsHUD::DrawHUD()
{
	Super::DrawHUD();
	const UBackroomsWorldSubsystem* Backrooms = GetWorld() ? GetWorld()->GetSubsystem<UBackroomsWorldSubsystem>() : nullptr;
	const Sim* Game = Backrooms ? Backrooms->GetSim() : nullptr;
	if (!Canvas || !Game || !Backrooms->IsRunning())
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	// A raylib size is the height of a line of text; the font is scaled to it.
	float Unused = 0.0f, LineH = 0.0f;
	GetTextSize(TEXT("Xg"), Unused, LineH, Font, 1.0f);
	auto ScaleFor = [LineH](int32 Px) { return LineH > 0.0f ? Px / LineH : 1.0f; };
	const HudMeasure Measure = [this, Font, &ScaleFor](const char* Text, int Px) {
		float W = 0.0f, H = 0.0f;
		GetTextSize(FString(UTF8_TO_TCHAR(Text)), W, H, Font, ScaleFor(Px));
		return (int)W;
	};

	const int W = (int)Canvas->ClipX, H = (int)Canvas->ClipY;
	const double Now = Backrooms->SimClock();
	const Camcorder& Cam = *Backrooms->GetCamcorder();
	const std::tm Clock = WallClock();
	// Through the camcorder its OSD replaces the inventory and meters, as the
	// tape look will replace the eye's when the port has a reel.
	const bool bFilming = Cam.viewing() && !Game->inMenu;
	if (bFilming)
	{
		DrawList(Cam.viewfinderOsd(W, H, (float)Now, (float)(Now - Game->runStart), &Clock, Measure), Font, LineH);
	}
	if (!Game->inMenu && Cam.raise > 0.0f && Cam.raise < 1.0f)   // the eye meeting the finder
	{
		const float Dark = 1.0f - FMath::Abs(2.0f * Cam.raise - 1.0f);
		DrawRect(FLinearColor(FColor(0, 0, 0, (uint8)(255.0f * FMath::Clamp(Dark, 0.0f, 1.0f)))), 0, 0, W, H);
	}
	if (Game->inMenu && Game->deathT > 0 && Cam.replaying())
	{
		DrawList(Cam.replayOsd(W, H, (float)Now, &Clock, Measure), Font, LineH);
	}

	HudInput In;
	In.width = W;
	In.height = H;
	In.now = Now;
	In.filming = bFilming;
	In.replaying = Cam.replaying();
	In.cardAlpha = Cam.cardAlpha();
	In.touch = false;
	In.captureHint = false;
	const float Dt = GetWorld()->GetDeltaSeconds();
	In.fps = Dt > 0.0f ? (int)(1.0f / Dt + 0.5f) : 0;
	In.debug = false;

	DrawList(hudDraws(*Game, In, bEverFlashed, Measure), Font, LineH);
}

void ABackroomsHUD::DrawList(const std::vector<HudDraw>& Draws, UFont* Font, float LineH)
{
	auto ScaleFor = [LineH](int32 Px) { return LineH > 0.0f ? Px / LineH : 1.0f; };
	for (const HudDraw& D : Draws)
	{
		const FLinearColor C = Colour(D.col), C2 = Colour(D.col2);
		switch (D.kind)
		{
		case HudDraw::Text:
			DrawText(FString(UTF8_TO_TCHAR(D.text.c_str())), C, D.x, D.y, Font, ScaleFor(D.h));
			break;
		case HudDraw::Rect:
			DrawRect(C, D.x, D.y, D.w, D.h);
			break;
		case HudDraw::RectLines:
			DrawRect(C, D.x, D.y, D.w, 1);
			DrawRect(C, D.x, D.y + D.h - 1, D.w, 1);
			DrawRect(C, D.x, D.y + 1, 1, D.h - 2);
			DrawRect(C, D.x + D.w - 1, D.y + 1, 1, D.h - 2);
			break;
		case HudDraw::GradientV:
		case HudDraw::GradientH:
		{
			const bool bDown = D.kind == HudDraw::GradientV;
			const float Length = bDown ? D.h : D.w, Band = Length / GradientBands;
			for (int32 B = 0; B < GradientBands; B++)
			{
				const FLinearColor Mix = FMath::Lerp(C, C2, (B + 0.5f) / GradientBands);
				if (bDown) DrawRect(Mix, D.x, D.y + B * Band, D.w, Band);
				else DrawRect(Mix, D.x + B * Band, D.y, Band, D.h);
			}
			break;
		}
		case HudDraw::Circle:
			// A disc a few pixels across, as rows.
			for (int32 Dy = -(int32)D.r; Dy <= (int32)D.r; Dy++)
			{
				const float Half = FMath::Sqrt(FMath::Max(D.r * D.r - Dy * Dy, 0.0f));
				DrawRect(C, D.x - Half, D.y + Dy, 2.0f * Half, 1);
			}
			break;
		case HudDraw::Triangle:
			// Pointing right, as rows: full width r at the middle row, none at the ends.
			for (int32 Dy = 0; Dy < D.h; Dy++)
			{
				const float Mid = 0.5f * D.h;
				const float Width = D.r * (1.0f - FMath::Abs(Dy + 0.5f - Mid) / FMath::Max(Mid, 1.0f));
				DrawRect(C, D.x, D.y + Dy, Width, 1);
			}
			break;
		}
	}
}
