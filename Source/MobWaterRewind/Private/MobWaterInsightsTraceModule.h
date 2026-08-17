// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/ModuleService.h"

/** Registers the provider and the analyzer with an analysis session. */
class FMobWaterInsightsTraceModule : public TraceServices::IModule
{
public:
	static FName ModuleName;

	//~ Begin IModule Interface
	virtual void GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& InSession) override;
	virtual void GetLoggers(TArray<const TCHAR*>& OutLoggers) override;
	virtual void GenerateReports(const TraceServices::IAnalysisSession& Session, const TCHAR* CmdLine,
		const TCHAR* OutputDirectory) override {}
	//~ End IModule Interface
};
