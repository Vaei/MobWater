// Copyright (c) Jared Taylor

#include "MobWaterInsightsProvider.h"

namespace TraceServices
{
	thread_local FProviderLock::FThreadLocalState GMobWaterProviderLockState;
}

FName FMobWaterInsightsProvider::ProviderName("MobWaterProvider");

FMobWaterInsightsProvider::FMobWaterInsightsProvider(TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
{
}

namespace
{
	/** The timeline for an id, made on first use. One shape, three times, so it is written once. */
	template<typename TimelineType>
	static TSharedRef<TimelineType> MobWaterFindOrAdd(TMap<uint64, uint32>& Index,
		TArray<TSharedRef<TimelineType>>& Timelines, uint64 Id, TraceServices::IAnalysisSession& Session)
	{
		if (const uint32* Found = Index.Find(Id))
		{
			return Timelines[*Found];
		}

		TSharedRef<TimelineType> Timeline = MakeShared<TimelineType>(Session.GetLinearAllocator());
		Index.Add(Id, Timelines.Num());
		Timelines.Add(Timeline);

		return Timeline;
	}

	template<typename TimelineType, typename CallbackType>
	static bool MobWaterRead(const TMap<uint64, uint32>& Index,
		const TArray<TSharedRef<TimelineType>>& Timelines, uint64 Id, CallbackType&& Callback)
	{
		if (const uint32* Found = Index.Find(Id))
		{
			if (Timelines.IsValidIndex(*Found))
			{
				Callback(*Timelines[*Found]);
				return true;
			}
		}

		return false;
	}
}

bool FMobWaterInsightsProvider::ReadStateTimeline(uint64 WorldId,
	TFunctionRef<void(const StateTimeline&)> Callback) const
{
	ReadAccessCheck();
	return MobWaterRead(WorldToState, StateTimelines, WorldId, Callback);
}

bool FMobWaterInsightsProvider::ReadBodyTimeline(uint64 BodyId,
	TFunctionRef<void(const BodyTimeline&)> Callback) const
{
	ReadAccessCheck();
	return MobWaterRead(ObjectToBody, BodyTimelines, BodyId, Callback);
}

bool FMobWaterInsightsProvider::ReadQueryTimeline(uint64 QuerierId,
	TFunctionRef<void(const QueryTimeline&)> Callback) const
{
	ReadAccessCheck();
	return MobWaterRead(ObjectToQuery, QueryTimelines, QuerierId, Callback);
}

void FMobWaterInsightsProvider::AppendState(uint64 WorldId, double Time, const FMobWaterStateMessage& Message)
{
	EditAccessCheck();
	MobWaterFindOrAdd(WorldToState, StateTimelines, WorldId, Session)->AppendEvent(Time, Message);
}

void FMobWaterInsightsProvider::AppendBody(uint64 BodyId, double Time, const FMobWaterBodyMessage& Message)
{
	EditAccessCheck();
	MobWaterFindOrAdd(ObjectToBody, BodyTimelines, BodyId, Session)->AppendEvent(Time, Message);
}

void FMobWaterInsightsProvider::AppendQuery(uint64 QuerierId, double Time, const FMobWaterQueryMessage& Message)
{
	EditAccessCheck();
	MobWaterFindOrAdd(ObjectToQuery, QueryTimelines, QuerierId, Session)->AppendEvent(Time, Message);
}
