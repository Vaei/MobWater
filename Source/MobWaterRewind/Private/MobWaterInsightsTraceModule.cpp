// Copyright (c) Jared Taylor

#include "MobWaterInsightsTraceModule.h"

#include "MobWaterInsightsAnalyzer.h"
#include "MobWaterInsightsProvider.h"

FName FMobWaterInsightsTraceModule::ModuleName("MobWater");

void FMobWaterInsightsTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("MobWater");
}

void FMobWaterInsightsTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& InSession)
{
	TSharedPtr<FMobWaterInsightsProvider> Provider = MakeShared<FMobWaterInsightsProvider>(InSession);
	InSession.AddProvider(FMobWaterInsightsProvider::ProviderName, Provider, Provider);

	InSession.AddAnalyzer(new FMobWaterInsightsAnalyzer(InSession, *Provider));
}

void FMobWaterInsightsTraceModule::GetLoggers(TArray<const TCHAR*>& OutLoggers)
{
	OutLoggers.Add(TEXT("MobWater"));
}
