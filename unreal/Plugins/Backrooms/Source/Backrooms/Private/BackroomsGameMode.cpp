#include "BackroomsGameMode.h"
#include "BackroomsPawn.h"
#include "BackroomsPlayerController.h"
#include "BackroomsWorldSubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

ABackroomsGameMode::ABackroomsGameMode()
{
	DefaultPawnClass = ABackroomsPawn::StaticClass();
	PlayerControllerClass = ABackroomsPlayerController::StaticClass();
}

void ABackroomsGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	UBackroomsWorldSubsystem* Backrooms = GetWorld()->GetSubsystem<UBackroomsWorldSubsystem>();
	if (!Backrooms)
	{
		return;
	}
	const int32 Level = UGameplayStatics::GetIntOption(Options, TEXT("level"), 0);
	const int32 Seed = UGameplayStatics::GetIntOption(Options, TEXT("seed"), 1337);
	if (UGameplayStatics::ParseOption(Options, TEXT("mode")) == TEXT("free"))
	{
		const int32 Visit = UGameplayStatics::GetIntOption(Options, TEXT("visit"), Level == 0 ? 1 : 0);
		Spawn = Backrooms->StartFreeCamera(Level, (uint32)Seed, (uint32)Visit);
	}
	else
	{
		Backrooms->StartRun((uint32)Seed, Level);
		float FovY = 70.0f;
		Spawn = Backrooms->ViewTransform(FovY);
	}
}

void ABackroomsGameMode::RestartPlayer(AController* NewPlayer)
{
	RestartPlayerAtTransform(NewPlayer, Spawn);
	if (ABackroomsPlayerController* Controller = Cast<ABackroomsPlayerController>(NewPlayer))
	{
		Controller->SetFreeCameraStart(Spawn);
	}
}
