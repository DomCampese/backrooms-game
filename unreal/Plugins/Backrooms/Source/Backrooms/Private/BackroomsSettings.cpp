#include "BackroomsSettings.h"
#include "BackroomsLevelLook.h"
#include "core/level_rules.h"

UBackroomsSettings::UBackroomsSettings()
{
	for (int32 Level = 0; Level < NLEVELS; Level++)
	{
		const FString Name = FString::Printf(TEXT("DA_Level%d"), Level);
		LevelLooks.Add(TSoftObjectPtr<UBackroomsLevelLook>(
			FSoftObjectPath(FString::Printf(TEXT("/Game/Backrooms/Looks/%s.%s"), *Name, *Name))));
	}
}

UBackroomsLevelLook* UBackroomsSettings::LookFor(int32 Level) const
{
	if (!LevelLooks.IsValidIndex(Level) || LevelLooks[Level].IsNull())
	{
		return nullptr;
	}
	// Null while the editor has not built the looks yet.
	return LevelLooks[Level].LoadSynchronous();
}
