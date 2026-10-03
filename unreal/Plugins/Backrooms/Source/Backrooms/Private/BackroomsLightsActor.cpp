#include "BackroomsLightsActor.h"
#include "BackroomsCoords.h"
#include "BackroomsLevelLook.h"
#include "BackroomsSettings.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/RectLightComponent.h"

namespace
{
// The panel's glowing square, the shader's PANEL_HALF either side.
constexpr float PanelCm = 124.0f;
// Unreal's fog density is per 1000 world units (cm), raylib's per metre:
// exp(-den * metres) is exp(-(den * 10) / 1000 * cm).
constexpr float FogPerMetre = 10.0f;
// The fog's height falloff, as near to none as Unreal allows: the raylib fog
// does not thin with height.
constexpr float FogFlat = 0.001f;
}

ABackroomsLightsActor::ABackroomsLightsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetVisibility(false);
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

void ABackroomsLightsActor::Show(World& W, const Vec3& At, const UBackroomsLevelLook* Look, float Blackout,
	float Now, const FVector& Origin)
{
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();

	const bool bFog = Look && Look->bFog;
	Fog->SetVisibility(bFog);
	if (bFog)
	{
		Fog->SetWorldLocation(Origin);
		Fog->SetFogDensity(Look->FogDensity * FogPerMetre);
		Fog->SetFogInscatteringColor(Look->FogColour);
	}

	// The fittings of the 3x3 chunks round At, nearest first.
	// Copies: reading a chunk can grow Fittings and move what it holds.
	struct FCandidate
	{
		LightFitting Fitting;
		float DistSq;
	};
	TArray<FCandidate> Near;
	const int32 Pcx = fdiv(cellOf(At.x), CCELLS), Pcz = fdiv(cellOf(At.z), CCELLS);
	for (int32 Dz = -1; Dz <= 1; Dz++)
	{
		for (int32 Dx = -1; Dx <= 1; Dx++)
		{
			for (const LightFitting& Fitting : FittingsOf(W, Pcx + Dx, Pcz + Dz))
			{
				const float X = Fitting.pos.x - At.x, Y = Fitting.pos.y - At.y, Z = Fitting.pos.z - At.z;
				Near.Add({ Fitting, X * X + Y * Y + Z * Z });
			}
		}
	}
	Near.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistSq < B.DistSq; });

	const int32 Wanted = Settings->FittingLights;
	while (Pool.Num() < Wanted)
	{
		URectLightComponent* Light = NewObject<URectLightComponent>(this);
		Light->SetupAttachment(RootComponent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetSourceWidth(PanelCm);
		Light->SetSourceHeight(PanelCm);
		Light->SetAttenuationRadius(Settings->FittingReach * BackroomsCoords::CmPerM);
		// A rect light shines along its +X: straight down.
		Light->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		Light->RegisterComponent();
		Pool.Add(Light);
	}

	const FLinearColor Colour = Look ? Look->LightColour : FLinearColor::White;
	const float Output = (Look ? Look->LightOutput : 1.0f) * Settings->FittingLumens * Blackout;
	for (int32 I = 0; I < Pool.Num(); I++)
	{
		URectLightComponent* Light = Pool[I];
		if (I >= Wanted || I >= Near.Num())
		{
			Light->SetVisibility(false);
			continue;
		}
		const LightFitting& Fitting = Near[I].Fitting;
		const float Stutter = Fitting.faulty ? tubeStutter(Fitting.stutterSeed, Now) : 1.0f;
		Light->SetVisibility(true);
		Light->SetWorldLocation(BackroomsCoords::ToUnreal(Fitting.pos) + Origin);
		Light->SetLightColor(Colour);
		Light->SetIntensity(Output * Fitting.output * Stutter);
		Light->SetCastShadows(I < Settings->FittingShadows);
	}
}
