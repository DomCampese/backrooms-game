#include "BackroomsSpriteActor.h"
#include "BackroomsCoords.h"
#include "BackroomsLevelLook.h"
#include "BackroomsSettings.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "port/sprites.h"

namespace
{
constexpr int32 SheetCount = (int32)SpriteSheet::Count;
constexpr int32 GlowIndex = SheetCount;

// The quads of one sheet, built up over a frame.
struct FQuads
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colours;

	// Corners counter-clockwise from the bottom left, as the camera sees them.
	void Add(const FVector& Centre, const FVector& Right, const FVector& Facing, float W, float H, const FBox2D& UV,
		float Alpha)
	{
		const int32 Base = Vertices.Num();
		const FVector HalfW = Right * (W * 0.5 * BackroomsCoords::CmPerM);
		const FVector HalfH = FVector::UpVector * (H * 0.5 * BackroomsCoords::CmPerM);
		Vertices.Append({ Centre - HalfW - HalfH, Centre + HalfW - HalfH, Centre + HalfW + HalfH, Centre - HalfW + HalfH });
		UVs.Append({ FVector2D(UV.Min.X, UV.Max.Y), FVector2D(UV.Max.X, UV.Max.Y), FVector2D(UV.Max.X, UV.Min.Y),
			FVector2D(UV.Min.X, UV.Min.Y) });
		for (int32 I = 0; I < 4; I++)
		{
			Normals.Add(Facing);
			Colours.Add(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
		}
		// The material is two-sided, so the winding does not hide a quad.
		Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}
};

// Where a frame sits on its sheet, as texture coordinates (port/sheets.h).
FBox2D FrameUV(const ActorSprite& Sprite, int32 Frame)
{
	if (Sprite.sheet == SpriteSheet::Dog)
	{
		const double W = 1.0 / DOG_FRAMES;
		return FBox2D(FVector2D(Frame * W, 0.0), FVector2D((Frame + 1) * W, 1.0));
	}
	const double W = 1.0 / ENT_FRAMES, H = 1.0 / ENT_ROWS;
	return FBox2D(FVector2D(Frame * W, Sprite.row * H), FVector2D((Frame + 1) * W, (Sprite.row + 1) * H));
}

void Publish(UProceduralMeshComponent& Mesh, int32 Section, const FQuads& Quads, UMaterialInterface* Material)
{
	if (Quads.Vertices.IsEmpty())
	{
		Mesh.ClearMeshSection(Section);
		return;
	}
	Mesh.CreateMeshSection_LinearColor(Section, Quads.Vertices, Quads.Triangles, Quads.Normals, Quads.UVs,
		Quads.Colours, TArray<FProcMeshTangent>(), false);
	Mesh.SetMaterial(Section, Material);
}
}

ABackroomsSpriteActor::ABackroomsSpriteActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Glow = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Glow"));
	Glow->SetupAttachment(RootComponent);
	Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Glow->SetCastShadow(false);
	Glow->SetTranslucentSortPriority(1);
}

bool ABackroomsSpriteActor::Ready()
{
	if (bTried)
	{
		return Materials.Num() == SheetCount + 1;
	}
	bTried = true;
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	UMaterialInterface* Lit = Settings->SpriteMaterial.LoadSynchronous();
	UMaterialInterface* Unlit = Settings->SpriteGlowMaterial.LoadSynchronous();
	const TSoftObjectPtr<UTexture2D>* Sheets[SheetCount + 1] = { &Settings->ClarkSheet, &Settings->SmilerSheet,
		&Settings->PartygoerSheet, &Settings->DogSheet, &Settings->SmilerGlowSheet };
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Made;
	for (int32 I = 0; I <= SheetCount; I++)
	{
		UTexture2D* Sheet = Sheets[I]->LoadSynchronous();
		UMaterialInterface* Parent = I == GlowIndex ? Unlit : Lit;
		if (!Sheet || !Parent)
		{
			UE_LOG(LogTemp, Display, TEXT("Backrooms: no sprite sheets or sprite materials yet; the hunter and the pack are plain shapes. Open the editor to build them"));
			return false;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
		Material->SetTextureParameterValue(TEXT("Sheet"), Sheet);
		Made.Add(Material);
	}
	Materials = Made;
	UE_LOG(LogTemp, Display, TEXT("Backrooms: the hunter and the pack are sprites"));
	return true;
}

bool ABackroomsSpriteActor::Draws(EBackroomsItem Kind, const UBackroomsLevelLook* Look)
{
	if (Kind != EBackroomsItem::Hunter && Kind != EBackroomsItem::Dog)
	{
		return false;
	}
	const FBackroomsItemLook* Own = Look ? Look->Items.Find(Kind) : nullptr;
	return !(Own && Own->Mesh) && Ready();
}

void ABackroomsSpriteActor::Show(const std::vector<ActorSprite>& Sprites, const UBackroomsLevelLook* Look,
	const FVector& Camera, const FVector& Origin)
{
	FQuads Quads[SheetCount + 1];
	for (const ActorSprite& Sprite : Sprites)
	{
		const EBackroomsItem Kind = Sprite.sheet == SpriteSheet::Dog ? EBackroomsItem::Dog : EBackroomsItem::Hunter;
		if (!Draws(Kind, Look))
		{
			continue;
		}
		const FVector Centre = BackroomsCoords::ToUnreal(Sprite.centre) + Origin;
		FVector Facing = Camera - Centre;
		Facing.Z = 0.0;
		Facing = Facing.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		// Unreal's right is Up x Forward, and the camera looks along -Facing.
		const FVector Right = FVector::CrossProduct(Facing, FVector::UpVector);
		const int32 Frames[2] = { Sprite.frame0, Sprite.frame1 };
		const float Weights[2] = { 1.0f - Sprite.blend, Sprite.blend };
		for (int32 F = 0; F < 2; F++)
		{
			const FBox2D UV = FrameUV(Sprite, Frames[F]);
			const float Alpha = Weights[F] * Sprite.fade;
			Quads[(int32)Sprite.sheet].Add(Centre, Right, Facing, Sprite.w, Sprite.h, UV, Alpha);
			if (Sprite.glow)
			{
				Quads[GlowIndex].Add(Centre, Right, Facing, Sprite.w, Sprite.h, UV, Alpha);
			}
		}
	}
	if (!Ready())
	{
		return;
	}
	for (int32 I = 0; I < SheetCount; I++)
	{
		Publish(*Body, I, Quads[I], Materials[I]);
	}
	Publish(*Glow, 0, Quads[GlowIndex], Materials[GlowIndex]);
}
