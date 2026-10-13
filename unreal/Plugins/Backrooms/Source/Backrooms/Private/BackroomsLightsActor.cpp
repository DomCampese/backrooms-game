#include "BackroomsLightsActor.h"
#include "BackroomsChunkActor.h"
#include "BackroomsLevelLook.h"
#include "BackroomsSettings.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "port/view.h"

namespace
{
// A light keeps its shadow until this many times FittingShadows lights are
// nearer, so walking between two fittings does not swap one back and forth.
constexpr int32 ShadowKeep = 2;
// Unreal's fog density is per 1000 world units (cm), raylib's per metre:
// exp(-den * metres) is exp(-(den * 10) / 1000 * cm).
constexpr float FogPerMetre = 10.0f;
// The fog's height falloff, as near to none as Unreal allows: the raylib fog
// does not thin with height.
constexpr float FogFlat = 0.001f;
// Unreal's fringe is in percent of the screen, the post pass's split a fraction.
constexpr float FringePerSplit = 100.0f;
// The exposure without a look, EV100.
constexpr float DefaultEV100 = 6.0f;
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

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(RootComponent);
	Post->bUnbound = true;
	FPostProcessSettings& Pinned = Post->Settings;
	// Eye adaptation pinned: the minimum and maximum are one EV100 (the
	// project extends the luminance range, so these are EV100).
	Pinned.bOverride_AutoExposureMinBrightness = true;
	Pinned.bOverride_AutoExposureMaxBrightness = true;
	Pinned.bOverride_SceneFringeIntensity = true;
}

void ABackroomsLightsActor::Show(const TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Chunks, int32 Storey,
	const FVector& At, const UBackroomsLevelLook* Look, const FMoment& Moment, const FVector& Origin)
{
	FPostProcessSettings& Camera = Post->Settings;
	Camera.AutoExposureMinBrightness = Camera.AutoExposureMaxBrightness = Look ? Look->ExposureEV100 : DefaultEV100;
	Camera.SceneFringeIntensity =
		colourSplit(Moment.Fear, migraineThrob(Moment.Migraine, Moment.Now)) * FringePerSplit;

	const bool bFog = Look && Look->bFog;
	Fog->SetVisibility(bFog);
	if (bFog)
	{
		Fog->SetWorldLocation(Origin);
		Fog->SetFogDensity(Look->FogDensity * FogPerMetre);
		Fog->SetFogInscatteringColor(Look->FogColour);
	}

	for (const TPair<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Entry : Chunks)
	{
		if (Entry.Value)
		{
			Entry.Value->ShowLights(Moment.Blackout, Moment.Now);
		}
	}
	PickShadows(Chunks, Storey, At);
}

void ABackroomsLightsActor::PickShadows(const TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Chunks,
	int32 Storey, const FVector& At)
{
	const int32 Wanted = GetDefault<UBackroomsSettings>()->FittingShadows;
	// The lights of the player's storey, nearest first.
	struct FCandidate
	{
		UPointLightComponent* Light;
		double DistSq;
	};
	TArray<FCandidate> Near;
	if (Wanted > 0)
	{
		for (const TPair<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Entry : Chunks)
		{
			if (Entry.Key.Z != Storey || !Entry.Value)
			{
				continue;
			}
			for (UPointLightComponent* Light : Entry.Value->GetLights())
			{
				Near.Add({ Light, FVector::DistSquared(Light->GetComponentLocation(), At) });
			}
		}
		Near.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistSq < B.DistSq; });
	}

	// Those already casting keep it while they stay near enough, then the
	// nearest fill what is left.
	TArray<UPointLightComponent*> Picked;
	const int32 Keep = FMath::Min(Near.Num(), Wanted * ShadowKeep);
	for (int32 I = 0; I < Keep && Picked.Num() < Wanted; I++)
	{
		if (Shadowed.Contains(TWeakObjectPtr<UPointLightComponent>(Near[I].Light)))
		{
			Picked.Add(Near[I].Light);
		}
	}
	for (int32 I = 0; I < Near.Num() && Picked.Num() < Wanted; I++)
	{
		Picked.AddUnique(Near[I].Light);
	}

	for (const TWeakObjectPtr<UPointLightComponent>& Was : Shadowed)
	{
		if (Was.IsValid() && !Picked.Contains(Was.Get()))
		{
			Was->SetCastShadows(false);
		}
	}
	Shadowed.Reset();
	for (UPointLightComponent* Light : Picked)
	{
		if (!Light->CastShadows)
		{
			Light->SetCastShadows(true);
		}
		Shadowed.Add(Light);
	}
}
