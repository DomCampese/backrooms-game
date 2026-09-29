#include "BackroomsPawn.h"
#include "Camera/CameraComponent.h"

ABackroomsPawn::ABackroomsPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->bUsePawnControlRotation = false;
	RootComponent = Camera;
	SetActorEnableCollision(false);
}

void ABackroomsPawn::SetView(const FTransform& View, float FovY, float Aspect)
{
	SetActorTransform(View);
	const float HalfY = FMath::DegreesToRadians(FovY) * 0.5f;
	Camera->SetFieldOfView(FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfY) * Aspect)));
}
