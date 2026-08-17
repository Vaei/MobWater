// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "IRewindDebugger.h"
#include "IRewindDebuggerTrackCreator.h"
#include "RewindDebuggerTrack.h"
#include "SCurveTimelineView.h"
#include "Textures/SlateIcon.h"

namespace MobWaterRewind
{

/**
 * Which number a curve track is drawing.
 *
 * The shared state and the queries are one list, because they are drawn the same way and the only
 * thing that differs is which timeline is read - and a second class to say that would be the same
 * forty lines twice.
 */
enum class EMobWaterCurve : uint8
{
	// The shared state, hung off the world.
	WaterTime,
	RawTime,
	WaveHash,
	BodyCount,

	// A body of water, hung off the component and off the actor carrying it.
	BodyX,
	BodyY,
	BodyZ,
	BodyYaw,

	// A query, hung off whoever made it.
	SurfaceZ,
	ImmersionDepth,
	QueryX,
	QueryY,
	QueryZ,
};

/** One number over time. */
class FMobWaterCurveTrack : public RewindDebugger::FRewindDebuggerTrack
{
public:
	FMobWaterCurveTrack(uint64 InObjectId, EMobWaterCurve InCurve);

private:
	//~ Begin FRewindDebuggerTrack Interface
	virtual bool UpdateInternal() override;
	virtual TSharedPtr<SWidget> GetTimelineViewInternal() override;
	virtual FName GetNameInternal() const override { return "MobWaterCurve"; }
	virtual FText GetDisplayNameInternal() const override;
	virtual uint64 GetObjectIdInternal() const override { return ObjectId; }
	//~ End FRewindDebuggerTrack Interface

	TSharedPtr<SCurveTimelineView::FTimelineCurveData> GetCurveData() const;

	uint64 ObjectId;
	EMobWaterCurve Curve;

	mutable TSharedPtr<SCurveTimelineView::FTimelineCurveData> CurveData;
	mutable int32 UpdatesRequested = 0;
};

/**
 * What a machine believed about the water, hung off the world it believed it about.
 *
 * Two recordings scrubbed together part on the frame one of these lines does. There are only three
 * of them because there are only three things that can differ: the clock, the wave set, and how many
 * bodies were registered - everything else about the surface is a pure function of those.
 */
class FMobWaterStateTrack : public RewindDebugger::FRewindDebuggerTrack
{
public:
	explicit FMobWaterStateTrack(uint64 InObjectId);

private:
	//~ Begin FRewindDebuggerTrack Interface
	virtual bool UpdateInternal() override;
	virtual FSlateIcon GetIconInternal() override { return Icon; }
	virtual FName GetNameInternal() const override { return "MobWater"; }
	virtual FText GetDisplayNameInternal() const override;
	virtual uint64 GetObjectIdInternal() const override { return ObjectId; }
	virtual TConstArrayView<TSharedPtr<FRewindDebuggerTrack>> GetChildrenInternal(
		TArray<TSharedPtr<FRewindDebuggerTrack>>& OutTracks) const override;
	//~ End FRewindDebuggerTrack Interface

	FSlateIcon Icon;
	uint64 ObjectId;

	TArray<TSharedPtr<FRewindDebuggerTrack>> Children;
};

/**
 * Where a body of water was, hung off the body and off the actor carrying it.
 *
 * The third of the three things two machines can disagree about, and the one that is usually somebody
 * else's bug: a ship that replicated late moves the water it carries, and a recording that held the
 * clock and the wave set but not this sends someone looking for the fault in the wrong plugin.
 */
class FMobWaterBodyTrack : public RewindDebugger::FRewindDebuggerTrack
{
public:
	explicit FMobWaterBodyTrack(uint64 InObjectId);

private:
	//~ Begin FRewindDebuggerTrack Interface
	virtual bool UpdateInternal() override;
	virtual FSlateIcon GetIconInternal() override { return Icon; }
	virtual FName GetNameInternal() const override { return "MobWaterBody"; }
	virtual FText GetDisplayNameInternal() const override;
	virtual uint64 GetObjectIdInternal() const override { return ObjectId; }
	virtual TConstArrayView<TSharedPtr<FRewindDebuggerTrack>> GetChildrenInternal(
		TArray<TSharedPtr<FRewindDebuggerTrack>>& OutTracks) const override;
	//~ End FRewindDebuggerTrack Interface

	FSlateIcon Icon;
	uint64 ObjectId;

	TArray<TSharedPtr<FRewindDebuggerTrack>> Children;
};

/**
 * What this object asked the water, and what it was told.
 *
 * The location is here as well as the answer, and that is the whole reason this track exists. Most of
 * the time a ship that desyncs has not found different water - the two machines asked about different
 * places, because the ship itself was somewhere different. Those are unrelated bugs and a recording
 * of answers alone cannot tell them apart.
 */
class FMobWaterQueryTrack : public RewindDebugger::FRewindDebuggerTrack
{
public:
	explicit FMobWaterQueryTrack(uint64 InObjectId);

private:
	//~ Begin FRewindDebuggerTrack Interface
	virtual bool UpdateInternal() override;
	virtual FSlateIcon GetIconInternal() override { return Icon; }
	virtual FName GetNameInternal() const override { return "MobWaterQueries"; }
	virtual FText GetDisplayNameInternal() const override;
	virtual uint64 GetObjectIdInternal() const override { return ObjectId; }
	virtual TConstArrayView<TSharedPtr<FRewindDebuggerTrack>> GetChildrenInternal(
		TArray<TSharedPtr<FRewindDebuggerTrack>>& OutTracks) const override;
	//~ End FRewindDebuggerTrack Interface

	FSlateIcon Icon;
	uint64 ObjectId;

	TArray<TSharedPtr<FRewindDebuggerTrack>> Children;
};

class FMobWaterStateTrackCreator : public RewindDebugger::IRewindDebuggerTrackCreator
{
	virtual FName GetTargetTypeNameInternal() const override;
	virtual FName GetNameInternal() const override { return "MobWater"; }
	virtual void GetTrackTypesInternal(TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> CreateTrackInternal(
		const RewindDebugger::FObjectId& InObjectId) const override;
	virtual bool HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const override;
};

class FMobWaterBodyTrackCreator : public RewindDebugger::IRewindDebuggerTrackCreator
{
	virtual FName GetTargetTypeNameInternal() const override;
	virtual FName GetNameInternal() const override { return "MobWaterBody"; }
	virtual void GetTrackTypesInternal(TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> CreateTrackInternal(
		const RewindDebugger::FObjectId& InObjectId) const override;
	virtual bool HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const override;
};

class FMobWaterQueryTrackCreator : public RewindDebugger::IRewindDebuggerTrackCreator
{
	virtual FName GetTargetTypeNameInternal() const override;
	virtual FName GetNameInternal() const override { return "MobWaterQueries"; }
	virtual void GetTrackTypesInternal(TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> CreateTrackInternal(
		const RewindDebugger::FObjectId& InObjectId) const override;
	virtual bool HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const override;
};

}
