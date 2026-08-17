// Copyright (c) Jared Taylor

#include "MobWaterTracks.h"

#include "MobWaterComponent.h"
#include "MobWaterInsightsProvider.h"
#include "Common/ProviderLock.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "MobWaterRewind"

namespace MobWaterRewind
{

namespace
{
	static const FMobWaterInsightsProvider* MobWaterProvider()
	{
		const IRewindDebugger* Debugger = IRewindDebugger::Instance();
		const TraceServices::IAnalysisSession* Analysis = Debugger ? Debugger->GetAnalysisSession() : nullptr;

		return Analysis ? Analysis->ReadProvider<FMobWaterInsightsProvider>(
			FMobWaterInsightsProvider::ProviderName) : nullptr;
	}

	/** Which of the three timelines a curve reads. */
	static bool IsQueryCurve(EMobWaterCurve Curve)
	{
		return Curve >= EMobWaterCurve::SurfaceZ;
	}

	static bool IsBodyCurve(EMobWaterCurve Curve)
	{
		return Curve >= EMobWaterCurve::BodyX && Curve <= EMobWaterCurve::BodyYaw;
	}
}

FMobWaterCurveTrack::FMobWaterCurveTrack(uint64 InObjectId, EMobWaterCurve InCurve)
	: ObjectId(InObjectId)
	, Curve(InCurve)
{
}

FText FMobWaterCurveTrack::GetDisplayNameInternal() const
{
	switch (Curve)
	{
	case EMobWaterCurve::WaterTime:			return LOCTEXT("WaterTime", "Water Time");
	case EMobWaterCurve::RawTime:			return LOCTEXT("RawTime", "Raw Time");
	case EMobWaterCurve::WaveHash:			return LOCTEXT("WaveHash", "Wave Set");
	case EMobWaterCurve::BodyCount:			return LOCTEXT("BodyCount", "Bodies");
	case EMobWaterCurve::BodyX:				return LOCTEXT("BodyX", "X");
	case EMobWaterCurve::BodyY:				return LOCTEXT("BodyY", "Y");
	case EMobWaterCurve::BodyZ:				return LOCTEXT("BodyZ", "Surface Height");
	case EMobWaterCurve::BodyYaw:			return LOCTEXT("BodyYaw", "Yaw");
	case EMobWaterCurve::SurfaceZ:			return LOCTEXT("SurfaceZ", "Surface Z");
	case EMobWaterCurve::ImmersionDepth:	return LOCTEXT("ImmersionDepth", "Immersion");
	case EMobWaterCurve::QueryX:			return LOCTEXT("QueryX", "Asked About X");
	case EMobWaterCurve::QueryY:			return LOCTEXT("QueryY", "Asked About Y");
	case EMobWaterCurve::QueryZ:			return LOCTEXT("QueryZ", "Asked About Z");
	default:								return FText::GetEmpty();
	}
}

TSharedPtr<SCurveTimelineView::FTimelineCurveData> FMobWaterCurveTrack::GetCurveData() const
{
	if (!CurveData.IsValid())
	{
		CurveData = MakeShared<SCurveTimelineView::FTimelineCurveData>();
	}

	++UpdatesRequested;

	return CurveData;
}

bool FMobWaterCurveTrack::UpdateInternal()
{
	// Rebuilt every tenth request rather than every one, which is what the engine's own curve tracks
	// do. The widget asks on every paint, and re-reading a whole timeline at the refresh rate of the
	// editor costs more than everything else the Rewind Debugger does put together.
	if (UpdatesRequested <= 10 || !CurveData.IsValid())
	{
		return false;
	}

	const IRewindDebugger* Debugger = IRewindDebugger::Instance();
	const FMobWaterInsightsProvider* Provider = MobWaterProvider();

	if (!Debugger || !Provider)
	{
		return false;
	}

	const TRange<double> Range = Debugger->GetCurrentTraceRange();
	const double StartTime = Range.GetLowerBoundValue();
	const double EndTime = Range.GetUpperBoundValue();

	TArray<SCurveTimelineView::FTimelineCurveData::CurvePoint>& Points = CurveData->Points;
	Points.SetNum(0, EAllowShrinking::No);

	TraceServices::FProviderReadScopeLock ReadScope(*Provider);

	if (IsQueryCurve(Curve))
	{
		Provider->ReadQueryTimeline(ObjectId, [this, StartTime, EndTime, &Points]
			(const FMobWaterInsightsProvider::QueryTimeline& Timeline)
		{
			Timeline.EnumerateEvents(StartTime, EndTime, [this, &Points]
				(double InStart, double InEnd, uint32 InDepth, const FMobWaterQueryMessage& Message)
			{
				float Value = 0.f;
				switch (Curve)
				{
				case EMobWaterCurve::SurfaceZ:		Value = Message.SurfaceZ; break;
				case EMobWaterCurve::ImmersionDepth:Value = Message.ImmersionDepth; break;
				case EMobWaterCurve::QueryX:		Value = static_cast<float>(Message.Location.X); break;
				case EMobWaterCurve::QueryY:		Value = static_cast<float>(Message.Location.Y); break;
				case EMobWaterCurve::QueryZ:		Value = static_cast<float>(Message.Location.Z); break;
				default: break;
				}

				Points.Add({ InStart, Value });
				return TraceServices::EEventEnumerate::Continue;
			});
		});
	}
	else if (IsBodyCurve(Curve))
	{
		Provider->ReadBodyTimeline(ObjectId, [this, StartTime, EndTime, &Points]
			(const FMobWaterInsightsProvider::BodyTimeline& Timeline)
		{
			Timeline.EnumerateEvents(StartTime, EndTime, [this, &Points]
				(double InStart, double InEnd, uint32 InDepth, const FMobWaterBodyMessage& Message)
			{
				float Value = 0.f;
				switch (Curve)
				{
				case EMobWaterCurve::BodyX:		Value = static_cast<float>(Message.Location.X); break;
				case EMobWaterCurve::BodyY:		Value = static_cast<float>(Message.Location.Y); break;
				case EMobWaterCurve::BodyZ:		Value = static_cast<float>(Message.Location.Z); break;
				case EMobWaterCurve::BodyYaw:	Value = Message.Yaw; break;
				default: break;
				}

				Points.Add({ InStart, Value });
				return TraceServices::EEventEnumerate::Continue;
			});
		});
	}
	else
	{
		Provider->ReadStateTimeline(ObjectId, [this, StartTime, EndTime, &Points]
			(const FMobWaterInsightsProvider::StateTimeline& Timeline)
		{
			Timeline.EnumerateEvents(StartTime, EndTime, [this, &Points]
				(double InStart, double InEnd, uint32 InDepth, const FMobWaterStateMessage& Message)
			{
				float Value = 0.f;
				switch (Curve)
				{
				case EMobWaterCurve::WaterTime:	Value = Message.WaterTime; break;
				case EMobWaterCurve::RawTime:	Value = static_cast<float>(Message.RawTime); break;

				// The hash is drawn rather than read. Nobody wants to know what it is; what is worth
				// seeing is the moment it steps, because the two machines were running different wave
				// sets from that frame on. Scaled down so it shares an axis with something legible.
				case EMobWaterCurve::WaveHash:	Value = static_cast<float>(Message.WaveHash % 1000u); break;
				case EMobWaterCurve::BodyCount:	Value = static_cast<float>(Message.BodyCount); break;
				default: break;
				}

				Points.Add({ InStart, Value });
				return TraceServices::EEventEnumerate::Continue;
			});
		});
	}

	UpdatesRequested = 0;
	return false;
}

TSharedPtr<SWidget> FMobWaterCurveTrack::GetTimelineViewInternal()
{
	const FLinearColor CurveColor = FLinearColor::MakeFromHSV8(140, 140, 160);
	const FLinearColor SelectedColor = FLinearColor::MakeFromHSV8(140, 140, 220);

	return SNew(SCurveTimelineView)
		.TrackName(GetDisplayNameInternal())
		.CurveColor_Lambda([CurveColor, SelectedColor, this]()
		{
			return GetIsSelected() ? SelectedColor : CurveColor;
		})
		.ViewRange_Lambda([]() { return IRewindDebugger::Instance()->GetCurrentViewRange(); })
		.CurveData_Raw(this, &FMobWaterCurveTrack::GetCurveData);
}

FMobWaterStateTrack::FMobWaterStateTrack(uint64 InObjectId)
	: ObjectId(InObjectId)
{
	Icon = FSlateIconFinder::FindIconForClass(UWorld::StaticClass());

	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::WaterTime));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::RawTime));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::WaveHash));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::BodyCount));
}

FText FMobWaterStateTrack::GetDisplayNameInternal() const
{
	return LOCTEXT("MobWaterStateTrack", "Water");
}

bool FMobWaterStateTrack::UpdateInternal()
{
	for (const TSharedPtr<FRewindDebuggerTrack>& Child : Children)
	{
		Child->Update();
	}

	return false;
}

TConstArrayView<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> FMobWaterStateTrack::GetChildrenInternal(
	TArray<TSharedPtr<FRewindDebuggerTrack>>& OutTracks) const
{
	return Children;
}

FMobWaterBodyTrack::FMobWaterBodyTrack(uint64 InObjectId)
	: ObjectId(InObjectId)
{
	Icon = FSlateIconFinder::FindIconForClass(UMobWaterComponent::StaticClass());

	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::BodyZ));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::BodyX));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::BodyY));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::BodyYaw));
}

FText FMobWaterBodyTrack::GetDisplayNameInternal() const
{
	return LOCTEXT("MobWaterBodyTrack", "Water Body");
}

bool FMobWaterBodyTrack::UpdateInternal()
{
	for (const TSharedPtr<FRewindDebuggerTrack>& Child : Children)
	{
		Child->Update();
	}

	return false;
}

TConstArrayView<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> FMobWaterBodyTrack::GetChildrenInternal(
	TArray<TSharedPtr<FRewindDebuggerTrack>>& OutTracks) const
{
	return Children;
}

FMobWaterQueryTrack::FMobWaterQueryTrack(uint64 InObjectId)
	: ObjectId(InObjectId)
{
	Icon = FSlateIconFinder::FindIconForClass(UWorld::StaticClass());

	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::SurfaceZ));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::ImmersionDepth));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::QueryX));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::QueryY));
	Children.Add(MakeShared<FMobWaterCurveTrack>(ObjectId, EMobWaterCurve::QueryZ));
}

FText FMobWaterQueryTrack::GetDisplayNameInternal() const
{
	return LOCTEXT("MobWaterQueryTrack", "Water Queries");
}

bool FMobWaterQueryTrack::UpdateInternal()
{
	for (const TSharedPtr<FRewindDebuggerTrack>& Child : Children)
	{
		Child->Update();
	}

	return false;
}

TConstArrayView<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> FMobWaterQueryTrack::GetChildrenInternal(
	TArray<TSharedPtr<FRewindDebuggerTrack>>& OutTracks) const
{
	return Children;
}

FName FMobWaterStateTrackCreator::GetTargetTypeNameInternal() const
{
	return UObject::StaticClass()->GetFName();
}

void FMobWaterStateTrackCreator::GetTrackTypesInternal(
	TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const
{
	Types.Add({ "MobWater", LOCTEXT("MobWaterTrackType", "Water") });
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FMobWaterStateTrackCreator::CreateTrackInternal(
	const RewindDebugger::FObjectId& InObjectId) const
{
	return MakeShared<FMobWaterStateTrack>(InObjectId.GetMainId());
}

bool FMobWaterStateTrackCreator::HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const
{
	const FMobWaterInsightsProvider* Provider = MobWaterProvider();
	if (!Provider)
	{
		return false;
	}

	bool bHasData = false;

	TraceServices::FProviderReadScopeLock ReadScope(*Provider);
	Provider->ReadStateTimeline(InObjectId.GetMainId(),
		[&bHasData](const FMobWaterInsightsProvider::StateTimeline&) { bHasData = true; });

	return bHasData;
}

FName FMobWaterBodyTrackCreator::GetTargetTypeNameInternal() const
{
	return UObject::StaticClass()->GetFName();
}

void FMobWaterBodyTrackCreator::GetTrackTypesInternal(
	TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const
{
	Types.Add({ "MobWaterBody", LOCTEXT("MobWaterBodyTrackType", "Water Body") });
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FMobWaterBodyTrackCreator::CreateTrackInternal(
	const RewindDebugger::FObjectId& InObjectId) const
{
	return MakeShared<FMobWaterBodyTrack>(InObjectId.GetMainId());
}

bool FMobWaterBodyTrackCreator::HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const
{
	const FMobWaterInsightsProvider* Provider = MobWaterProvider();
	if (!Provider)
	{
		return false;
	}

	bool bHasData = false;

	TraceServices::FProviderReadScopeLock ReadScope(*Provider);
	Provider->ReadBodyTimeline(InObjectId.GetMainId(),
		[&bHasData](const FMobWaterInsightsProvider::BodyTimeline&) { bHasData = true; });

	return bHasData;
}

FName FMobWaterQueryTrackCreator::GetTargetTypeNameInternal() const
{
	return UObject::StaticClass()->GetFName();
}

void FMobWaterQueryTrackCreator::GetTrackTypesInternal(
	TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const
{
	Types.Add({ "MobWaterQueries", LOCTEXT("MobWaterQueryTrackType", "Water Queries") });
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FMobWaterQueryTrackCreator::CreateTrackInternal(
	const RewindDebugger::FObjectId& InObjectId) const
{
	return MakeShared<FMobWaterQueryTrack>(InObjectId.GetMainId());
}

bool FMobWaterQueryTrackCreator::HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const
{
	const FMobWaterInsightsProvider* Provider = MobWaterProvider();
	if (!Provider)
	{
		return false;
	}

	bool bHasData = false;

	TraceServices::FProviderReadScopeLock ReadScope(*Provider);
	Provider->ReadQueryTimeline(InObjectId.GetMainId(),
		[&bHasData](const FMobWaterInsightsProvider::QueryTimeline&) { bHasData = true; });

	return bHasData;
}

}

#undef LOCTEXT_NAMESPACE
