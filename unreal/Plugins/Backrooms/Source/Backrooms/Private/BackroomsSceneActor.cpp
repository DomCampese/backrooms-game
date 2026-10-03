#include "BackroomsSceneActor.h"
#include "BackroomsCoords.h"
#include "BackroomsItemShapes.h"
#include "BackroomsLevelLook.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "port/scene.h"

namespace
{
bool HasColours(EBackroomsItem Kind) { return Kind == EBackroomsItem::Balloon || Kind == EBackroomsItem::Confetti; }
}

ABackroomsSceneActor::ABackroomsSceneActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

UInstancedStaticMeshComponent* ABackroomsSceneActor::Instances(EBackroomsItem Kind, uint8 Colour,
	const UBackroomsLevelLook* Look)
{
	const int32 Key = (int32)Kind * 256 + (HasColours(Kind) ? Colour : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = ByKind.Find(Key))
	{
		return *Found;
	}
	const FBackroomsItemLook* Own = Look ? Look->Items.Find(Kind) : nullptr;
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Component->SetStaticMesh(Own && Own->Mesh ? Own->Mesh.Get() : BackroomsItemShapes::Mesh(Kind));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetupAttachment(RootComponent);
	Component->RegisterComponent();
	if (Own && Own->Material)
	{
		Component->SetMaterial(0, Own->Material);
	}
	else if (!(Own && Own->Mesh))
	{
		FLinearColor Party;
		if (HasColours(Kind))
		{
			const TArray<FLinearColor> Palette = Look && Look->PartyColours.Num() ? Look->PartyColours
			                                                                        : UBackroomsLevelLook::DefaultPartyColours();
			Party = Palette[Colour % Palette.Num()];
		}
		if (UMaterialInterface* Flat = BackroomsItemShapes::Material(Kind, this, HasColours(Kind) ? &Party : nullptr))
		{
			Component->SetMaterial(0, Flat);
		}
	}
	ByKind.Add(Key, Component);
	return Component;
}

void ABackroomsSceneActor::Show(const std::vector<SceneItem>& Items, const UBackroomsLevelLook* Look, const FVector& Origin)
{
	if (Look != ShownLook)
	{
		for (const TPair<int32, TObjectPtr<UInstancedStaticMeshComponent>>& Entry : ByKind)
		{
			if (Entry.Value)
			{
				Entry.Value->DestroyComponent();
			}
		}
		ByKind.Reset();
		ShownLook = Look;
	}
	for (const TPair<int32, TObjectPtr<UInstancedStaticMeshComponent>>& Entry : ByKind)
	{
		Entry.Value->ClearInstances();
	}
	for (const SceneItem& Item : Items)
	{
		const EBackroomsItem Kind = (EBackroomsItem)Item.kind;
		const FTransform Frame(BackroomsCoords::ToUnrealRotator(Item.yaw, 0.0f), BackroomsCoords::ToUnreal(Item.pos) + Origin);
		const FBackroomsItemLook* Own = Look ? Look->Items.Find(Kind) : nullptr;
		const FTransform Placed = (Own && Own->Mesh ? Own->Offset : BackroomsItemShapes::Fit(Kind)) * Frame;
		Instances(Kind, Item.variant, Look)->AddInstance(Placed, true);
	}
}
