// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Trace/Analyzer.h"

class FMobWaterInsightsProvider;

namespace TraceServices { class IAnalysisSession; }

/** Turns the three MobWater trace events back into timelines the tracks read. */
class FMobWaterInsightsAnalyzer : public UE::Trace::IAnalyzer
{
public:
	FMobWaterInsightsAnalyzer(TraceServices::IAnalysisSession& InSession, FMobWaterInsightsProvider& InProvider);

	//~ Begin IAnalyzer Interface
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override {}
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
	//~ End IAnalyzer Interface

private:
	enum : uint16
	{
		RouteId_State,
		RouteId_Body,
		RouteId_Query,
	};

	TraceServices::IAnalysisSession& Session;
	FMobWaterInsightsProvider& Provider;
};
