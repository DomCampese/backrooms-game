#include "BackroomsSceneActor.h"
#include "BackroomsCoords.h"
#include "BackroomsLevelLook.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "port/scene.h"

namespace
{
const TCHAR* BaseMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

enum class EShape : uint8 { Cube, Cylinder, Sphere };

// A plain stand-in for each thing, at its real size in metres (x across, y up,
// z along its heading), and whether its origin is its base or its centre.
struct FDefaultItem
{
	EShape Shape;
	Vec3 Size;
	FColor Colour;
	bool bCentred;
};

const FDefaultItem& DefaultItem(EBackroomsItem Kind)
{
	static const FDefaultItem Items[(int32)EBackroomsItem::Count] = {
		{ EShape::Cylinder, { 0.066f, 0.123f, 0.066f }, FColor(205, 212, 220), false },   // AlmondWater: a 330 ml can
		{ EShape::Cylinder, { 0.17f, 0.024f, 0.17f }, FColor(234, 188, 74), false },      // Doubloon
		{ EShape::Cube, { 0.06f, 0.10f, 0.06f }, FColor(70, 150, 90), false },            // Battery
		{ EShape::Cube, { 0.11f, 0.04f, 0.07f }, FColor(40, 38, 42), false },             // Tape
		{ EShape::Cube, { 0.09f, 0.012f, 0.035f }, FColor(214, 172, 86), false },         // Key
		{ EShape::Cube, { 0.9f, 0.56f, 0.6f }, FColor(150, 118, 76), false },             // Crate
		{ EShape::Cube, { 0.9f, 0.5f, 0.6f }, FColor(110, 86, 56), false },               // CrateOpen
		{ EShape::Cube, { 0.30f, 0.09f, 0.18f }, FColor(70, 70, 76), false },             // Deck
		{ EShape::Cylinder, { 0.17f, 0.024f, 0.17f }, FColor(234, 188, 74), false },      // Coin
		{ EShape::Sphere, { 0.12f, 0.12f, 0.12f }, FColor(255, 150, 60), true },          // Flare
		{ EShape::Cube, { 0.45f, 0.005f, 0.12f }, FColor(230, 228, 214), false },         // Chalk
		{ EShape::Sphere, { 0.34f, 0.40f, 0.34f }, FColor::White, true },                 // Balloon: party colour
		{ EShape::Cube, { 0.05f, 0.05f, 0.005f }, FColor::White, true },                  // Confetti: party colour
		{ EShape::Sphere, { 0.04f, 0.04f, 0.04f }, FColor(30, 28, 26), true },            // Impact
		{ EShape::Cylinder, { 0.7f, 1.96f, 0.7f }, FColor(90, 20, 18), false },           // Hunter
		{ EShape::Cube, { 1.0f, 0.9f, 0.35f }, FColor(60, 30, 26), false },               // Dog
	};
	return Items[FMath::Clamp((int32)Kind, 0, (int32)EBackroomsItem::Count - 1)];
}

UStaticMesh* ShapeMesh(EShape Shape)
{
	switch (Shape)
	{
	case EShape::Cylinder: return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	case EShape::Sphere: return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	default: return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
}

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
	const FDefaultItem& Plain = DefaultItem(Kind);
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Component->SetStaticMesh(Own && Own->Mesh ? Own->Mesh.Get() : ShapeMesh(Plain.Shape));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetupAttachment(RootComponent);
	Component->RegisterComponent();
	if (Own && Own->Material)
	{
		Component->SetMaterial(0, Own->Material);
	}
	else if (!(Own && Own->Mesh))
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BaseMaterial))
		{
			FLinearColor Tint(Plain.Colour);
			if (HasColours(Kind))
			{
				const TArray<FLinearColor> Palette = Look && Look->PartyColours.Num() ? Look->PartyColours
				                                                                        : UBackroomsLevelLook::DefaultPartyColours();
				Tint = Palette[Colour % Palette.Num()];
			}
			UMaterialInstanceDynamic* Flat = UMaterialInstanceDynamic::Create(Base, this);
			Flat->SetVectorParameterValue(TEXT("Color"), Tint);
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
		FTransform Placed;
		if (Own && Own->Mesh)
		{
			Placed = Own->Offset * Frame;
		}
		else
		{
			// Basic shapes are 1 m across, centred on their origin: scale to the
			// thing's size, and lift a base-anchored one by half its height.
			const FDefaultItem& Plain = DefaultItem(Kind);
			const FVector Scale(Plain.Size.z, Plain.Size.x, Plain.Size.y);
			const FVector Lift(0.0, 0.0, Plain.bCentred ? 0.0 : Plain.Size.y * 0.5 * BackroomsCoords::CmPerM);
			Placed = FTransform(FQuat::Identity, Lift, Scale) * Frame;
		}
		Instances(Kind, Item.variant, Look)->AddInstance(Placed, true);
	}
}
