// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Trace/Config.h"
#include "Trace/Trace.h"

struct FMobWaterInfo;
struct FMobWaterWaveParams;

class UMobWaterComponent;
class UMobWaterSubsystem;

#if !defined(MOB_WATER_TRACE_ENABLED)
	#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
		#define MOB_WATER_TRACE_ENABLED 1
	#else
		#define MOB_WATER_TRACE_ENABLED 0
	#endif
#endif

#if MOB_WATER_TRACE_ENABLED
UE_TRACE_CHANNEL_EXTERN(MobWaterChannel, MOBWATER_API);
#endif

/**
 * What a machine believed about the water, frame by frame, so two recordings can be scrubbed together.
 *
 * A client and a server can only disagree about this water in three places, because the surface is a
 * pure function: the clock, the wave set that was published, or a body's own transform. That is a few
 * scalars a frame, which is what makes a timeline worth recording rather than a stack of logs.
 *
 * Queries are recorded too, and that is the part that earns it. Most of the time a ship that desyncs
 * has not found different water - the two machines asked about different places, because the ship
 * itself was somewhere different. Recording only the answer cannot tell those apart, and they are
 * unrelated bugs: one is this plugin's clock, the other is the ship's own replication and has nothing
 * to do with water. So a query records where it was asked as well as what it answered, attributed to
 * whoever asked, which is how a character standing on a boat shows up as the boat being wrong rather
 * than as the water being wrong under its feet.
 *
 * What it must not claim: only the analytic surface is comparable. Ripples, wakes and stamped foam
 * live in a render target a dedicated server does not have, so they are not synchronised by
 * construction and never will be. Nothing about them is recorded here, deliberately - a track showing
 * them beside the shared values would send someone hunting a desync in a system that was never in
 * sync.
 *
 * Behind a trace channel that is off by default, because a pontoon array is several queries per actor
 * per frame and recording them unconditionally would cost more than the buoyancy does.
 */
struct MOBWATER_API FMobWaterTrace
{
	/** Whether anything is listening. Checked before a query builds anything to say. */
	static bool IsEnabled();

	/** The clock, the wave set and how many bodies there are, once a frame. */
	static void State(const UMobWaterSubsystem& Subsystem, int32 NetMode, int32 BodyCount);

	/** Where a body is and how big, once a frame each. A body that moved is one of the three suspects. */
	static void Body(const UMobWaterComponent& Water);

	/**
	 * One query: who asked, where, and what they were told.
	 *
	 * Behind its own switch as well as the channel. The shared state is a handful of values a frame
	 * and is worth having whenever anything is recording; queries are unbounded, and a fleet of ships
	 * would fill a trace with them.
	 */
	static void Query(const UObject* Querier, const UMobWaterComponent* Body, const FVector& Location,
		const FMobWaterInfo& Info);

	/**
	 * A number that changes when the wave set does.
	 *
	 * Recorded rather than the set itself: eight waves of six floats every frame is most of what a
	 * trace would hold, and the only question ever asked of it is whether the two machines were
	 * running the same one.
	 */
	static uint32 HashWaves(const FMobWaterWaveParams& Params);
};
