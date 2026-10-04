#include "BackroomsSound.h"
#include "BackroomsSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "port/mixer.h"

void UBackroomsSound::Start(UWorld* World)
{
	Stop();
	Scene = World;
	Mixer = new SoundMixer();
	Mixer->load();

	Stream = NewObject<USoundWaveProcedural>(this);
	Stream->SetSampleRate(SAMPLE_RATE);
	Stream->NumChannels = 2;
	Stream->Duration = INDEFINITELY_LOOPING_DURATION;
	Stream->SoundGroup = SOUNDGROUP_Default;
	Stream->bLooping = false;
	StreamPlayer = UGameplayStatics::CreateSound2D(World, Stream, 1.0f, 1.0f, 0.0f, nullptr, false, false);
	if (StreamPlayer)
	{
		StreamPlayer->Play();
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("Backrooms: no audio device, so the game is silent"));
		return;
	}
	// Loaded now, so the first splash does not load from disk mid-frame.
	for (int32 S = 0; S < SFX_COUNT; S++)
	{
		for (int32 V = 0; V < clipSpec((Sfx)S).variants; V++)
		{
			const std::string Path = clipRecording((Sfx)S, V);
			if (!Path.empty())
			{
				Player(Path.c_str());
			}
		}
	}
	Player(UNDERWATER_RECORDING);
	Player(PARTY_RECORDING);
}

void UBackroomsSound::Stop()
{
	if (StreamPlayer)
	{
		StreamPlayer->Stop();
	}
	for (const TPair<FString, TObjectPtr<UAudioComponent>>& It : Players)
	{
		if (It.Value)
		{
			It.Value->Stop();
		}
	}
	StreamPlayer = nullptr;
	Stream = nullptr;
	Players.Empty();
	delete Mixer;
	Mixer = nullptr;
}

void UBackroomsSound::BeginDestroy()
{
	delete Mixer;
	Mixer = nullptr;
	Super::BeginDestroy();
}

void UBackroomsSound::Play(const std::vector<AudioEvent>& Events)
{
	if (!Mixer)
	{
		return;
	}
	Mixer->apply(Events);
	for (const RecordedPlay& Rec : Mixer->recorded)
	{
		const std::string Path = clipRecording(Rec.sfx, Rec.variant);
		if (UAudioComponent* Comp = Player(Path.c_str()))
		{
			Comp->SetPitchMultiplier(Rec.pitch);
			Comp->SetVolumeMultiplier(Rec.volume * CENTRE_GAIN);
			Comp->Play();
		}
	}
	Mixer->recorded.clear();
}

void UBackroomsSound::HoldPaused(const AmbienceMix& Mix, const LoopCue& Cue)
{
	if (Mixer)
	{
		Mixer->holdPaused(Mix, Cue);
	}
}

void UBackroomsSound::Feed()
{
	if (!Mixer)
	{
		return;
	}
	FeedLoop(UNDERWATER_RECORDING, Mixer->loops.underwater, 1.0f);
	FeedLoop(PARTY_RECORDING, Mixer->loops.party, Mixer->loops.partyPitch);
	if (!StreamPlayer)
	{
		return;
	}
	// Stereo int16: four bytes a frame.
	const int32 Queued = Stream->GetAvailableAudioByteCount() / 4;
	const int32 Wanted = FMath::CeilToInt(GetDefault<UBackroomsSettings>()->SoundLatency * SAMPLE_RATE);
	if (Queued >= Wanted)
	{
		return;
	}
	const int32 Count = Wanted - Queued;
	Frames.SetNumUninitialized(Count * 2);
	Mixer->render(Frames.GetData(), Count);
	Stream->QueueAudio(reinterpret_cast<const uint8*>(Frames.GetData()), Count * 4);
}

// The importer keeps assets/sounds' folders: "sounds/water/swim_1.ogg" is
// <SoundFolder>/water/swim_1.
USoundWave* UBackroomsSound::Recording(const char* Path)
{
	FString Rel = UTF8_TO_TCHAR(Path);
	Rel.RemoveFromStart(TEXT("sounds/"));
	const FString Name = FPaths::GetBaseFilename(Rel);
	const FString Folder = GetDefault<UBackroomsSettings>()->SoundFolder / FPaths::GetPath(Rel);
	const FString Object = Folder / Name + TEXT(".") + Name;
	USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Object, nullptr, LOAD_NoWarn);
	if (!Wave && !Missing.Contains(Object))
	{
		Missing.Add(Object);
		UE_LOG(LogTemp, Warning, TEXT("Backrooms: no sound at %s; open the editor to import assets/sounds"), *Object);
	}
	return Wave;
}

UAudioComponent* UBackroomsSound::Player(const char* Path)
{
	const FString Key = UTF8_TO_TCHAR(Path);
	if (TObjectPtr<UAudioComponent>* Found = Players.Find(Key))
	{
		return *Found;
	}
	UAudioComponent* Comp = nullptr;
	if (StreamPlayer)
	{
		if (USoundWave* Wave = Recording(Path))
		{
			Comp = UGameplayStatics::CreateSound2D(Scene.Get(), Wave, 1.0f, 1.0f, 0.0f, nullptr, false, false);
		}
	}
	Players.Add(Key, Comp);
	return Comp;
}

// The Web build plays a loop while its level is above 0.005 and stops it
// below, so a stopped loop starts again from the top.
void UBackroomsSound::FeedLoop(const char* Path, float Volume, float Pitch)
{
	UAudioComponent* Comp = Player(Path);
	if (!Comp)
	{
		return;
	}
	if (Volume <= 0.005f)
	{
		if (Comp->IsPlaying())
		{
			Comp->Stop();
		}
		return;
	}
	Comp->SetVolumeMultiplier(Volume * CENTRE_GAIN);
	Comp->SetPitchMultiplier(Pitch);
	if (!Comp->IsPlaying())
	{
		Comp->Play();
	}
}
