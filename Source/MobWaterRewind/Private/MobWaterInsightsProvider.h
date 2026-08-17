// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Common/ProviderLock.h"
#include "Model/PointTimeline.h"
#include "TraceServices/Containers/Timelines.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{
	class IAnalysisSession;
	extern thread_local FProviderLock::FThreadLocalState GMobWaterProviderLockState;
}

/** What a machine believed about the water on one frame. Three values and a hash, per the header. */
struct FMobWaterStateMessage
{
	double RawTime = 0.0;
	float WaterTime = 0.f;
	uint32 WaveHash = 0;
	int32 NetMode = 0;
	int32 BodyCount = 0;
};

/** Where a body of water was. One of the only three things two machines can disagree about here. */
struct FMobWaterBodyMessage
{
	FVector Location = FVector::ZeroVector;
	float Yaw = 0.f;
	float ExtentX = 0.f;
	float ExtentY = 0.f;
	uint8 Shape = 0;
};

/** One query: where it was asked, and what it was told. */
struct FMobWaterQueryMessage
{
	uint64 BodyId = 0;
	FVector Location = FVector::ZeroVector;
	float SurfaceZ = 0.f;
	float ImmersionDepth = 0.f;
	bool bValid = false;
};

struct FMobWaterTimelineSettings
{
	enum { EventsPerPage = 2048 };
};

/**
 * Everything the analyzer read, keyed by whoever it belonged to.
 *
 * Three timelines rather than one, because they are asked different questions. The shared state is
 * asked "were the two machines running the same water", the bodies are asked "was it in the same
 * place", and the queries are asked "were they even asking about the same point" - which is the one
 * that is usually the answer, and the one a recording of answers alone cannot give.
 */
class FMobWaterInsightsProvider : public TraceServices::IProvider, public TraceServices::IEditableProvider
{
public:
	static FName ProviderName;

	explicit FMobWaterInsightsProvider(TraceServices::IAnalysisSession& InSession);

	typedef TraceServices::ITimeline<FMobWaterStateMessage> StateTimeline;
	typedef TraceServices::ITimeline<FMobWaterBodyMessage> BodyTimeline;
	typedef TraceServices::ITimeline<FMobWaterQueryMessage> QueryTimeline;

	//~ Begin IProvider Interface
	virtual void BeginRead() const override { Lock.BeginRead(TraceServices::GMobWaterProviderLockState); }
	virtual void EndRead() const override { Lock.EndRead(TraceServices::GMobWaterProviderLockState); }
	virtual void ReadAccessCheck() const override { Lock.ReadAccessCheck(TraceServices::GMobWaterProviderLockState); }
	//~ End IProvider Interface

	//~ Begin IEditableProvider Interface
	virtual void BeginEdit() const override { Lock.BeginWrite(TraceServices::GMobWaterProviderLockState); }
	virtual void EndEdit() const override { Lock.EndWrite(TraceServices::GMobWaterProviderLockState); }
	virtual void EditAccessCheck() const override { Lock.WriteAccessCheck(TraceServices::GMobWaterProviderLockState); }
	//~ End IEditableProvider Interface

	bool ReadStateTimeline(uint64 WorldId, TFunctionRef<void(const StateTimeline&)> Callback) const;
	bool ReadBodyTimeline(uint64 BodyId, TFunctionRef<void(const BodyTimeline&)> Callback) const;
	bool ReadQueryTimeline(uint64 QuerierId, TFunctionRef<void(const QueryTimeline&)> Callback) const;

	void AppendState(uint64 WorldId, double Time, const FMobWaterStateMessage& Message);
	void AppendBody(uint64 BodyId, double Time, const FMobWaterBodyMessage& Message);
	void AppendQuery(uint64 QuerierId, double Time, const FMobWaterQueryMessage& Message);

private:
	template<typename MessageType>
	using FTimeline = TraceServices::TPointTimeline<MessageType, FMobWaterTimelineSettings>;

	mutable TraceServices::FProviderLock Lock;
	TraceServices::IAnalysisSession& Session;

	TMap<uint64, uint32> WorldToState;
	TArray<TSharedRef<FTimeline<FMobWaterStateMessage>>> StateTimelines;

	TMap<uint64, uint32> ObjectToBody;
	TArray<TSharedRef<FTimeline<FMobWaterBodyMessage>>> BodyTimelines;

	TMap<uint64, uint32> ObjectToQuery;
	TArray<TSharedRef<FTimeline<FMobWaterQueryMessage>>> QueryTimelines;
};
