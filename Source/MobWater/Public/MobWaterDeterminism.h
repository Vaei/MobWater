// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

class UMobWaterSubsystem;

/**
 * Records the surface a machine computed, so two machines can be asked whether they computed the same one.
 *
 * The whole plugin rests on one claim: the surface is a pure function of position and time, so a
 * server and a client handed the same instant answer exactly the same thing and buoyancy on the two
 * cannot part. Everything else that is asserted here is asserted on one machine, and one machine
 * cannot fail that claim - it is only about two.
 *
 * So this runs on three: a dedicated server and two clients, each writing what it computed to its own
 * file, and a comparator that fails if any row differs. What makes it worth building rather than
 * inspecting the code is that the failures it catches are not visible in the code at all. A float
 * folded in one order on one machine, a wave set published a frame later, a body whose transform
 * arrived late - each leaves the surface subtly different on one machine and identical in every
 * source file.
 *
 * The clock is synthetic while this is on, and that is deliberate rather than a shortcut. A real
 * clock differs between machines by design, so a comparison against one would be measuring the clock
 * rather than the water, and would fail for a reason nobody could fix. Handed the same instant, the
 * two sides have nothing left to disagree about but the arithmetic - which is the claim.
 *
 * Then it is run again with the clock deliberately stepped, by whole loop periods and by a large
 * arbitrary offset, and the same rows are demanded. That is the run that proves the fold: a wrap that
 * is out by anything at all leaves a machine an hour into a session drawing a different sea from one
 * that just started, which is not a bug anybody finds by playing.
 *
 * Off unless mob.Water.Determinism is set. It spawns a body of water and writes a file per frame, so
 * it is a harness rather than a diagnostic and has no business running when it was not asked for.
 */
struct MOBWATER_API FMobWaterDeterminism
{
	/** Whether the harness has been asked for at all. Checked before anything is built. */
	static bool IsEnabled();

	/**
	 * Advances the synthetic clock and makes sure there is water to ask about.
	 *
	 * Called before the subsystem reads its clock, because the clock it reads is this one.
	 */
	void BeginFrame(UMobWaterSubsystem& Subsystem);

	/**
	 * Writes one row: the clock, and the surface at each fixed point.
	 *
	 * Called immediately after the clock is folded and before anything else the subsystem does, so
	 * what lands in the file is the wave model and nothing that happened to run in the same frame.
	 */
	void Record(UMobWaterSubsystem& Subsystem);

	/** The points every machine is asked about, in world units. */
	static TArrayView<const FVector2D> GetPoints();

private:
	void Open(UMobWaterSubsystem& Subsystem);
	void Close();

	/** The body this spawns to have something registered to answer. Never replicated: each machine builds its own. */
	TWeakObjectPtr<class AMobWaterPool> Body;

	TUniquePtr<FArchive> File;

	/** How many rows are written. The synthetic clock is this times the step, plus the offset. */
	int32 Frame = 0;

	bool bStarted = false;
	bool bFinished = false;
};
