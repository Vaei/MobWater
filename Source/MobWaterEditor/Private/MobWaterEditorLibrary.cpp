// Copyright (c) Jared Taylor

#include "MobWaterEditorLibrary.h"

#include "AssetCompilingManager.h"

void UMobWaterEditorLibrary::FinishAssetCompilation()
{
	FAssetCompilingManager::Get().FinishAllCompilation();
}
