// Automation tests: core's contract, the sim's recorded traces, the coordinate
// conversion, the greybox and the sound mixer, run by this engine's compiler
// and libm. Session
// Frontend > Automation, filter "Backrooms", or from the command line:
//   UnrealEditor-Cmd BackroomsGame.uproject -ExecCmds="Automation RunTests Backrooms; Quit" -unattended -nullrhi
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "BackroomsCoords.h"
#include "contract_lib.h"
#include "core/level_rules.h"
#include "port/greybox.h"
#include "port/mixer.h"
#include "sim/trace.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
// The repository: the project is its unreal/ folder.
FString RepoDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
}

// One automation message per line of a report; lines starting FAIL are errors.
void Report(FAutomationTestBase& Test, const std::string& Text)
{
	TArray<FString> Lines;
	FString(UTF8_TO_TCHAR(Text.c_str())).ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("FAIL")))
		{
			Test.AddError(Line);
		}
		else
		{
			Test.AddInfo(Line);
		}
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackroomsContractTest, "Backrooms.Core.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBackroomsContractTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(RepoDir(), TEXT("tests/golden"));
	const std::vector<contract::Golden> Golden = contract::produce();
	std::string Text;
	const int Bad = contract::compare(TCHAR_TO_UTF8(*Dir), Golden, Text);
	Report(*this, Text);
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackroomsReplayTest, "Backrooms.Sim.Replay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBackroomsReplayTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(RepoDir(), TEXT("tests/traces"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.trace")), true, false);
	if (Files.Num() == 0)
	{
		AddError(FString::Printf(TEXT("no traces in %s"), *Dir));
		return false;
	}
	bool bAllOk = true;
	for (const FString& File : Files)
	{
		const FString Path = FPaths::Combine(Dir, File);
		const ReplayResult Result = replayTrace(TCHAR_TO_UTF8(*Path));
		const FString Line = FString::Printf(TEXT("%s %s: %s"), Result.ok ? TEXT("ok  ") : TEXT("FAIL"), *File,
			UTF8_TO_TCHAR(Result.report.c_str()));
		Report(*this, TCHAR_TO_UTF8(*Line));
		bAllOk = bAllOk && Result.ok;
	}
	return bAllOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackroomsCoordsTest, "Backrooms.Port.Coords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBackroomsCoordsTest::RunTest(const FString& Parameters)
{
	const Vec3 P = { 1.5f, 2.25f, -3.0f };
	TestEqual(TEXT("metres to centimetres, y and z swapped"), BackroomsCoords::ToUnreal(P), FVector(150.0, -300.0, 225.0));
	const Vec3 Back = BackroomsCoords::FromUnreal(BackroomsCoords::ToUnreal(P));
	TestTrue(TEXT("round trip"), Back.x == P.x && Back.y == P.y && Back.z == P.z);
	// Not mirrored: at every yaw, core's forward and right (the sim's f2 and r2)
	// land on Unreal's forward and right for the converted rotator.
	for (int32 Step = 0; Step < 8; Step++)
	{
		const float Yaw = Step * 0.8f - 2.0f;
		const FRotationMatrix M(BackroomsCoords::ToUnrealRotator(Yaw, 0.0f));
		const FVector Forward = BackroomsCoords::ToUnrealDirection({ FMath::Cos(Yaw), 0.0f, FMath::Sin(Yaw) });
		const FVector Right = BackroomsCoords::ToUnrealDirection({ -FMath::Sin(Yaw), 0.0f, FMath::Cos(Yaw) });
		TestTrue(TEXT("forward"), Forward.Equals(M.GetUnitAxis(EAxis::X), 1e-5));
		TestTrue(TEXT("right"), Right.Equals(M.GetUnitAxis(EAxis::Y), 1e-5));
	}
	// Pitch up is up.
	const FVector Up = FRotationMatrix(BackroomsCoords::ToUnrealRotator(0.0f, 0.5f)).GetUnitAxis(EAxis::X);
	TestTrue(TEXT("pitch up"), Up.Z > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackroomsGreyboxTest, "Backrooms.Port.Greybox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBackroomsGreyboxTest::RunTest(const FString& Parameters)
{
	for (int32 Level = 0; Level < NLEVELS; Level++)
	{
		World W;
		W.level = Level;
		W.wallH = LEVEL_RULES[Level].wallH;
		W.storeyH = LEVEL_RULES[Level].storeyH;
		const GreyboxMesh Mesh = greyboxChunk(W, 0, 0);
		TestTrue(FString::Printf(TEXT("level %d has geometry"), Level), Mesh.triangles() > 0);
		for (const GreyboxMesh::Section& Section : Mesh.sections)
		{
			for (uint32_t I : Section.index)
			{
				if (I >= Section.pos.size())
				{
					AddError(FString::Printf(TEXT("level %d: index %u past %d vertices"), Level, I, (int32)Section.pos.size()));
					return false;
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackroomsSoundTest, "Backrooms.Port.Sound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The mixer the game streams (BackroomsSound.cpp), without an audio device:
// silence with nothing playing, a clip that sounds and ends, the pan law's
// sides, a recording handed back to the engine, the bed only once fed.
bool FBackroomsSoundTest::RunTest(const FString& Parameters)
{
	TUniquePtr<SoundMixer> Mixer = MakeUnique<SoundMixer>();
	Mixer->load();
	constexpr int32 Frames = 4096;
	TArray<int16> Out;
	Out.SetNumZeroed(Frames * 2);
	auto Loudness = [&Out](int32 Side) {
		int64 Sum = 0;
		for (int32 I = Side; I < Out.Num(); I += 2)
		{
			Sum += FMath::Abs((int32)Out[I]);
		}
		return Sum;
	};
	auto Event = [](AudioEvent::Kind Kind, Sfx Clip = Sfx::Click, int Variant = 0) {
		AudioEvent E;
		E.kind = Kind;
		E.sfx = Clip;
		E.variant = (uint8_t)Variant;
		return E;
	};

	Mixer->apply({});
	Mixer->render(Out.GetData(), Frames);
	TestEqual(TEXT("silent with nothing playing"), Loudness(0) + Loudness(1), (int64)0);

	std::vector<AudioEvent> Events = { Event(AudioEvent::PLAY, Sfx::Bark, 1) };
	Events.back().atPan(-1.0f);
	Mixer->apply(Events);
	Mixer->render(Out.GetData(), Frames);
	TestTrue(TEXT("a bark sounds"), Loudness(0) > 0);
	TestEqual(TEXT("a bark hard left is silent on the right"), Loudness(1), (int64)0);
	for (int32 I = 0; I < 10; I++)
	{
		Mixer->render(Out.GetData(), Frames);
	}
	TestFalse(TEXT("a bark ends"), Mixer->playing(Sfx::Bark, 1));

	Events = { Event(AudioEvent::PLAY, Sfx::SplashIn, 2) };
	Events.back().atPitch(1.25f);
	Mixer->apply(Events);
	Mixer->render(Out.GetData(), Frames);
	TestEqual(TEXT("a recording makes no sound in the mix"), Loudness(0) + Loudness(1), (int64)0);
	TestTrue(TEXT("a recording is handed back"), Mixer->recorded.size() == 1 && Mixer->recorded[0].sfx == Sfx::SplashIn
		&& Mixer->recorded[0].variant == 2 && Mixer->recorded[0].pitch == 1.25f && Mixer->recorded[0].volume == 1.0f);

	Mixer->apply({ Event(AudioEvent::AMBIENCE) });
	Mixer->render(Out.GetData(), Frames);
	TestTrue(TEXT("the bed sounds once fed"), Loudness(0) > 0 && Loudness(1) > 0);
	Mixer->apply({});
	Mixer->render(Out.GetData(), Frames);
	TestEqual(TEXT("the bed is silent when not fed"), Loudness(0) + Loudness(1), (int64)0);
	return true;
}

#endif
