#include "BackroomsLightsActor.h"
#include "BackroomsCoords.h"
#include "BackroomsRender.h"
#include "BackroomsSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "core/world.h"
#include "port/draw.h"
#include "port/levels.h"

namespace
{
// The panel's glowing square, core's PANEL_HALF either side, in cm.
constexpr float PanelCm = 2.0f * PANEL_HALF * 100.0f;
// The rect light sits this far under the panel's plane, so the fitting's own
// mesh does not shadow it.
constexpr float PanelClearCm = 2.0f;

// The torch, flare and lamp, in lumens at full output, with their reach in
// metres. The colours are the Web build's shader's (BackroomsWorld.ush).
constexpr float TorchLumens = 900.0f;
constexpr float TorchReach = 25.0f;
// The Web shader's cone is pow(cos, 26): half its light inside 13 degrees.
constexpr float TorchInnerDeg = 9.0f;
constexpr float TorchOuterDeg = 22.0f;
// Held at the hand, below the eye, so its shadows show.
constexpr float TorchDropM = 0.15f;
const FLinearColor TorchColour(1.0f, 0.97f, 0.86f);
constexpr float FlareLumens = 1400.0f;
constexpr float FlareReach = 12.0f;
const FLinearColor FlareColour(1.0f, 0.42f, 0.15f);
constexpr float LampLumens = 2400.0f;
// The Web shader's lamp fades out by 90 m^2, about 9.5 m.
constexpr float LampReach = 9.5f;

// Level 94's sun at full strength, in lux. A surface it lights square-on
// gains SunShare times the brightness the level's ambient glow gives it
// (Amb luma 0.196 at WebUnitNits): E = pi * SunShare * 0.196 * WebUnitNits.
constexpr float SunShare = 2.0f;
constexpr float SunLux = 3.14159265f * SunShare * 0.196f * UBackroomsRender::WebUnitNits;
// How far its shadows reach, in cm: a house or a hill 80 m off still shades.
constexpr float SunShadowCm = 8000.0f;

// Unreal's fog density is per 1000 world units (cm), the Web build's per
// metre: exp(-den * metres) is exp(-(den * 10) / 1000 * cm).
constexpr float FogPerMetre = 10.0f;
// As near to no height falloff as Unreal allows: the Web build's fog does not
// thin with height.
constexpr float FogFlat = 0.001f;
// The Web shader's fog colour in a blackout, as a share of the lit colour.
constexpr float FogBlackoutFloor = 0.10f;

// The hunter's pool of dead light on a fitting at XZ (BRPanelSum).
float EntDim(const FrameLight& L, float X, float Z)
{
	if (L.entDark <= 0.01f)
	{
		return 1.0f;
	}
	const float D = FMath::Sqrt(FMath::Square(X - L.entPos.x) + FMath::Square(Z - L.entPos.z));
	return FMath::Lerp(1.0f, FMath::SmoothStep(2.0f, 9.0f, D), L.entDark);
}

FLinearColor ColourOf(const Vec3& C)
{
	const float Peak = FMath::Max3(C.x, C.y, C.z);
	return Peak > 0.0f ? FLinearColor(C.x / Peak, C.y / Peak, C.z / Peak) : FLinearColor::White;
}

template <class T>
T* MakeLight(AActor* Owner, float ReachM)
{
	T* Light = NewObject<T>(Owner);
	Light->SetupAttachment(Owner->GetRootComponent());
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Lumens);
	Light->SetAttenuationRadius(ReachM * BackroomsCoords::CmPerM);
	Light->SetVisibility(false);
	Light->RegisterComponent();
	return Light;
}

int32 ShadowedFittings(const UBackroomsSettings& Settings)
{
	switch (Settings.LightQuality)
	{
	case EBackroomsLightQuality::High: return Settings.FittingShadowsHigh;
	case EBackroomsLightQuality::Medium: return Settings.FittingShadowsMedium;
	default: return 0;
	}
}
}

ABackroomsLightsActor::ABackroomsLightsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetFogHeightFalloff(FogFlat);
	Fog->SetStartDistance(0.0f);
	Fog->SetFogMaxOpacity(1.0f);
}

const TArray<LightFitting>& ABackroomsLightsActor::FittingsOf(World& W, int32 Cx, int32 Cz)
{
	const FIntVector Key(Cx, Cz, W.qs);
	if (const TArray<LightFitting>* Known = Fittings.Find(Key))
	{
		return *Known;
	}
	TArray<LightFitting>& Live = Fittings.Add(Key);
	for (const LightFitting& Fitting : chunkLayout(W, Cx, Cz).fittings)
	{
		if (Fitting.gap == FittingGap::None && !Fitting.dead)
		{
			Live.Add(Fitting);
		}
	}
	return Live;
}

void ABackroomsLightsActor::Show(World& W, int32 Level, const Vec3& Eye, const FrameLight& L, const FVector& Origin)
{
	ShowFittings(W, Level, Eye, L, Origin);
	ShowFog(Level, L, Origin);
	ShowSun(L);

	if (!Torch)
	{
		Torch = MakeLight<USpotLightComponent>(this, TorchReach);
		Torch->SetInnerConeAngle(TorchInnerDeg);
		Torch->SetOuterConeAngle(TorchOuterDeg);
		Torch->SetLightColor(TorchColour);
		Flare = MakeLight<UPointLightComponent>(this, FlareReach);
		Flare->SetLightColor(FlareColour);
		Lamp = MakeLight<UPointLightComponent>(this, LampReach);
	}
	Torch->SetVisibility(L.flash > 0.01f);
	if (L.flash > 0.01f)
	{
		const Vec3 Hand = { Eye.x, Eye.y - TorchDropM, Eye.z };
		Torch->SetWorldLocationAndRotation(BackroomsCoords::ToUnreal(Hand) + Origin,
			BackroomsCoords::ToUnrealDirection(L.flashDir).Rotation());
		Torch->SetIntensity(TorchLumens * L.flash);
	}
	Flare->SetVisibility(L.flareInt > 0.01f);
	if (L.flareInt > 0.01f)
	{
		Flare->SetWorldLocation(BackroomsCoords::ToUnreal(L.flarePos) + Origin);
		Flare->SetIntensity(FlareLumens * L.flareInt);
	}
	Lamp->SetVisibility(L.lamp[3] > 0.01f);
	if (L.lamp[3] > 0.01f)
	{
		Lamp->SetWorldLocation(BackroomsCoords::ToUnreal({ L.lamp[0], L.lamp[1], L.lamp[2] }) + Origin);
		Lamp->SetLightColor(ColourOf(L.lampCol));
		Lamp->SetIntensity(LampLumens * L.lamp[3]);
	}
}

void ABackroomsLightsActor::ShowFittings(World& W, int32 Level, const Vec3& Eye, const FrameLight& L, const FVector& Origin)
{
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	const LevelCfg& C = LEVELS[Level];

	// The fittings of the 3x3 chunks round the eye, nearest first. Copies:
	// reading a chunk can grow Fittings and move what it holds.
	struct FCandidate
	{
		LightFitting Fitting;
		float DistSq;
	};
	TArray<FCandidate> Near;
	const int32 Pcx = fdiv(cellOf(Eye.x), CCELLS), Pcz = fdiv(cellOf(Eye.z), CCELLS);
	for (int32 Dz = -1; Dz <= 1; Dz++)
	{
		for (int32 Dx = -1; Dx <= 1; Dx++)
		{
			for (const LightFitting& Fitting : FittingsOf(W, Pcx + Dx, Pcz + Dz))
			{
				const float X = Fitting.pos.x - Eye.x, Y = Fitting.pos.y - Eye.y, Z = Fitting.pos.z - Eye.z;
				Near.Add({ Fitting, X * X + Y * Y + Z * Z });
			}
		}
	}
	Near.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistSq < B.DistSq; });

	const int32 Wanted = Settings->FittingLights;
	while (Pool.Num() < Wanted)
	{
		URectLightComponent* Light = MakeLight<URectLightComponent>(this, Settings->FittingReach);
		Light->SetSourceWidth(PanelCm);
		Light->SetSourceHeight(PanelCm);
		// A rect light shines along its +X: straight down.
		Light->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		Pool.Add(Light);
	}

	const FLinearColor Colour = ColourOf(C.lightCol);
	const float Output = Settings->FittingLumens * C.lightMul * L.blackout;
	const int32 Shadowed = ShadowedFittings(*Settings);
	for (int32 I = 0; I < Pool.Num(); I++)
	{
		URectLightComponent* Light = Pool[I];
		if (I >= Wanted || I >= Near.Num() || Output <= 0.0f)
		{
			Light->SetVisibility(false);
			continue;
		}
		const LightFitting& Fitting = Near[I].Fitting;
		const float Stutter = Fitting.faulty ? tubeStutter(Fitting.stutterSeed, L.time) : 1.0f;
		const FVector At = BackroomsCoords::ToUnreal(Fitting.pos) + Origin - FVector(0.0, 0.0, PanelClearCm);
		Light->SetVisibility(true);
		Light->SetWorldLocation(At);
		Light->SetLightColor(Colour);
		Light->SetIntensity(Output * Fitting.output * Stutter * EntDim(L, Fitting.pos.x, Fitting.pos.z));
		Light->SetCastShadows(I < Shadowed);
	}
}

void ABackroomsLightsActor::ShowFog(int32 Level, const FrameLight& L, const FVector& Origin)
{
	const LevelCfg& C = LEVELS[Level];
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	Fog->SetWorldLocation(Origin);
	Fog->SetFogDensity(C.fogDen * FogPerMetre);
	const float Lit = FMath::Lerp(FogBlackoutFloor, 1.0f, L.blackout);
	Fog->SetFogInscatteringColor(FLinearColor(C.fogCol.x, C.fogCol.y, C.fogCol.z) * (Lit * UBackroomsRender::WebUnitNits));
	Fog->SetVolumetricFog(Settings->LightQuality != EBackroomsLightQuality::Low);
}

void ABackroomsLightsActor::ShowSun(const FrameLight& L)
{
	if (!Sun)
	{
		Sun = NewObject<UDirectionalLightComponent>(this);
		Sun->SetupAttachment(GetRootComponent());
		Sun->SetMobility(EComponentMobility::Movable);
		Sun->SetCastShadows(true);
		Sun->SetDynamicShadowDistanceMovableLight(SunShadowCm);
		Sun->SetVisibility(false);
		Sun->RegisterComponent();
	}
	// L.sun is 0 off Level 94 and at night; L.blackout is the level's
	// daylight there, so the sun dims through dusk with the ambient.
	const float Strength = L.sun * L.blackout;
	Sun->SetVisibility(Strength > 0.01f);
	if (Strength <= 0.01f)
	{
		return;
	}
	// A directional light shines along its +X, away from the sun.
	Sun->SetWorldRotation((-BackroomsCoords::ToUnrealDirection(L.sunDir)).Rotation());
	Sun->SetLightColor(FLinearColor(L.sunCol.x, L.sunCol.y, L.sunCol.z));
	Sun->SetIntensity(SunLux * Strength);
}
