// Copyright (c) Jared Taylor

#include "MobWaterDeterminism.h"

#include "MobWaterComponent.h"
#include "MobWaterInfo.h"
#include "MobWaterModule.h"
#include "MobWaterPoolActor.h"
#include "MobWaterSubsystem.h"
#include "MobWaterWavePreset.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

namespace MobWaterDeterminismCVars
{
	static int32 Enabled = 0;
	static FAutoConsoleVariableRef CVarEnabled(
		TEXT("mob.Water.Determinism"),
		Enabled,
		TEXT("Records the surface this machine computed, for comparison against another's.\n")
		TEXT("Spawns its own body of water and drives the clock itself, so it is a harness rather\n")
		TEXT("than a diagnostic. 0 off, 1 on."),
		ECVF_Default);

	static float Offset = 0.f;
	static FAutoConsoleVariableRef CVarOffset(
		TEXT("mob.Water.DeterminismOffset"),
		Offset,
		TEXT("Seconds the synthetic clock starts at. Stepping this between two runs and demanding\n")
		TEXT("the same rows is what proves the fold at the loop period is right."),
		ECVF_Default);

	static float Step = 1.f / 64.f;
	static FAutoConsoleVariableRef CVarStep(
		TEXT("mob.Water.DeterminismStep"),
		Step,
		TEXT("Seconds the synthetic clock advances by each recorded frame.\n")
		TEXT("A sixty fourth rather than a sixtieth, because it is exactly representable. A step that\n")
		TEXT("is not means a machine started an hour in rounds its own clock differently from one that\n")
		TEXT("just started, and the comparison then fails on the precision of the number it was given\n")
		TEXT("rather than on the fold it is testing."),
		ECVF_Default);

	static int32 Frames = 600;
	static FAutoConsoleVariableRef CVarFrames(
		TEXT("mob.Water.DeterminismFrames"),
		Frames,
		TEXT("How many frames to record before stopping."),
		ECVF_Default);

	static int32 Quit = 1;
	static FAutoConsoleVariableRef CVarQuit(
		TEXT("mob.Water.DeterminismQuit"),
		Quit,
		TEXT("Whether to close the process once the recording is complete. On, because a harness\n")
		TEXT("that has to be shut down by hand is one nobody runs unattended."),
		ECVF_Default);

	static FString File;
	static FAutoConsoleVariableRef CVarFile(
		TEXT("mob.Water.DeterminismFile"),
		File,
		TEXT("Where to write the recording. Empty names it after the net mode and the process id."),
		ECVF_Default);
}

namespace
{
	/**
	 * The points every machine is asked about.
	 *
	 * Spread rather than gridded, and none of them on a round number. A grid lands every point on the
	 * same phase of a wave whose length divides the spacing, and two machines that both had that wave
	 * wrong would agree at every one of them.
	 */
	static const FVector2D MobWaterDeterminismPoints[] =
	{
		FVector2D(0.0, 0.0),
		FVector2D(137.5, -412.25),
		FVector2D(-1903.75, 884.5),
		FVector2D(4471.0, 4471.0),
		FVector2D(-7318.5, 2205.25),
		FVector2D(9214.25, -6087.75),
		FVector2D(-12005.0, -11117.5),
		FVector2D(15733.75, 819.0),
	};

	/** What this machine is, as the comparator reads it back. */
	static FString MobWaterNetModeName(const UWorld* World)
	{
		switch (World ? World->GetNetMode() : NM_Standalone)
		{
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Standalone");
		}
	}
}

bool FMobWaterDeterminism::IsEnabled()
{
	return MobWaterDeterminismCVars::Enabled != 0;
}

TArrayView<const FVector2D> FMobWaterDeterminism::GetPoints()
{
	return MakeArrayView(MobWaterDeterminismPoints, UE_ARRAY_COUNT(MobWaterDeterminismPoints));
}

void FMobWaterDeterminism::BeginFrame(UMobWaterSubsystem& Subsystem)
{
	if (bFinished)
	{
		return;
	}

	if (!bStarted)
	{
		Open(Subsystem);
		bStarted = true;
	}

	// The clock is this and nothing else while the harness is running. Bound rather than written
	// straight into the subsystem so it goes through the same path a project's own synchronised clock
	// would - a harness that bypassed the time source would be proving something about a route
	// nothing uses.
	FMobWaterTimeSource Source;
	Source.BindLambda([this]()
	{
		return static_cast<double>(MobWaterDeterminismCVars::Offset)
			+ static_cast<double>(MobWaterDeterminismCVars::Step) * Frame;
	});

	Subsystem.SetTimeSource(Source);

	++Frame;
}

void FMobWaterDeterminism::Open(UMobWaterSubsystem& Subsystem)
{
	UWorld* World = Subsystem.GetWorld();
	if (!World)
	{
		return;
	}

	// Spawned rather than placed in a map, because a map is content that has to be authored, kept and
	// loaded the same way on three machines - and every one of those is a way for the comparison to
	// fail for a reason that has nothing to do with the water. A body built here is identical on all
	// three by construction.
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMobWaterPool* Pool = World->SpawnActor<AMobWaterPool>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Pool)
	{
		UMobWaterComponent* Water = Pool->GetWaterComponent();

		// Large enough that every sampled point is well inside it, so none of them lands in the shore
		// fade - which is correct arithmetic on both machines and would still narrow what the
		// comparison covers to the part of the wave set that survives being flattened.
		Water->Extent = FVector2D(40000.0, 40000.0);
		Water->Depth = 4000.f;
		Water->ShoreFadeDistance = 200.f;

		// Named rather than left to the settings. What a project has configured is not something the
		// comparison should depend on, and two machines with different configs is a failure that
		// would present as the arithmetic having parted.
		Water->WavePreset = LoadObject<UMobWaterWavePreset>(nullptr, TEXT("/MobWater/Waves/WP_MobWater_Ocean"));

		// Both off unity, so the pair that share a data slot are in the path being compared. At one
		// each they pack and unpack to themselves and a packing read the wrong way round would still
		// answer the right surface.
		Water->WaveAmplitude = 0.37f;
		Water->WaveSpeed = 0.62f;

		Water->MarkRenderStateDirty();
		Body = Pool;
	}

	FString Path = MobWaterDeterminismCVars::File;
	if (Path.IsEmpty())
	{
		Path = FPaths::ProjectSavedDir() / TEXT("MobWater") / FString::Printf(
			TEXT("Determinism_%s_%d.csv"), *MobWaterNetModeName(World), FPlatformProcess::GetCurrentProcessId());
	}

	File.Reset(IFileManager::Get().CreateFileWriter(*Path));
	if (!File)
	{
		UE_LOG(LogMobWater, Error, TEXT("MobWater: the determinism harness could not write %s."), *Path);
		bFinished = true;
		return;
	}

	FString Header = TEXT("Frame,RawTime,WaterTime");
	for (int32 Index = 0; Index < GetPoints().Num(); ++Index)
	{
		Header += FString::Printf(TEXT(",Z%d"), Index);
	}
	Header += TEXT("\n");

	const FTCHARToUTF8 Converted(*Header);
	File->Serialize(const_cast<ANSICHAR*>(Converted.Get()), Converted.Length());

	UE_LOG(LogMobWater, Display,
		TEXT("MobWater: recording %d frames of %s from %.3fs in steps of %.6fs to %s"),
		MobWaterDeterminismCVars::Frames, *MobWaterNetModeName(World),
		MobWaterDeterminismCVars::Offset, MobWaterDeterminismCVars::Step, *Path);
}

void FMobWaterDeterminism::Record(UMobWaterSubsystem& Subsystem)
{
	if (bFinished || !File)
	{
		return;
	}

	// Seventeen significant figures, which is a double written out and read back unchanged. Fewer and
	// the comparator would be asserting that two machines agree to however many digits were printed,
	// which is a weaker claim than the one being made and would hide exactly the small divergence
	// that grows.
	FString Row = FString::Printf(TEXT("%d,%.17g,%.17g"),
		Frame - 1, Subsystem.GetRawWaterTime(), Subsystem.GetWaterTime());

	const UMobWaterComponent* Water = Body.IsValid() ? Body->GetWaterComponent() : nullptr;

	for (const FVector2D& Point : GetPoints())
	{
		const FMobWaterInfo Info = Water
			? Water->GetWaterInfoAtLocation(FVector(Point.X, Point.Y, 0.0))
			: FMobWaterInfo();

		// Not guarded on bValid. A point that has stopped answering is exactly the divergence worth
		// catching, and skipping it would leave the two files the same length and the same values
		// with a body missing on one of them.
		Row += FString::Printf(TEXT(",%.17g"), Info.bValid ? Info.SurfaceZ : 0.f);
	}

	Row += TEXT("\n");

	const FTCHARToUTF8 Converted(*Row);
	File->Serialize(const_cast<ANSICHAR*>(Converted.Get()), Converted.Length());

	if (Frame >= FMath::Max(MobWaterDeterminismCVars::Frames, 1))
	{
		Close();
	}
}

void FMobWaterDeterminism::Close()
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;

	if (File)
	{
		File->Close();
		File.Reset();
	}

	UE_LOG(LogMobWater, Display, TEXT("MobWater: determinism recording complete after %d frames."), Frame);

	if (MobWaterDeterminismCVars::Quit != 0)
	{
		FPlatformMisc::RequestExit(false);
	}
}
