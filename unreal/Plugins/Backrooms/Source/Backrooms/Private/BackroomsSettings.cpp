#include "BackroomsSettings.h"
#include "BackroomsLevelLook.h"

UBackroomsLevelLook* UBackroomsSettings::LookFor(int32 Level) const
{
	if (!LevelLooks.IsValidIndex(Level) || LevelLooks[Level].IsNull())
	{
		return nullptr;
	}
	return LevelLooks[Level].LoadSynchronous();
}
