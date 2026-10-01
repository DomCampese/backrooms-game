#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include <vector>
#include "BackroomsSound.generated.h"

struct AmbienceMix;
struct AudioEvent;
struct LoopCue;
struct SoundMixer;
class UAudioComponent;
class USoundWave;
class USoundWaveProcedural;

// The sim's sound in Unreal (M6). The synthesized clips, the tape voice and
// the ambience bed are mixed in software (src/port/mixer.h) and streamed
// through one procedural wave, so they sound as the raylib build plays them.
// The recordings in assets/sounds, which the editor imports
// (backrooms_import.py), play as sound waves on components of their own.
UCLASS()
class UBackroomsSound : public UObject
{
	GENERATED_BODY()

public:
	// Synthesizes the clips and starts the stream. Without an audio device (a
	// headless test, -nosound) the events are still mixed and dropped.
	void Start(UWorld* World);
	void Stop();
	// One frame's events, in the order the sim emitted them.
	void Play(const std::vector<AudioEvent>& Events);
	// A paused frame, after Play: the bed goes on without the growl, hiss and
	// whispers, and the loops hold.
	void HoldPaused(const AmbienceMix& Mix, const LoopCue& Cue);
	// Tops the stream up to the settings' latency and sets the loops' levels.
	// Once a frame, after Play and HoldPaused.
	void Feed();

	virtual void BeginDestroy() override;

private:
	// The imported wave for a recording such as "sounds/water/swim_1.ogg", or
	// null, said once in the log.
	USoundWave* Recording(const char* Path);
	// The component for a recording, made by Start: one per recording, so a
	// play restarts it as raylib's PlaySound does. Null without a device or
	// the imported wave.
	UAudioComponent* Player(const char* Path);
	void FeedLoop(const char* Path, float Volume, float Pitch);

	SoundMixer* Mixer = nullptr;
	TWeakObjectPtr<UWorld> Scene;
	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> Stream;
	UPROPERTY()
	TObjectPtr<UAudioComponent> StreamPlayer;
	UPROPERTY()
	TMap<FString, TObjectPtr<UAudioComponent>> Players;
	TSet<FString> Missing;
	TArray<int16> Frames;
};
