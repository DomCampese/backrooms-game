#include "BackroomsGameMode.h"
#include "BackroomsWorldSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "Kismet/GameplayStatics.h"

ABackroomsGameMode::ABackroomsGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
}

void ABackroomsGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const int32 Level = UGameplayStatics::GetIntOption(Options, TEXT("level"), 0);
	const int32 Seed = UGameplayStatics::GetIntOption(Options, TEXT("seed"), 1337);
	const int32 Visit = UGameplayStatics::GetIntOption(Options, TEXT("visit"), Level == 0 ? 1 : 0);
	if (UBackroomsWorldSubsystem* Backrooms = GetWorld()->GetSubsystem<UBackroomsWorldSubsystem>())
	{
		Spawn = Backrooms->StartLevel(Level, (uint32)Seed, (uint32)Visit);
	}
}

void ABackroomsGameMode::RestartPlayer(AController* NewPlayer)
{
	RestartPlayerAtTransform(NewPlayer, Spawn);
}
