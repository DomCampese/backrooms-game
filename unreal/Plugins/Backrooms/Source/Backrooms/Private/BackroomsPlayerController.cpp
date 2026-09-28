#include "BackroomsPlayerController.h"
#include "BackroomsCoords.h"
#include "BackroomsPawn.h"
#include "BackroomsWorldSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "port/view.h"
#include "sim/input_frame.h"

namespace
{
using EControl = ABackroomsPlayerController::EControl;

// Free camera speed, metres per second, and how much faster with sprint held.
constexpr float FlySpeed = 6.0f;
constexpr float FlySprint = 3.0f;
// The sim's look rate (Sim::updateLook), for the free camera.
constexpr float LookRadPerPx = 0.0030f;

struct FControlKeys
{
	EControl Control;
	EInputActionValueType Type;
	FKey Keys[2];
};

// The raylib build's keys (Game::readInput).
TArray<FControlKeys> ControlKeys()
{
	using V = EInputActionValueType;
	return {
		{ EControl::Forward, V::Boolean, { EKeys::W } },
		{ EControl::Back, V::Boolean, { EKeys::S } },
		{ EControl::Left, V::Boolean, { EKeys::A } },
		{ EControl::Right, V::Boolean, { EKeys::D } },
		{ EControl::Sprint, V::Boolean, { EKeys::LeftShift } },
		{ EControl::Crouch, V::Boolean, { EKeys::LeftControl } },
		{ EControl::Squeeze, V::Boolean, { EKeys::C } },
		{ EControl::Jump, V::Boolean, { EKeys::SpaceBar } },
		{ EControl::PickRevolver, V::Boolean, { EKeys::One } },
		{ EControl::PickFlare, V::Boolean, { EKeys::Two } },
		{ EControl::PickDeck, V::Boolean, { EKeys::Four } },
		{ EControl::Fire, V::Boolean, { EKeys::LeftMouseButton } },
		{ EControl::Aim, V::Boolean, { EKeys::RightMouseButton } },
		{ EControl::Reload, V::Boolean, { EKeys::R } },
		{ EControl::Throw, V::Boolean, { EKeys::Q } },
		{ EControl::Flashlight, V::Boolean, { EKeys::F, EKeys::L } },
		{ EControl::Use, V::Boolean, { EKeys::E } },
		{ EControl::Drink, V::Boolean, { EKeys::Three } },
		{ EControl::Chalk, V::Boolean, { EKeys::M } },
		{ EControl::Pause, V::Boolean, { EKeys::P } },
		{ EControl::DebugHud, V::Boolean, { EKeys::F3 } },
		{ EControl::DevBlackout, V::Boolean, { EKeys::B } },
		{ EControl::DevBanish, V::Boolean, { EKeys::H } },
		{ EControl::DevRefill, V::Boolean, { EKeys::G } },
		{ EControl::DevStoreyUp, V::Boolean, { EKeys::PageUp } },
		{ EControl::DevStoreyDown, V::Boolean, { EKeys::PageDown } },
		{ EControl::DevNextLevel, V::Boolean, { EKeys::N } },
		// raylib begins on any key; AnyKey here would also catch mouse motion, so
		// the title screen takes Enter, Space or a click (Fire).
		{ EControl::Begin, V::Boolean, { EKeys::Enter, EKeys::SpaceBar } },
		{ EControl::Look, V::Axis2D, { EKeys::Mouse2D } },
		{ EControl::Wheel, V::Axis1D, { EKeys::MouseWheelAxis } },
	};
}
}

void ABackroomsPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void ABackroomsPlayerController::MakeControls()
{
	Controls = NewObject<UInputMappingContext>(this);
	Actions.SetNum((int32)EControl::Count);
	for (const FControlKeys& Entry : ControlKeys())
	{
		UInputAction* Action = NewObject<UInputAction>(this);
		Action->ValueType = Entry.Type;
		for (const FKey& Key : Entry.Keys)
		{
			if (Key.IsValid())
			{
				Controls->MapKey(Action, Key);
			}
		}
		Actions[(int32)Entry.Control] = Action;
	}
}

void ABackroomsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!Controls)
	{
		MakeControls();
	}
	// Bound for their values only: the controller reads them once a frame.
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		for (const TObjectPtr<UInputAction>& Action : Actions)
		{
			Input->BindActionValue(Action);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Backrooms: the input component is not Enhanced Input's; see Config/DefaultInput.ini"));
	}
	AddMappingContext();
}

void ABackroomsPlayerController::AddMappingContext()
{
	if (bContextAdded || !Controls)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Input->AddMappingContext(Controls, 0);
		bContextAdded = true;
	}
}

void ABackroomsPlayerController::ReadControls()
{
	FMemory::Memcpy(bWasHeld, bHeld, sizeof(bHeld));
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}
	for (int32 C = 0; C < (int32)EControl::Count; C++)
	{
		const FInputActionValue Value = Input->GetBoundActionValue(Actions[C]);
		if (C == (int32)EControl::Look)
		{
			LookDelta = Value.Get<FVector2D>();
		}
		else if (C == (int32)EControl::Wheel)
		{
			Wheel = Value.Get<float>();
		}
		else
		{
			bHeld[C] = Value.Get<bool>();
		}
	}
}

void ABackroomsPlayerController::SetFreeCameraStart(const FTransform& Start)
{
	const FRotator R = Start.Rotator();
	FreeYaw = FMath::DegreesToRadians(R.Yaw);
	FreePitch = FMath::DegreesToRadians(R.Pitch);
}

void ABackroomsPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	AddMappingContext();
	ReadControls();
	UBackroomsWorldSubsystem* Backrooms = GetWorld()->GetSubsystem<UBackroomsWorldSubsystem>();
	ABackroomsPawn* Eye = GetPawn<ABackroomsPawn>();
	if (!Backrooms || !Eye)
	{
		return;
	}
	int32 ViewW = 0, ViewH = 0;
	GetViewportSize(ViewW, ViewH);
	const float Aspect = ViewH > 0 ? float(ViewW) / float(ViewH) : 1440.0f / 850.0f;
	if (!Backrooms->IsRunning())
	{
		FlyFreeCamera(DeltaTime);
		Eye->SetView(FTransform(BackroomsCoords::ToUnrealRotator(FreeYaw, FreePitch), Eye->GetActorLocation()),
			windowFovY(ViewW, ViewH, 0.0f), Aspect);
		return;
	}

	// The order Game::tick reads them in: pause, the debug HUD, then the frame.
	const Sim* Game = Backrooms->GetSim();
	const bool bPauseToggled = !Game->inMenu && Pressed(EControl::Pause);
	if (!Game->inMenu && !Game->paused && Pressed(EControl::DebugHud))
	{
		bDebugHud = !bDebugHud;
	}
	InputFrame In;
	In.playing = !bShowMouseCursor;
	In.forward = Down(EControl::Forward);
	In.back = Down(EControl::Back);
	In.left = Down(EControl::Left);
	In.right = Down(EControl::Right);
	In.forwardPressed = Pressed(EControl::Forward);
	In.sprint = Down(EControl::Sprint);
	In.crouch = Down(EControl::Crouch);
	In.squeeze = Down(EControl::Squeeze);
	In.jumpHeld = Down(EControl::Jump);
	In.jumpPressed = Pressed(EControl::Jump);
	// raylib's mouse delta is screen pixels, y down; Unreal's y is up.
	In.look = { float(LookDelta.X) * LookScale, float(LookDelta.Y) * LookScale * (bInvertLookY ? 1.0f : -1.0f) };
	In.wheel = Wheel;
	In.pickRevolver = Pressed(EControl::PickRevolver);
	In.pickFlare = Pressed(EControl::PickFlare);
	In.pickDeck = Pressed(EControl::PickDeck);
	In.fire = Pressed(EControl::Fire);
	In.aim = Down(EControl::Aim);
	In.reload = Pressed(EControl::Reload);
	In.throwFlare = Pressed(EControl::Throw);
	In.flashlight = Pressed(EControl::Flashlight);
	In.use = Pressed(EControl::Use);
	In.drink = Pressed(EControl::Drink);
	In.chalk = Pressed(EControl::Chalk);
	In.begin = Pressed(EControl::Begin) || Pressed(EControl::Fire);
	if (bDebugHud)
	{
		In.dev.blackout = Pressed(EControl::DevBlackout);
		In.dev.spawnAhead = Pressed(EControl::Use);
		In.dev.chase = Pressed(EControl::Squeeze);
		In.dev.banish = Pressed(EControl::DevBanish);
		In.dev.refill = Pressed(EControl::DevRefill);
		In.dev.storeyUp = Pressed(EControl::DevStoreyUp);
		In.dev.storeyDown = Pressed(EControl::DevStoreyDown);
		In.dev.nextLevel = Pressed(EControl::DevNextLevel);
	}
	In.screenFov = windowFovY(ViewW, ViewH, 0.0f);

	Backrooms->TickRun(In, bPauseToggled, DeltaTime);
	float FovY = 70.0f;
	const FTransform View = Backrooms->ViewTransform(FovY);
	Eye->SetView(View, FovY, Aspect);
}

void ABackroomsPlayerController::FlyFreeCamera(float DeltaTime)
{
	FreeYaw += float(LookDelta.X) * LookScale * LookRadPerPx;
	const float Up = float(LookDelta.Y) * LookScale * (bInvertLookY ? -1.0f : 1.0f);
	FreePitch = FMath::Clamp(FreePitch + Up * LookRadPerPx, -1.45f, 1.45f);
	const float Forward = (Down(EControl::Forward) ? 1.0f : 0.0f) - (Down(EControl::Back) ? 1.0f : 0.0f);
	const float Right = (Down(EControl::Right) ? 1.0f : 0.0f) - (Down(EControl::Left) ? 1.0f : 0.0f);
	const float Rise = (Down(EControl::Jump) ? 1.0f : 0.0f) - (Down(EControl::Crouch) ? 1.0f : 0.0f);
	const Vec3 Fwd = { FMath::Cos(FreePitch) * FMath::Cos(FreeYaw), FMath::Sin(FreePitch), FMath::Cos(FreePitch) * FMath::Sin(FreeYaw) };
	const Vec3 Side = { -FMath::Sin(FreeYaw), 0.0f, FMath::Cos(FreeYaw) };
	const float Metres = FlySpeed * (Down(EControl::Sprint) ? FlySprint : 1.0f) * DeltaTime;
	const Vec3 Step = { (Fwd.x * Forward + Side.x * Right) * Metres, (Fwd.y * Forward + Rise) * Metres,
		(Fwd.z * Forward + Side.z * Right) * Metres };
	if (APawn* Eye = GetPawn())
	{
		Eye->SetActorLocation(Eye->GetActorLocation() + BackroomsCoords::ToUnrealDirection(Step) * BackroomsCoords::CmPerM);
	}
}
