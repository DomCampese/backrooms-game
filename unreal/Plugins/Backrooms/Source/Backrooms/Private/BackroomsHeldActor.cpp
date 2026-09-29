#include "BackroomsHeldActor.h"
#include "BackroomsCoords.h"
#include "BackroomsSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Algo/Sort.h"
#include "port/held.h"
#include "port/view.h"

namespace
{
const TCHAR* const ClipKeys[3] = { TEXT("idle"), TEXT("reload"), TEXT("shoot") };
const TCHAR AxisNames[3] = { 'X', 'Y', 'Z' };

float Axis(const Vec3& V, int32 A) { return A == 0 ? V.x : A == 1 ? V.y : V.z; }
}

ABackroomsHeldActor::ABackroomsHeldActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Animator = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Animator"));
	Animator->SetupAttachment(RootComponent);
	Animator->SetHiddenInGame(true);
	Animator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Hidden, it would otherwise stop evaluating its pose.
	Animator->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Gun = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(RootComponent);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// A viewmodel's shadow lands on the wall beside you as a large dark gun.
	Gun->SetCastShadow(false);
	Gun->SetVisibility(false);

	// The raylib build's flash borrows the flare's point light for 0.09 s.
	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(RootComponent);
	Flash->SetIntensityUnits(ELightUnits::Candelas);
	Flash->SetLightColor(FLinearColor(FColor(255, 175, 70)));
	Flash->SetAttenuationRadius(800.0f);
	Flash->SetCastShadows(false);
	Flash->SetVisibility(false);
}

bool ABackroomsHeldActor::Load()
{
	if (bLoaded || bMissing)
	{
		return bLoaded;
	}
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	USkeletalMesh* Mesh = Settings->RevolverMesh.LoadSynchronous();
	UAnimSequence* Found[3] = { Settings->RevolverIdle.LoadSynchronous(), Settings->RevolverReload.LoadSynchronous(),
		Settings->RevolverShoot.LoadSynchronous() };

	// If the import script's renames did not happen, take whatever the importer
	// made in that folder, by class and by clip name.
	const FString Folder = FPackageName::GetLongPackagePath(Settings->RevolverMesh.ToSoftObjectPath().GetLongPackageName());
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(*Folder), Assets, true);
	int32 StaticMeshes = 0;
	for (const FAssetData& Asset : Assets)
	{
		const FString Name = Asset.AssetName.ToString().ToLower();
		if (Asset.IsInstanceOf(USkeletalMesh::StaticClass()))
		{
			if (!Mesh)
			{
				Mesh = Cast<USkeletalMesh>(Asset.GetAsset());
			}
		}
		else if (Asset.IsInstanceOf(UAnimSequence::StaticClass()))
		{
			for (int32 i = 0; i < 3; ++i)
			{
				if (!Found[i] && Name.Contains(ClipKeys[i]))
				{
					Found[i] = Cast<UAnimSequence>(Asset.GetAsset());
				}
			}
		}
		else if (Asset.IsInstanceOf(UStaticMesh::StaticClass()))
		{
			++StaticMeshes;
		}
	}
	if (!Mesh)
	{
		Status = Assets.Num() == 0
			? FString::Printf(TEXT("revolver: nothing in %s. The editor has not imported assets/models/revolver.glb (Output Log, LogPython)"), *Folder)
			: StaticMeshes > 0
			? FString::Printf(TEXT("revolver: %s holds %d static meshes and no skeletal mesh; the importer did not see the skin"), *Folder, StaticMeshes)
			: FString::Printf(TEXT("revolver: no skeletal mesh among the %d assets in %s"), Assets.Num(), *Folder);
		bMissing = true;
		return false;
	}
	FString NoClips;
	for (int32 i = 0; i < 3; ++i)
	{
		if (!Found[i])
		{
			NoClips += FString(NoClips.IsEmpty() ? TEXT("") : TEXT(", ")) + ClipKeys[i];
		}
	}
	if (!NoClips.IsEmpty())
	{
		Status = FString::Printf(TEXT("revolver: %s has the mesh but no clip named %s"), *Folder, *NoClips);
		bMissing = true;
		return false;
	}
	Clips = { Found[0], Found[1], Found[2] };

	// The barrel runs along the GLB's z, the grip down its y, and the gun is
	// thinnest across x: so the mesh's longest axis holds z, its shortest x.
	// The gun is about 0.3 m long, so a half-length under 2 units is metres.
	const FVector Extent = Mesh->GetBounds().BoxExtent;
	int32 Order[3] = { 0, 1, 2 };   // mesh axes, shortest first
	Algo::Sort(Order, [&Extent](int32 A, int32 B) { return Extent[A] < Extent[B]; });
	for (int32 i = 0; i < 3; ++i)
	{
		Perm[Order[i]] = i;
	}
	// An odd permutation is a mirror, which turns a rotation the other way.
	const int32 Swaps = (Perm[0] > Perm[1]) + (Perm[0] > Perm[2]) + (Perm[1] > Perm[2]);
	Sense = Swaps % 2 ? -1.0f : 1.0f;
	Units = Extent[Order[2]] < 2.0 ? 1.0f : BackroomsCoords::CmPerM;

	Animator->SetSkinnedAssetAndUpdate(Mesh, true);
	Animator->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Gun->SetSkinnedAssetAndUpdate(Mesh, true);

	// The drum and the six live rounds turn together; the spent cases do not.
	for (int32 Bone = 0; Bone < Gun->GetNumBones(); ++Bone)
	{
		const FName Name = Gun->GetBoneName(Bone);
		const FString Text = Name.ToString();
		if (Text == TEXT("DEF_Cylinder") || (Text.StartsWith(TEXT("DEF_Bullet")) && !Text.StartsWith(TEXT("DEF_BulletFired"))))
		{
			DrumBones.Add(Name);   // bone index order: a parent before its children
		}
		if (Text == TEXT("DEF_RevolverHandle"))
		{
			Handle = Name;
		}
	}
	if (Handle.IsNone())
	{
		Status = FString::Printf(TEXT("revolver: %s has %d bones and none is DEF_RevolverHandle"), *Mesh->GetName(), Gun->GetNumBones());
		bMissing = true;
		return false;
	}
	bLoaded = true;
	return true;
}

void ABackroomsHeldActor::Show(const Sim& Game, const FVector& Origin)
{
	const FString Before = Status;
	ShowRevolver(Game, Origin);
	// The "shown" line carries sizes that move every frame; log only its kind.
	if (Status.Left(16) != Before.Left(16))
	{
		UE_LOG(LogTemp, Display, TEXT("Backrooms: %s"), *Status);
	}
}

void ABackroomsHeldActor::ShowRevolver(const Sim& Game, const FVector& Origin)
{
	const bool bRevolver = heldItem(Game) == Held::Revolver;
	if (!bRevolver || !Load())
	{
		Gun->SetVisibility(false);
		Flash->SetVisibility(false);
		return;
	}
	Gun->SetVisibility(true);

	// The GLB's +x, +y, +z go to the frame's right, up and forward, and each
	// mesh axis holds one of them (Perm). The matrix may be a mirror, as the
	// raylib build's is; FTransform keeps that as a negative scale.
	const HeldFrame Frame = heldWeapon(Game, simView(Game));
	const Vec3 Dirs[3] = { Frame.right, Frame.up, Frame.forward };
	const float Scale = Frame.scale * BackroomsCoords::CmPerM / Units;
	FVector Rows[3];
	for (int32 k = 0; k < 3; ++k)
	{
		Rows[k] = BackroomsCoords::ToUnrealDirection(Dirs[Perm[k]]) * Scale;
	}
	const FVector Eye = BackroomsCoords::ToUnreal(simView(Game).eye) + Origin;
	const FTransform Placed(FMatrix(Rows[0], Rows[1], Rows[2], BackroomsCoords::ToUnreal(Frame.pos) + Origin));
	// A point in the GLB's frame, in metres, as the mesh holds it.
	auto FromGlb = [this](const Vec3& V) {
		return FVector(Axis(V, Perm[0]), Axis(V, Perm[1]), Axis(V, Perm[2])) * Units;
	};
	const FTransform Fix(GetDefault<UBackroomsSettings>()->RevolverMeshRotation);
	Animator->SetWorldTransform(Fix * Placed);
	Gun->SetWorldTransform(Fix * Placed);

	// Pose the hidden animator at the clip's time and copy it.
	const RevolverPose Pose = revolverPose(Game.reloadT, Game.gunCd, Game.ammo);
	UAnimSequence* Clip = Clips[(int32)Pose.clip];
	UAnimSingleNodeInstance* Node = Animator->GetSingleNodeInstance();
	if (!Node || Node->GetAnimationAsset() != Clip)
	{
		Animator->PlayAnimation(Clip, false);
		Node = Animator->GetSingleNodeInstance();
	}
	if (Node)
	{
		Node->SetPlaying(false);
		Node->SetPosition(Pose.progress * Clip->GetPlayLength(), false);
	}
	Animator->TickAnimation(0.0f, false);
	Animator->RefreshBoneTransforms();
	Gun->CopyPoseFromSkeletalComponent(Animator);

	// Turn the drum and its rounds about the handle's +z.
	// Every target is read before any is set, so a round parented to the drum
	// is not turned twice.
	const FTransform HandleAt = Gun->GetBoneTransformByName(Handle, EBoneSpaces::ComponentSpace);
	const FVector Pivot = HandleAt.TransformPosition(FromGlb(REVOLVER_DRUM_PIVOT));
	FVector DrumAxis = FVector::ZeroVector;
	DrumAxis[Perm[0] == 2 ? 0 : Perm[1] == 2 ? 1 : 2] = 1.0;
	DrumAxis = HandleAt.TransformVectorNoScale(DrumAxis).GetSafeNormal();
	const FQuat Spin(DrumAxis, Sense * Pose.drumTurns * UE_PI / 3.0f);
	TArray<FTransform> Turned;
	for (const FName& Bone : DrumBones)
	{
		FTransform At = Gun->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace);
		At.SetLocation(Pivot + Spin.RotateVector(At.GetLocation() - Pivot));
		At.SetRotation(Spin * At.GetRotation());
		Turned.Add(At);
	}
	for (int32 i = 0; i < DrumBones.Num(); ++i)
	{
		Gun->SetBoneTransformByName(DrumBones[i], Turned[i], EBoneSpaces::ComponentSpace);
	}

	const FBoxSphereBounds Shown = Gun->Bounds;
	Status = FString::Printf(TEXT("revolver: shown, %.0f cm across, %.0f cm from the eye; barrel on mesh %c, %s"),
		Shown.SphereRadius * 2.0, FVector::Dist(Shown.Origin, Eye),
		AxisNames[Perm[0] == 2 ? 0 : Perm[1] == 2 ? 1 : 2], Units == 1.0f ? TEXT("imported in metres") : TEXT("imported in cm"));

	if (Game.muzzleT > 0.0f)
	{
		const float Life = Game.muzzleT / Sim::MUZZLE_FLASH;
		Flash->SetWorldLocation(Gun->GetComponentTransform().TransformPosition(HandleAt.TransformPosition(FromGlb(REVOLVER_MUZZLE))));
		Flash->SetIntensity(80.0f * Life);
		Flash->SetVisibility(true);
	}
	else
	{
		Flash->SetVisibility(false);
	}
}
