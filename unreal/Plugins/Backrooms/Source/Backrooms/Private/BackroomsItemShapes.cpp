#include "BackroomsItemShapes.h"
#include "BackroomsCoords.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
const TCHAR* BaseMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

enum class EShape : uint8 { Cube, Cylinder, Sphere };

// Each thing at its real size in metres (x across, y up, z along its heading),
// and whether its origin is its base or its centre.
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
}

namespace BackroomsItemShapes
{
FTransform Fit(EBackroomsItem Kind)
{
	const FDefaultItem& Plain = DefaultItem(Kind);
	const FVector Scale(Plain.Size.z, Plain.Size.x, Plain.Size.y);
	const FVector Lift(0.0, 0.0, Plain.bCentred ? 0.0 : Plain.Size.y * 0.5 * BackroomsCoords::CmPerM);
	return FTransform(FQuat::Identity, Lift, Scale);
}

UStaticMesh* Mesh(EBackroomsItem Kind)
{
	switch (DefaultItem(Kind).Shape)
	{
	case EShape::Cylinder: return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	case EShape::Sphere: return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	default: return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
}

UMaterialInterface* Material(EBackroomsItem Kind, UObject* Outer, const FLinearColor* Tint)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BaseMaterial);
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Flat = UMaterialInstanceDynamic::Create(Base, Outer);
	Flat->SetVectorParameterValue(TEXT("Color"), Tint ? *Tint : FLinearColor(DefaultItem(Kind).Colour));
	return Flat;
}
}
