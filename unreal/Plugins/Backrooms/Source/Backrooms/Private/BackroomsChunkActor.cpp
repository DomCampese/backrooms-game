#include "BackroomsChunkActor.h"
#include "BackroomsChunkBuild.h"
#include "BackroomsCoords.h"
#include "BackroomsLevelLook.h"
#include "BackroomsLighting.h"
#include "BackroomsSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "ProceduralMeshComponent.h"
#include "core/level_rules.h"

namespace
{
// The light's soft radius, so highlights and shadow edges are softer than a
// point's. No larger than the drop from the ceiling, so the sphere stays below it.
constexpr float SourceCm = LIGHT_DROP * 100.0f;
}

ABackroomsChunkActor::ABackroomsChunkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	// Queried by the sim's bullets (FUnrealTracer), against the triangles
	// themselves. Nothing simulates physics against it: core does collision.
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->bUseComplexAsSimpleCollision = true;
	RootComponent = Mesh;
}

void ABackroomsChunkActor::Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look)
{
	FBackroomsLitFittings Lit;
	BackroomsChunkBuild::Build(W, Cx, Cz, Look, *Mesh, true, Instances, Lit);
	Fittings = MoveTemp(Lit.Fittings);
	Panels = Lit.Panels;

	// A point light 12 cm under the ceiling (LIGHT_DROP) lights the ceiling round
	// the panel as well as the room below, as the Web build's shader does. A
	// panel's lumens shine into half a sphere, so its candelas straight down are
	// lumens / pi; a point light given those candelas matches it there.
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	Candelas = (Look ? Look->LightOutput : 1.0f) * Settings->FittingLumens / UE_PI;
	const FLinearColor Colour = Look ? Look->LightColour : FLinearColor::White;
	const float ReachCm = Settings->FittingReachPitches * LEVEL_RULES[W.level].ls * BackroomsCoords::CmPerM;
	for (const LightFitting& Fitting : Fittings)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
		Light->SetupAttachment(Mesh);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRelativeLocation(BackroomsCoords::ToUnreal(Fitting.pos));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetLightColor(Colour);
		Light->SetSourceRadius(SourceCm);
		Light->SetAttenuationRadius(ReachCm);
		Light->SetCastShadows(false);
		Light->MaxDrawDistance = Settings->FittingDrawDistance * BackroomsCoords::CmPerM;
		Light->MaxDistanceFadeRange = Settings->FittingFade * BackroomsCoords::CmPerM;
		Light->SetIntensity(Candelas * Fitting.output);
		Light->RegisterComponent();
		Lights.Add(Light);
		Shown.Add(Fitting.output);
	}
	BackroomsLighting::Apply(*this, W.qs);
}

void ABackroomsChunkActor::ShowLights(float Blackout, float Now)
{
	bool bPanelsChanged = false;
	for (int32 I = 0; I < Fittings.Num(); I++)
	{
		const LightFitting& Fitting = Fittings[I];
		const float Stutter = Fitting.faulty ? tubeStutter(Fitting.stutterSeed, Now) : 1.0f;
		const float Level = Fitting.output * Stutter * Blackout;
		if (Level == Shown[I])
		{
			continue;
		}
		Shown[I] = Level;
		Lights[I]->SetIntensity(Candelas * Level);
		if (Panels)
		{
			Panels->SetCustomDataValue(I, 0, Level);
			bPanelsChanged = true;
		}
	}
	if (bPanelsChanged)
	{
		Panels->MarkRenderStateDirty();
	}
}
