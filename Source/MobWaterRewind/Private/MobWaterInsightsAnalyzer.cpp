// Copyright (c) Jared Taylor

#include "MobWaterInsightsAnalyzer.h"

#include "MobWaterInsightsProvider.h"
#include "Common/ProviderLock.h"

FMobWaterInsightsAnalyzer::FMobWaterInsightsAnalyzer(TraceServices::IAnalysisSession& InSession,
	FMobWaterInsightsProvider& InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
}

void FMobWaterInsightsAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_State, "MobWater", "State");
	Builder.RouteEvent(RouteId_Body, "MobWater", "Body");
	Builder.RouteEvent(RouteId_Query, "MobWater", "Query");
}

bool FMobWaterInsightsAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	TraceServices::FProviderEditScopeLock ProviderEditScope(Provider);

	const auto& EventData = Context.EventData;
	const double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));

	switch (RouteId)
	{
	case RouteId_State:
		{
			FMobWaterStateMessage Message;
			Message.RawTime = EventData.GetValue<double>("RawTime");
			Message.WaterTime = EventData.GetValue<float>("WaterTime");
			Message.WaveHash = EventData.GetValue<uint32>("WaveHash");
			Message.NetMode = EventData.GetValue<int32>("NetMode");
			Message.BodyCount = EventData.GetValue<int32>("BodyCount");

			Provider.AppendState(EventData.GetValue<uint64>("WorldId"), Time, Message);
			break;
		}

	case RouteId_Body:
		{
			FMobWaterBodyMessage Message;
			Message.Location = FVector(
				EventData.GetValue<double>("X"),
				EventData.GetValue<double>("Y"),
				EventData.GetValue<double>("Z"));
			Message.Yaw = EventData.GetValue<float>("Yaw");
			Message.ExtentX = EventData.GetValue<float>("ExtentX");
			Message.ExtentY = EventData.GetValue<float>("ExtentY");
			Message.Shape = EventData.GetValue<uint8>("Shape");

			Provider.AppendBody(EventData.GetValue<uint64>("BodyId"), Time, Message);
			break;
		}

	case RouteId_Query:
		{
			FMobWaterQueryMessage Message;
			Message.BodyId = EventData.GetValue<uint64>("BodyId");
			Message.Location = FVector(
				EventData.GetValue<double>("X"),
				EventData.GetValue<double>("Y"),
				EventData.GetValue<double>("Z"));
			Message.SurfaceZ = EventData.GetValue<float>("SurfaceZ");
			Message.ImmersionDepth = EventData.GetValue<float>("ImmersionDepth");
			Message.bValid = EventData.GetValue<bool>("Valid");

			Provider.AppendQuery(EventData.GetValue<uint64>("QuerierId"), Time, Message);
			break;
		}

	default:
		break;
	}

	return true;
}
