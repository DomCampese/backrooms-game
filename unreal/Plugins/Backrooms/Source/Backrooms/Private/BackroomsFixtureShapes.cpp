#include "BackroomsFixtureShapes.h"
#include "BackroomsCoords.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "core/layout.h"

namespace
{
const TCHAR* BaseMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

// A plain box: across the face, up it and out of it, metres; or for a run, its
// thickness. Sizes follow the raylib build's fittings (FIXTURES in textures.cpp,
// the mesher's builders).
struct FPlainFixture
{
	bool bShown;
	bool bRun;
	Vec3 Size;   // across, up, depth; a run uses Size.x as its thickness
	FColor Colour;
};

const FPlainFixture& PlainOf(FixtureKind Kind)
{
	static const FPlainFixture Kinds[(int32)EBackroomsFixture::Count] = {
		{ true, false, { 0.075f, 0.118f, 0.012f }, FColor(235, 232, 220) },   // Outlet
		{ true, false, { 0.075f, 0.118f, 0.012f }, FColor(90, 84, 76) },      // BrokenOutlet
		{ true, false, { 0.072f, 0.115f, 0.012f }, FColor(235, 232, 220) },   // Switch
		{ true, false, { 0.56f, 0.40f, 0.010f }, FColor(200, 200, 196) },     // Grille
		{ true, false, { 0.56f, 0.20f, 0.060f }, FColor(40, 190, 90) },       // ExitSign
		{ true, false, { 0.60f, 0.60f, 0.020f }, FColor(220, 220, 214) },     // Diffuser
		{ true, false, { 0.074f, 0.074f, 0.040f }, FColor(190, 190, 190) },   // Sprinkler
		{ true, true, { 0.040f, 0, 0 }, FColor(150, 152, 150) },              // Conduit
		{ false, false, {}, FColor::White },                                  // Scrawl
		{ true, false, { 1.40f, 2.46f, 0.045f }, FColor(142, 146, 144) },     // LiftDoor
		{ false, false, {}, FColor::White },                                  // Spall
		{ false, false, {}, FColor::White },                                  // PillarSpall
		{ true, true, { 0, 0, 0 }, FColor(120, 110, 100) },                   // Pipe: 2 * Fixture::w thick
		{ true, true, { 0.060f, 0, 0 }, FColor(196, 62, 48) },                // Valve
		{ true, true, { 0.020f, 0, 0 }, FColor(220, 80, 120) },               // Streamer
		{ false, false, {}, FColor::White },                                  // ManilaRoom
	};
	return Kinds[FMath::Clamp((int32)Kind, 0, (int32)EBackroomsFixture::Count - 1)];
}

Vec3 Add(Vec3 A, Vec3 B, float K) { return { A.x + B.x * K, A.y + B.y * K, A.z + B.z * K }; }

FQuat FaceRotation(const Fixture& F)
{
	const FVector Out = BackroomsCoords::ToUnrealDirection(F.normal);
	const FVector Up = FMath::Abs(Out.Z) > 0.5 ? FVector::ForwardVector : FVector::UpVector;
	return FRotationMatrix::MakeFromXZ(Out, Up).ToQuat();
}

bool IsRun(const Fixture& F) { return PlainOf(F.kind).bRun; }
}

namespace BackroomsFixtureShapes
{
FTransform Frame(const Fixture& F)
{
	const FVector Start = BackroomsCoords::ToUnreal(F.pos);
	if (IsRun(F))
	{
		const FVector Along = BackroomsCoords::ToUnreal(F.end) - Start;
		const double Metres = Along.Size() / BackroomsCoords::CmPerM;
		return FTransform(FRotationMatrix::MakeFromX(Along).ToQuat(), Start, FVector(Metres, 1.0, 1.0));
	}
	return FTransform(FaceRotation(F), Start);
}

bool Plain(const Fixture& F, FTransform& Out)
{
	const FPlainFixture& Box = PlainOf(F.kind);
	if (!Box.bShown)
	{
		return false;
	}
	if (Box.bRun)
	{
		const FVector Start = BackroomsCoords::ToUnreal(F.pos), End = BackroomsCoords::ToUnreal(F.end);
		const double Thick = F.kind == FixtureKind::Pipe ? 2.0 * F.w : Box.Size.x;
		Out = FTransform(FRotationMatrix::MakeFromX(End - Start).ToQuat(), (Start + End) * 0.5,
			FVector((End - Start).Size() / BackroomsCoords::CmPerM, Thick, Thick));
		return true;
	}
	// Out of the face by half its depth; a lift door is laid out along +x from
	// the edge's start, up from the floor (addLiftDoor in world_mesh.cpp).
	Vec3 Centre = Add(F.pos, F.normal, Box.Size.z * 0.5f);
	if (F.kind == FixtureKind::LiftDoor)
	{
		Centre = Add(Add(Centre, { 1, 0, 0 }, CELL * 0.5f), { 0, 1, 0 }, Box.Size.y * 0.5f);
	}
	Out = FTransform(FaceRotation(F), BackroomsCoords::ToUnreal(Centre), FVector(Box.Size.z, Box.Size.x, Box.Size.y));
	return true;
}

UStaticMesh* Mesh() { return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")); }

UMaterialInterface* Material(EBackroomsFixture Kind, UObject* Outer)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BaseMaterial);
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Flat = UMaterialInstanceDynamic::Create(Base, Outer);
	Flat->SetVectorParameterValue(TEXT("Color"), FLinearColor(PlainOf((FixtureKind)Kind).Colour));
	return Flat;
}
}
