// Copyright (c) Jared Taylor

#include "MobWaterTrace.h"

#include "MobWaterComponent.h"
#include "MobWaterInfo.h"
#include "MobWaterSubsystem.h"
#include "MobWaterWaves.h"

#if MOB_WATER_TRACE_ENABLED
#include "ObjectTrace.h"
#include "Trace/Trace.inl"
#endif

namespace MobWaterTraceCVars
{
	static int32 Queries = 0;
	static FAutoConsoleVariableRef CVarQueries(
		TEXT("mob.Water.TraceQueries"),
		Queries,
		TEXT("Records every water query, attributed to whoever asked.\n")
		TEXT("Off by default and separately from the channel: a pontoon array is several queries per\n")
		TEXT("actor per frame, and recording them unconditionally costs more than the buoyancy does."),
		ECVF_Default);
}

#if MOB_WATER_TRACE_ENABLED

UE_TRACE_CHANNEL_DEFINE(MobWaterChannel);

UE_TRACE_EVENT_BEGIN(MobWater, State)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WorldId)
	UE_TRACE_EVENT_FIELD(double, RawTime)
	UE_TRACE_EVENT_FIELD(float, WaterTime)
	UE_TRACE_EVENT_FIELD(uint32, WaveHash)
	UE_TRACE_EVENT_FIELD(int32, NetMode)
	UE_TRACE_EVENT_FIELD(int32, BodyCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MobWater, Body)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, BodyId)
	UE_TRACE_EVENT_FIELD(uint64, OwnerId)
	UE_TRACE_EVENT_FIELD(double, X)
	UE_TRACE_EVENT_FIELD(double, Y)
	UE_TRACE_EVENT_FIELD(double, Z)
	UE_TRACE_EVENT_FIELD(float, Yaw)
	UE_TRACE_EVENT_FIELD(float, ExtentX)
	UE_TRACE_EVENT_FIELD(float, ExtentY)
	UE_TRACE_EVENT_FIELD(uint8, Shape)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MobWater, Query)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, QuerierId)
	UE_TRACE_EVENT_FIELD(uint64, BodyId)
	UE_TRACE_EVENT_FIELD(double, X)
	UE_TRACE_EVENT_FIELD(double, Y)
	UE_TRACE_EVENT_FIELD(double, Z)
	UE_TRACE_EVENT_FIELD(float, SurfaceZ)
	UE_TRACE_EVENT_FIELD(float, ImmersionDepth)
	UE_TRACE_EVENT_FIELD(bool, Valid)
UE_TRACE_EVENT_END()

namespace
{
	/** An object's trace id, and zero for anything the trace has never been told about. */
	static uint64 MobWaterObjectId(const UObject* Object)
	{
#if OBJECT_TRACE_ENABLED
		if (Object)
		{
			// Traced here rather than assumed. An id the recording has no object for is a track with
			// nothing to hang off, which shows as the querier having vanished rather than as it never
			// having been recorded.
			TRACE_OBJECT(Object);
			return FObjectTrace::GetObjectId(Object);
		}
#endif
		return 0;
	}
}

#endif

bool FMobWaterTrace::IsEnabled()
{
#if MOB_WATER_TRACE_ENABLED
	return UE_TRACE_CHANNELEXPR_IS_ENABLED(MobWaterChannel);
#else
	return false;
#endif
}

uint32 FMobWaterTrace::HashWaves(const FMobWaterWaveParams& Params)
{
	uint32 Hash = GetTypeHash(Params.Waves.Num());

	Hash = HashCombine(Hash, GetTypeHash(Params.AmplitudeScale));
	Hash = HashCombine(Hash, GetTypeHash(Params.SpeedScale));
	Hash = HashCombine(Hash, GetTypeHash(Params.ChoppinessScale));

	for (const FMobGerstnerWave& Wave : Params.Waves)
	{
		Hash = HashCombine(Hash, GetTypeHash(Wave.Direction.X));
		Hash = HashCombine(Hash, GetTypeHash(Wave.Direction.Y));
		Hash = HashCombine(Hash, GetTypeHash(Wave.Wavelength));
		Hash = HashCombine(Hash, GetTypeHash(Wave.Amplitude));
		Hash = HashCombine(Hash, GetTypeHash(Wave.Steepness));
		Hash = HashCombine(Hash, GetTypeHash(Wave.PhaseOffset));
	}

	return Hash;
}

void FMobWaterTrace::State(const UMobWaterSubsystem& Subsystem, int32 NetMode, int32 BodyCount)
{
#if MOB_WATER_TRACE_ENABLED
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(MobWaterChannel))
	{
		return;
	}

	UE_TRACE_LOG(MobWater, State, MobWaterChannel)
		<< State.Cycle(FPlatformTime::Cycles64())
		<< State.WorldId(MobWaterObjectId(Subsystem.GetWorld()))
		<< State.RawTime(Subsystem.GetRawWaterTime())
		<< State.WaterTime(Subsystem.GetWaterTime())
		<< State.WaveHash(HashWaves(Subsystem.GetDefaultWaves()))
		<< State.NetMode(NetMode)
		<< State.BodyCount(BodyCount);
#endif
}

void FMobWaterTrace::Body(const UMobWaterComponent& Water)
{
#if MOB_WATER_TRACE_ENABLED
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(MobWaterChannel))
	{
		return;
	}

	const FTransform Transform = Water.GetComponentTransform();
	const FVector Location = Transform.GetLocation();

	// The owner as well as the component, because the Rewind Debugger is driven by what is selected in
	// the level and what anybody selects is the actor. Keyed to the component alone, an ocean picked in
	// the viewport finds no track and reads as nothing having been recorded.
	UE_TRACE_LOG(MobWater, Body, MobWaterChannel)
		<< Body.Cycle(FPlatformTime::Cycles64())
		<< Body.BodyId(MobWaterObjectId(&Water))
		<< Body.OwnerId(MobWaterObjectId(Water.GetOwner()))
		<< Body.X(Location.X)
		<< Body.Y(Location.Y)
		<< Body.Z(Location.Z)
		<< Body.Yaw(static_cast<float>(Transform.Rotator().Yaw))
		<< Body.ExtentX(static_cast<float>(Water.Extent.X))
		<< Body.ExtentY(static_cast<float>(Water.Extent.Y))
		<< Body.Shape(static_cast<uint8>(Water.Shape));
#endif
}

void FMobWaterTrace::Query(const UObject* Querier, const UMobWaterComponent* WaterBody,
	const FVector& Location, const FMobWaterInfo& Info)
{
#if MOB_WATER_TRACE_ENABLED
	if (MobWaterTraceCVars::Queries == 0 || !UE_TRACE_CHANNELEXPR_IS_ENABLED(MobWaterChannel))
	{
		return;
	}

	UE_TRACE_LOG(MobWater, Query, MobWaterChannel)
		<< Query.Cycle(FPlatformTime::Cycles64())
		<< Query.QuerierId(MobWaterObjectId(Querier))
		<< Query.BodyId(MobWaterObjectId(WaterBody))
		<< Query.X(Location.X)
		<< Query.Y(Location.Y)
		<< Query.Z(Location.Z)
		<< Query.SurfaceZ(Info.SurfaceZ)
		<< Query.ImmersionDepth(Info.ImmersionDepth)
		<< Query.Valid(Info.bValid);
#endif
}
