// Copyright (c) Jared Taylor

#include "MobWaterEditor.h"

#include "MobWaterActorFactory.h"
#include "MobWaterComponent.h"
#include "MobWaterBodyActor.h"
#include "MobWaterDetails.h"
#include "MobWaterEditorStyle.h"
#include "MobWaterExclusionActor.h"
#include "MobWaterUnderwaterComponent.h"
#include "MobWaterExclusionComponent.h"
#include "MobWaterExclusionVisualizer.h"
#include "MobWaterDisturbanceComponent.h"
#include "MobWaterInteractionComponent.h"
#include "MobWaterSplineComponent.h"
#include "MobWaterLookPreset.h"
#include "MobWaterMeshLibrary.h"
#include "MobWaterOceanActor.h"
#include "MobWaterPoolActor.h"
#include "SMobWaterMenuEntry.h"
#include "ActorFactories/ActorFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Selection.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "IPlacementModeModule.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "MobWaterEditorUserSettings.h"
#include "MobWaterModule.h"
#include "MobWaterSettings.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UnrealEdGlobals.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IPythonScriptPlugin.h"
#include "IRewindDebugger.h"
#include "LevelEditor.h"
#include "ObjectTrace.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/Paths.h"
#include "PropertyEditorModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "UObject/SavePackage.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MobWaterEditor"

DEFINE_LOG_CATEGORY(LogMobWaterEditor);

/** The Rewind Debugger's tab, named rather than linked so a project without it only loses a menu entry. */
static const FName MobWaterRewindTabId(TEXT("RewindDebugger2"));

namespace
{
	/** The shapes that can be placed, and what places them. */
	struct FShapeEntry
	{
		EMobWaterShape Shape;
		UClass* FactoryClass;
		const TCHAR* Label;
		const TCHAR* Tip;
	};

	TArray<FShapeEntry> ShapeEntries()
	{
		return {
			{ EMobWaterShape::Box, UMobWaterPoolFactory_Box::StaticClass(), TEXT("Water Pool"),
				TEXT("A rectangular body. Baths, troughs, flooded rooms, rice paddies.") },
			{ EMobWaterShape::Disc, UMobWaterPoolFactory_Disc::StaticClass(), TEXT("Water Disc"),
				TEXT("A circular body. Puddles, basins, wells, ponds.") },
		};
	}

	const FName PlacementCategoryHandle(TEXT("MobWater"));
}

void FMobWaterEditorModule::StartupModule()
{
	FMobWaterEditorStyle::Register();

	MissingMaterialHandle = OnMobWaterMaterialMissing.AddStatic(&FMobWaterEditorModule::OnMaterialMissing);

	if (GUnrealEd)
	{
		GUnrealEd->RegisterComponentVisualizer(UMobWaterExclusionComponent::StaticClass()->GetFName(),
			MakeShared<FMobWaterExclusionVisualizer>());
	}

	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	// The order someone actually builds a body of water in: what it is, how big, what shape the
	// surface takes, then what it looks like, then what happens in it.
	const TArray<FName> BodyCategories = {
		TEXT("Water"), TEXT("Waves"), TEXT("Colour"), TEXT("Surface"), TEXT("Foam"),
		TEXT("Caustics"), TEXT("Refraction"), TEXT("Reflection"), TEXT("Ripples"), TEXT("Query"),
	};

	TArray<FName> OceanCategories = BodyCategories;
	OceanCategories.Insert(TEXT("Ocean"), 1);

	// Registered for every component and the actor that owns it both. Selecting the actor in the
	// outliner and selecting its component in the tree are different panels, and only fixing one is
	// worse than fixing neither - it reads as the order being random.
	auto Prioritise = [&PropertyModule](const UClass* Class, const TArray<FName>& Categories)
	{
		PropertyModule.RegisterCustomClassLayout(Class->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FMobWaterDetails::Make, Categories));
	};

	Prioritise(UMobWaterComponent::StaticClass(), BodyCategories);
	Prioritise(AMobWaterPool::StaticClass(), BodyCategories);
	Prioritise(AMobWaterBody::StaticClass(), BodyCategories);
	Prioritise(AMobWaterOcean::StaticClass(), OceanCategories);
	Prioritise(UMobWaterSplineComponent::StaticClass(), { TEXT("Water") });
	Prioritise(UMobWaterUnderwaterComponent::StaticClass(), { TEXT("Underwater") });
	Prioritise(UMobWaterExclusionComponent::StaticClass(), { TEXT("Exclusion") });
	Prioritise(AMobWaterExclusion::StaticClass(), { TEXT("Exclusion") });
	Prioritise(UMobWaterDisturbanceComponent::StaticClass(), { TEXT("Disturbance") });
	Prioritise(UMobWaterInteractionComponent::StaticClass(),
		{ TEXT("Water"), TEXT("Movement"), TEXT("Ripples") });

	PropertyModule.NotifyCustomizationModuleChanged();

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
		this, &FMobWaterEditorModule::RegisterMenus));

	// The placement panel needs actor factories, and the editor has not instanced them while modules
	// are still loading.
	if (GEditor)
	{
		RegisterPlacement();
	}
	else
	{
		FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FMobWaterEditorModule::RegisterPlacement);
	}
}

void FMobWaterEditorModule::RegisterPlacement()
{
	if (!GEditor || !IPlacementModeModule::IsAvailable())
	{
		return;
	}

	IPlacementModeModule& Placement = IPlacementModeModule::Get();

	FPlacementCategoryInfo Category(
		LOCTEXT("PlacementCategory", "Mob Water"),
		FSlateIcon(FMobWaterEditorStyle::GetStyleSetName(), FMobWaterEditorStyle::GetMenuIconName()),
		PlacementCategoryHandle,
		TEXT("PMMobWater"),
		22);

	if (!Placement.RegisterPlacementCategory(Category))
	{
		return;
	}

	for (const FShapeEntry& Entry : ShapeEntries())
	{
		if (UActorFactory* Factory = GEditor->FindActorFactoryByClass(Entry.FactoryClass))
		{
			Placement.RegisterPlaceableItem(PlacementCategoryHandle, MakeShared<FPlaceableItem>(
				Factory, FAssetData(AMobWaterPool::StaticClass())));
		}
	}

	for (UClass* FactoryClass : { UMobWaterBodyFactory_Lake::StaticClass(), UMobWaterBodyFactory_River::StaticClass() })
	{
		if (UActorFactory* Factory = GEditor->FindActorFactoryByClass(FactoryClass))
		{
			Placement.RegisterPlaceableItem(PlacementCategoryHandle, MakeShared<FPlaceableItem>(
				Factory, FAssetData(AMobWaterBody::StaticClass())));
		}
	}

	if (UActorFactory* Factory = GEditor->FindActorFactoryByClass(UMobWaterOceanFactory::StaticClass()))
	{
		Placement.RegisterPlaceableItem(PlacementCategoryHandle, MakeShared<FPlaceableItem>(
			Factory, FAssetData(AMobWaterOcean::StaticClass())));
	}

	if (UActorFactory* Factory = GEditor->FindActorFactoryByClass(UMobWaterExclusionFactory::StaticClass()))
	{
		Placement.RegisterPlaceableItem(PlacementCategoryHandle, MakeShared<FPlaceableItem>(
			Factory, FAssetData(AMobWaterExclusion::StaticClass())));
	}
}

FVector FMobWaterEditorModule::PlacementLocation()
{
	if (GCurrentLevelEditingViewportClient)
	{
		return GCurrentLevelEditingViewportClient->GetViewLocation()
			+ GCurrentLevelEditingViewportClient->GetViewRotation().Vector() * 500.f;
	}

	return FVector::ZeroVector;
}

void FMobWaterEditorModule::PlaceWater(EMobWaterShape Shape)
{
	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("PlaceWaterMenu", "Place Water"));

	AMobWaterPool* Pool = World->SpawnActor<AMobWaterPool>(PlacementLocation(), FRotator::ZeroRotator);
	if (!Pool)
	{
		return;
	}

	if (UMobWaterComponent* Water = Pool->GetWaterComponent())
	{
		Water->Shape = Shape;
		Water->ApplySurface();
	}

	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Pool, true, true);
}

void FMobWaterEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);

	if (GUnrealEd)
	{
		// By literal name: the class may already be gone by the time a module unloads.
		GUnrealEd->UnregisterComponentVisualizer(TEXT("MobWaterExclusionComponent"));
	}

	if (FPropertyEditorModule* PropertyModule =
		FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterComponent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterPool"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterBody"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterOcean"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterSplineComponent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterUnderwaterComponent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterExclusionComponent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterExclusion"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterDisturbanceComponent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MobWaterInteractionComponent"));
	}

	if (IPlacementModeModule::IsAvailable())
	{
		IPlacementModeModule::Get().UnregisterPlacementCategory(PlacementCategoryHandle);
	}

	if (MissingMaterialHandle.IsValid())
	{
		OnMobWaterMaterialMissing.Remove(MissingMaterialHandle);
		MissingMaterialHandle.Reset();
	}

	FMobWaterEditorStyle::Unregister();
}

void FMobWaterEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
	if (!ToolBar)
	{
		return;
	}

	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
		TEXT("MobWaterMenu"),
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateStatic(&FMobWaterEditorModule::IsToolbarMenuEnabled)),
		FOnGetContent::CreateRaw(this, &FMobWaterEditorModule::BuildMenu),
		LOCTEXT("WaterToolbar", "Water"),
		LOCTEXT("WaterToolbarTip", "Place and author water"),
		FSlateIcon(FMobWaterEditorStyle::GetStyleSetName(), FMobWaterEditorStyle::GetMenuIconName())
	);

	// The style that gives a toolbar button its label beside the icon.
	Entry.StyleNameOverride = TEXT("CalloutToolbar");

	ToolBar->FindOrAddSection(TEXT("PlayGameExtensions")).AddEntry(Entry);
}

TSharedRef<SWidget> FMobWaterEditorModule::BuildMenu()
{
	FMenuBuilder Menu(true, nullptr);

	Menu.BeginSection(TEXT("MobWaterPlace"), LOCTEXT("PlaceSection", "Place"));
	for (const FShapeEntry& Entry : ShapeEntries())
	{
		UActorFactory* Factory = GEditor ? GEditor->FindActorFactoryByClass(Entry.FactoryClass) : nullptr;
		if (!Factory)
		{
			// A factory the editor has not instanced yet drags nothing and looks identical to one
			// that works, so fall back to something that at least places.
			Menu.AddMenuEntry(
				FText::FromString(Entry.Label),
				FText::FromString(Entry.Tip),
				FSlateIcon(FMobWaterEditorStyle::GetStyleSetName(), FMobWaterEditorStyle::GetMenuIconName()),
				FUIAction(FExecuteAction::CreateStatic(&FMobWaterEditorModule::PlaceWater, Entry.Shape)));
			continue;
		}

		// A widget rather than a menu entry, because a menu entry cannot be dragged into the level.
		Menu.AddWidget(
			SNew(SMobWaterMenuEntry, Factory,
				FMobWaterEditorStyle::Get().GetBrush(FMobWaterEditorStyle::GetMenuIconName()),
				AMobWaterPool::StaticClass())
				.Label(FText::FromString(Entry.Label))
				.ToolTip(FText::FromString(Entry.Tip)),
			FText::GetEmpty(), true);
	}
	struct FBodyEntry
	{
		UClass* FactoryClass;
		const TCHAR* Label;
		const TCHAR* Tip;
	};

	static const FBodyEntry BodyEntries[] =
	{
		{ UMobWaterBodyFactory_Lake::StaticClass(), TEXT("Water Lake"),
			TEXT("A closed spline. The shoreline is whatever you draw, and the water is everything inside it.") },
		{ UMobWaterBodyFactory_River::StaticClass(), TEXT("Water River"),
			TEXT("An open spline. The line is the middle of the river and the width says where the banks are.") },
	};

	if (UActorFactory* Ocean = GEditor ? GEditor->FindActorFactoryByClass(UMobWaterOceanFactory::StaticClass()) : nullptr)
	{
		Menu.AddWidget(
			SNew(SMobWaterMenuEntry, Ocean,
				FMobWaterEditorStyle::Get().GetBrush(FMobWaterEditorStyle::GetMenuIconName()),
				AMobWaterOcean::StaticClass())
				.Label(LOCTEXT("OceanRow", "Water Ocean"))
				.ToolTip(LOCTEXT("OceanRowTip",
					"Water with no edge, kept centred on the view. The surface is a function of world "
					"position, so following the camera moves the window and not the water.")),
			FText::GetEmpty(), true);
	}

	for (const FBodyEntry& Entry : BodyEntries)
	{
		if (UActorFactory* Factory = GEditor ? GEditor->FindActorFactoryByClass(Entry.FactoryClass) : nullptr)
		{
			Menu.AddWidget(
				SNew(SMobWaterMenuEntry, Factory,
					FMobWaterEditorStyle::Get().GetBrush(FMobWaterEditorStyle::GetMenuIconName()),
					AMobWaterBody::StaticClass())
					.Label(FText::FromString(Entry.Label))
					.ToolTip(FText::FromString(Entry.Tip)),
				FText::GetEmpty(), true);
		}
	}

	if (UActorFactory* Factory = GEditor ? GEditor->FindActorFactoryByClass(UMobWaterExclusionFactory::StaticClass()) : nullptr)
	{
		Menu.AddWidget(
			SNew(SMobWaterMenuEntry, Factory,
				FMobWaterEditorStyle::Get().GetBrush(FMobWaterEditorStyle::GetMenuIconName()),
				AMobWaterExclusion::StaticClass())
				.Label(LOCTEXT("ExclusionRow", "Water Exclusion"))
				.ToolTip(LOCTEXT("ExclusionRowTip",
					"An area water is kept out of. Attach one to a hull and the boat is dry inside "
					"while it moves. Four can be rendered at once, nearest the view first.")),
			FText::GetEmpty(), true);
	}

	Menu.EndSection();

	Menu.BeginSection(TEXT("MobWaterBuild"), LOCTEXT("BuildSection", "Build"));
	Menu.AddMenuEntry(
		LOCTEXT("SetUp", "Set Up Water"),
		LOCTEXT("SetUpTip",
			"Everything at once: authors the collection, the master, the instances and the meshes, "
			"points the settings at them, and refreshes any water already in the level. Run this once "
			"after installing and there is nothing else to do before dragging a pool in."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Play")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::SetUpWater),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::IsPythonAvailable)));

	Menu.AddMenuEntry(
		LOCTEXT("Generate", "Generate Materials"),
		LOCTEXT("GenerateTip",
			"Authors the collection, the water master and one instance per feature set. Run this once "
			"after installing, and again after changing anything in the shader library."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::GenerateMaterials),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::IsPythonAvailable)));

	Menu.AddMenuEntry(
		LOCTEXT("BakeSpectrum", "Bake Ocean Spectrum"),
		LOCTEXT("BakeSpectrumTip",
			"Solves a Phillips sea offline and bakes it into the two atlases and the table the ocean "
			"reads. Minutes rather than seconds, and only worth running when the sea state itself is "
			"being changed - the parameters are at the top of mob_water_spectrum.py."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Convert")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::BakeSpectrum),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::IsPythonAvailable)));

	Menu.AddMenuEntry(
		LOCTEXT("Verify", "Verify Contract"),
		LOCTEXT("VerifyTip",
			"Asserts what the documentation claims: that the wave maths in the header and the shader "
			"agree, and that the custom primitive data indices the component writes are the ones the "
			"material reads."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Check")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::VerifyContract),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::IsPythonAvailable)));

	Menu.AddMenuEntry(
		LOCTEXT("Report", "Report Cost"),
		LOCTEXT("ReportTip",
			"What this costs: instructions and samplers per master, how many instances exist, and "
			"what the textures weigh. Editor estimates, on whatever preview platform is active."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Info")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::ReportCost),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::IsPythonAvailable)));
	Menu.EndSection();

	Menu.BeginSection(TEXT("MobWaterAssets"), LOCTEXT("AssetsSection", "Water"));
	Menu.AddMenuEntry(
		LOCTEXT("OpenCollection", "Open Wave Collection"),
		LOCTEXT("OpenCollectionTip",
			"The waves and the clock every water material reads. Opened here because it is the one "
			"asset worth scrubbing while looking at the level, and it is nowhere obvious."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.MaterialParameterCollection")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::OpenCollection),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::HasCollection)));

	Menu.AddMenuEntry(
		LOCTEXT("OpenBodyMaterial", "Open Selected Body's Material"),
		LOCTEXT("OpenBodyMaterialTip",
			"The material instance the selected body is actually drawing with. Which of the thirty six "
			"that is depends on the features it has switched on, so it is not something to go looking "
			"for by name - and the foam texture is the one setting that lives there rather than on the "
			"body."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.MaterialInstanceConstant")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::OpenSelectedBodyMaterial),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::HasSelectedBody)));

	Menu.AddMenuEntry(
		LOCTEXT("SaveLookPreset", "Save Look Preset From Selected"),
		LOCTEXT("SaveLookPresetTip",
			"Writes the selected body's colour, foam, glint, caustics and reflection out as a look "
			"preset asset, and points the body at it. Choose where it goes - a look belongs to the "
			"project that tuned it, and one saved inside MobWater is one a regenerate may overwrite."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.SaveModified")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::SaveLookPreset),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::HasSelectedBody)));

	Menu.AddMenuEntry(
		LOCTEXT("SelectOcean", "Select Ocean"),
		LOCTEXT("SelectOceanTip",
			"Selects the level's ocean. It follows the view and has no bank, so there is rarely "
			"anywhere to click on it that is not already something else."),
		FSlateIcon(FMobWaterEditorStyle::GetStyleSetName(), FMobWaterEditorStyle::GetMenuIconName()),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::SelectOcean),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::HasOcean)));

	Menu.AddMenuEntry(
		LOCTEXT("DebugOcean", "Debug Ocean In Rewind Debugger"),
		LOCTEXT("DebugOceanTip",
			"Opens the Rewind Debugger on the level's ocean: the clock, the wave set, and where the "
			"body was, frame by frame. Recording is left alone - what the tracks are for is comparing "
			"two machines, and a record started from here would throw away whatever was captured."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Server")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobWaterEditorModule::DebugOceanInRewindDebugger),
			FCanExecuteAction::CreateStatic(&FMobWaterEditorModule::HasOcean)));
	Menu.EndSection();

	Menu.BeginSection(TEXT("MobWaterSettings"), LOCTEXT("SettingsSection", "Settings"));
	Menu.AddMenuEntry(
		LOCTEXT("OpenSettings", "Open Project Settings"),
		LOCTEXT("OpenSettingsTip", "Project settings, opened at Mob Water rather than wherever it was last."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")),
		FUIAction(FExecuteAction::CreateStatic(&FMobWaterEditorModule::OpenSettings)));

	Menu.AddMenuEntry(
		LOCTEXT("HideMenu", "Hide This Menu"),
		LOCTEXT("HideMenuTip",
			"Takes the Water button off the toolbar. Editor Preferences puts it back, and the choice "
			"is yours alone rather than the project's."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visibility")),
		FUIAction(FExecuteAction::CreateStatic(&FMobWaterEditorModule::HideToolbarMenu)));
	Menu.EndSection();

	return Menu.MakeWidget();
}

bool FMobWaterEditorModule::IsToolbarMenuEnabled()
{
	return GetDefault<UMobWaterEditorUserSettings>()->bShowToolbarMenu;
}

void FMobWaterEditorModule::HideToolbarMenu()
{
	UMobWaterEditorUserSettings* Settings = GetMutableDefault<UMobWaterEditorUserSettings>();
	Settings->bShowToolbarMenu = false;
	Settings->SaveConfig();
}

void FMobWaterEditorModule::OpenSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (!SettingsModule)
	{
		return;
	}

	// Asked of the settings object rather than spelled out. A UDeveloperSettings registers itself
	// under its container, category and section name, and the section name is the class name, not the
	// display name - naming it by hand opens the settings window on whatever was last shown instead,
	// which reads as the entry doing nothing.
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	SettingsModule->ShowViewer(
		Settings->GetContainerName(),
		Settings->GetCategoryName(),
		Settings->GetSectionName());
}

void FMobWaterEditorModule::SetUpWater()
{
	GenerateMaterials();

	const TArray<FString> Missing = AssignGeneratedAssets();

	if (Missing.Num() > 0)
	{
		Notify(FText::Format(
			LOCTEXT("SetUpIncomplete", "MobWater: set up, but {0} problem(s). See the Output Log."),
			FText::AsNumber(Missing.Num())), false);

		for (const FString& Path : Missing)
		{
			UE_LOG(LogMobWaterEditor, Warning, TEXT("MobWater: %s"), *Path);
		}
		return;
	}

	Notify(LOCTEXT("SetUpDone", "MobWater: ready. Drag a Water Pool out of the Water menu."), true);
}

TArray<FString> FMobWaterEditorModule::AssignGeneratedAssets()
{
	struct FAssignment
	{
		EMobWaterShape Shape;
		const TCHAR* Mesh;
		const TCHAR* MaterialShape;
	};

	// Spline and Ocean have no mesh of their own yet, so they borrow the shapes that behave like
	// them: a lake measures its bank the way a rectangle does, an ocean has no bank at all.
	static const FAssignment Assignments[] =
	{
		{ EMobWaterShape::Box,    TEXT("/MobWater/Meshes/SM_MobWaterPlane"), TEXT("Box") },
		{ EMobWaterShape::Disc,   TEXT("/MobWater/Meshes/SM_MobWaterDisc"),  TEXT("Disc") },
		// A spline body generates its own mesh, so the entry here is only for the material; the mesh
		// slot is never read for that shape.
		{ EMobWaterShape::Spline, TEXT("/MobWater/Meshes/SM_MobWaterPlane"), TEXT("Spline") },
		{ EMobWaterShape::Ocean,  TEXT("/MobWater/Meshes/SM_MobWaterOceanRing"), TEXT("Ocean") },
	};

	// The mask space rather than a list, so another feature is one constant rather than more lines.
	// Foam's own texture without foam is the one combination nothing can ask for, so it is not
	// generated and must not be looked for - hunting it would report a dozen assets missing that were
	// never meant to exist, and Set Up Water would say it had failed every time it ran.
	TArray<int32> BuiltVariants;
	for (int32 Variant = 0; Variant < MobWaterVariant::Num; ++Variant)
	{
		if ((Variant & MobWaterVariant::FoamTexture) && !(Variant & MobWaterVariant::Foam))
		{
			continue;
		}

		BuiltVariants.Add(Variant);
	}

	TArray<FString> Missing;

	UMobWaterSettings* Settings = GetMutableDefault<UMobWaterSettings>();

	for (const FAssignment& Assignment : Assignments)
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Assignment.Mesh))
		{
			Settings->SurfaceMeshes.Add(Assignment.Shape, Mesh);
		}
		else
		{
			Missing.Add(FString::Printf(TEXT("expected asset not found: %s"), Assignment.Mesh));
		}

		FMobWaterMaterialSet& Set = Settings->Materials.FindOrAdd(Assignment.Shape);

		// Indexed by the variant mask, so a material has to land at its own index rather than simply
		// being the next thing added.
		Set.Variants.SetNum(MobWaterVariant::Num);

		for (const int32 Variant : BuiltVariants)
		{
			const FString Path = FString::Printf(TEXT("/MobWater/Materials/MI_MobWater_%s%s"),
				Assignment.MaterialShape, *MobWaterVariant::Suffix(Variant));

			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path))
			{
				Set.Variants[Variant] = Material;
			}
			else
			{
				Missing.Add(FString::Printf(TEXT("expected asset not found: %s"), *Path));
			}
		}
	}

	ApplyReflectionTexture(Settings->ReflectionTexture.LoadSynchronous());

	if (!SaveDefaultConfig(Settings))
	{
		Missing.Add(FString::Printf(
			TEXT("%s could not be written, so none of the above will survive a restart"),
			*Settings->GetDefaultConfigFilename()));
	}

	UMobWaterSettings::RefreshPlacedWater();

	return Missing;
}

bool FMobWaterEditorModule::SaveDefaultConfig(UObject* Settings)
{
	// SaveConfig writes the user's ini, not the project's, so on its own this leaves a setting that
	// works until the editor is restarted and then is simply gone. TryUpdateDefaultConfigFile writes
	// the right file, but it refuses a read-only one and says so only in the log - and under
	// perforce the project's ini is read-only until it is checked out, which is every time.
	const FString ConfigFile = Settings->GetDefaultConfigFilename();

	if (FPaths::FileExists(ConfigFile) && IFileManager::Get().IsReadOnly(*ConfigFile))
	{
		if (ISourceControlModule::Get().IsEnabled())
		{
			USourceControlHelpers::CheckOutOrAddFile(ConfigFile);
		}

		// Still read-only means source control declined or is not running. Clearing the flag is worth
		// it either way: the alternative is silently dropping the assignment, which looks exactly
		// like never having run the setup at all.
		if (IFileManager::Get().IsReadOnly(*ConfigFile))
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ConfigFile, false);
		}
	}

	return Settings->TryUpdateDefaultConfigFile();
}

void FMobWaterEditorModule::ApplyReflectionTexture(UTexture* Texture)
{
	if (!Texture)
	{
		return;
	}

	// Written into every instance, because a collection cannot hold a texture and a project should
	// not have to open twenty-four assets to change which sky its water is reflecting.
	const FName ParameterName(TEXT("ReflectionTexture"));

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({ TEXT("/MobWater/Materials") }, true);

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(TEXT("/MobWater/Materials"), Assets);

	int32 Written = 0;

	for (const FAssetData& Asset : Assets)
	{
		UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
		if (!Instance)
		{
			continue;
		}

		UTexture* Existing = nullptr;
		if (Instance->GetTextureParameterValue(ParameterName, Existing) && Existing == Texture)
		{
			continue;
		}

		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, ParameterName, Texture);
		UMaterialEditingLibrary::UpdateMaterialInstance(Instance);

		Instance->MarkPackageDirty();
		++Written;
	}

	if (Written > 0)
	{
		UE_LOG(LogMobWaterEditor, Display,
			TEXT("MobWater: reflection sky set to %s on %d instance(s)."), *Texture->GetName(), Written);
	}
}

bool FMobWaterEditorModule::HasCollection()
{
	return !GetDefault<UMobWaterSettings>()->ParameterCollection.IsNull();
}

namespace
{
	/** The water component on whatever is selected, or null. */
	UMobWaterComponent* SelectedWaterComponent()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		USelection* Selection = GEditor->GetSelectedActors();
		if (!Selection)
		{
			return nullptr;
		}

		TArray<AActor*> Actors;
		Selection->GetSelectedObjects<AActor>(Actors);

		for (const AActor* Actor : Actors)
		{
			if (UMobWaterComponent* Water = Actor ? Actor->FindComponentByClass<UMobWaterComponent>() : nullptr)
			{
				return Water;
			}
		}

		return nullptr;
	}
}

bool FMobWaterEditorModule::HasSelectedBody()
{
	return SelectedWaterComponent() != nullptr;
}

namespace
{
	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
}

bool FMobWaterEditorModule::HasOcean()
{
	UWorld* World = EditorWorld();
	return World && TActorIterator<AMobWaterOcean>(World);
}

void FMobWaterEditorModule::SaveLookPreset()
{
	UMobWaterComponent* Water = SelectedWaterComponent();
	if (!Water)
	{
		Notify(LOCTEXT("NoBodyToSave", "MobWater: select a body of water first."), false);
		return;
	}

	FSaveAssetDialogConfig Config;
	Config.DialogTitleOverride = LOCTEXT("SaveLookTitle", "Save Water Look Preset");
	Config.DefaultPath = TEXT("/Game");
	Config.DefaultAssetName = TEXT("WL_Water");
	Config.AssetClassNames.Add(UMobWaterLookPreset::StaticClass()->GetClassPathName());
	Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;

	const FString Picked = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser")
		.Get().CreateModalSaveAssetDialog(Config);

	if (Picked.IsEmpty())
	{
		return;
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(Picked);
	const FString AssetName = FPackageName::ObjectPathToObjectName(Picked);

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Notify(LOCTEXT("SaveLookNoPackage", "MobWater: could not create that package."), false);
		return;
	}

	Package->FullyLoad();

	// Reused when it is already there, so a body pointed at a preset and saved again edits the one it
	// is using rather than leaving it behind and pointing at a copy.
	UMobWaterLookPreset* Preset = FindObject<UMobWaterLookPreset>(Package, *AssetName);
	const bool bCreated = Preset == nullptr;

	if (bCreated)
	{
		Preset = NewObject<UMobWaterLookPreset>(Package, *AssetName, RF_Public | RF_Standalone);
	}

	if (!Preset)
	{
		Notify(LOCTEXT("SaveLookNoAsset", "MobWater: could not create the preset."), false);
		return;
	}

	Preset->Modify();
	Water->CaptureLookPreset(Preset);

	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Preset);
	}

	Package->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;

	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());

	if (!UPackage::SavePackage(Package, Preset, *FileName, Args))
	{
		Notify(LOCTEXT("SaveLookFailed", "MobWater: the preset could not be written to disk."), false);
		return;
	}

	// The body is pointed at what it just produced, so the details panel shows where the look came
	// from and applying it again is a no-op rather than a surprise.
	{
		const FScopedTransaction Transaction(LOCTEXT("SaveLookTransaction", "Save Water Look Preset"));
		Water->Modify();
		Water->LookPreset = Preset;
	}

	FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser")
		.Get().SyncBrowserToAssets(TArray<UObject*>{ Preset });

	Notify(FText::Format(LOCTEXT("SaveLookDone", "MobWater: saved {0}."),
		FText::FromString(AssetName)), true);
}

void FMobWaterEditorModule::SelectOcean()
{
	UWorld* World = EditorWorld();
	if (!World || !GEditor)
	{
		return;
	}

	TArray<AActor*> Oceans;
	for (TActorIterator<AMobWaterOcean> It(World); It; ++It)
	{
		Oceans.Add(*It);
	}

	if (Oceans.Num() == 0)
	{
		Notify(LOCTEXT("NoOcean", "MobWater: there is no ocean in this level."), false);
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SelectOceanTransaction", "Select Ocean"));

	GEditor->SelectNone(false, true, false);
	for (AActor* Ocean : Oceans)
	{
		GEditor->SelectActor(Ocean, true, false);
	}
	GEditor->NoteSelectionChange();
}

bool FMobWaterEditorModule::HasRewindDebugger()
{
	FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	const TSharedPtr<FTabManager> Tabs = LevelEditor ? LevelEditor->GetLevelEditorTabManager() : nullptr;

	return Tabs.IsValid() && Tabs->HasTabSpawner(MobWaterRewindTabId);
}

void FMobWaterEditorModule::DebugOceanInRewindDebugger()
{
	// Selected in the level first. Without a recording there is no traced object to hand the debugger,
	// and its own picker offers whatever the level has selected - so a selected ocean is one click from
	// being debugged the moment a recording exists.
	SelectOcean();

	if (!HasRewindDebugger())
	{
		Notify(LOCTEXT("NoRewindDebugger",
			"MobWater: the Rewind Debugger is not here. Enable the Gameplay Insights plugin."), false);
		return;
	}

	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditor.GetLevelEditorTabManager()->TryInvokeTab(MobWaterRewindTabId);

#if OBJECT_TRACE_ENABLED
	IRewindDebugger* Debugger = IRewindDebugger::Instance();
	if (!Debugger || !Debugger->GetAnalysisSession())
	{
		return;
	}

	// The ocean in the world being recorded, which is the play world while one is running. The editor's
	// own copy is a different object and the recording has never heard of it.
	UWorld* World = GEditor && GEditor->PlayWorld ? ToRawPtr(GEditor->PlayWorld) : EditorWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AMobWaterOcean> It(World); It; ++It)
	{
		Debugger->SetObjectToDebug(RewindDebugger::FObjectId(FObjectTrace::GetObjectId(*It)));
		break;
	}
#endif
}

void FMobWaterEditorModule::OpenSelectedBodyMaterial()
{
	UMobWaterComponent* Water = SelectedWaterComponent();
	if (!Water)
	{
		Notify(LOCTEXT("NoBodySelected", "MobWater: select a body of water first."), false);
		return;
	}

	// What it is drawing with now, rather than what the settings say it should be. Those differ while
	// a variant is missing and the material has degraded to a nearer one, which is exactly when
	// somebody wants to look at it.
	UMaterialInterface* Material = Water->GetMaterial(0);
	if (!Material)
	{
		Notify(LOCTEXT("NoBodyMaterial",
			"MobWater: that body has no material. Run Set Up Water."), false);
		return;
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Material);
	}
}

void FMobWaterEditorModule::OpenCollection()
{
	if (UMaterialParameterCollection* Collection = GetDefault<UMobWaterSettings>()->ParameterCollection.LoadSynchronous())
	{
		if (GEditor)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Collection);
		}
	}
	else
	{
		Notify(LOCTEXT("NoCollection",
			"MobWater: the wave collection has not been authored yet. Generate Materials first."), false);
	}
}

void FMobWaterEditorModule::Notify(const FText& Message, bool bSuccess)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = bSuccess ? 4.f : 8.f;

	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

void FMobWaterEditorModule::OnMaterialMissing(EMobWaterShape Shape)
{
	FNotificationInfo Info(FText::Format(
		LOCTEXT("MissingMaterial",
			"MobWater: no material for {0} water, so it renders nothing. Generate them, or point the "
			"shape at your own instance in Project Settings."),
		FText::FromString(MobWaterShapeName(Shape))));

	Info.ExpireDuration = 12.f;
	Info.Hyperlink = FSimpleDelegate::CreateStatic(&FMobWaterEditorModule::GenerateMaterials);
	Info.HyperlinkText = LOCTEXT("MissingMaterialLink", "Generate Materials");

	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

bool FMobWaterEditorModule::IsPythonAvailable()
{
	return IPythonScriptPlugin::Get() && IPythonScriptPlugin::Get()->IsPythonAvailable();
}

bool FMobWaterEditorModule::RunPython(const FString& Snippet, const FText& DoneMessage)
{
	if (!IsPythonAvailable())
	{
		return false;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobWater"));
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString ScriptDir = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()), TEXT("Python")).Replace(TEXT("\\"), TEXT("/"));

	const FString Command = FString::Printf(
		TEXT("import sys\n")
		TEXT("p = r'%s'\n")
		TEXT("sys.path.append(p) if p not in sys.path else None\n")
		TEXT("%s\n"), *ScriptDir, *Snippet);

	const bool bOk = IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);

	// A traceback is not surfaced here on purpose. It is several lines wide and already in the log,
	// and a toast that tries to carry one is unreadable in both places.
	Notify(bOk ? DoneMessage
		: LOCTEXT("PythonFailed", "MobWater: failed. See the Output Log."), bOk);

	return bOk;
}

void FMobWaterEditorModule::GenerateMaterials()
{
	// The meshes first: the material is what a body of water is made of, but the surface is what it
	// is drawn on, and a generate that produced one without the other leaves nothing visible.
	UMobWaterMeshLibrary::BuildSurfaceMeshes();

	RunPython(
		TEXT("import importlib, mob_water_graph, author_water; ")
		TEXT("importlib.reload(mob_water_graph); importlib.reload(author_water); author_water.build_all()"),
		LOCTEXT("GenerateDone", "MobWater: materials authored. See the Output Log."));
}

void FMobWaterEditorModule::ReportCost()
{
	RunPython(
		TEXT("import importlib, mob_water_report; ")
		TEXT("importlib.reload(mob_water_report); mob_water_report.run()"),
		LOCTEXT("ReportDone", "MobWater: cost reported. See the Output Log."));
}

void FMobWaterEditorModule::BakeSpectrum()
{
	RunPython(
		TEXT("import importlib, mob_water_spectrum; ")
		TEXT("importlib.reload(mob_water_spectrum); mob_water_spectrum.run()"),
		LOCTEXT("BakeSpectrumDone", "MobWater: sea state baked. See the Output Log."));
}

void FMobWaterEditorModule::VerifyContract()
{
	RunPython(
		TEXT("import importlib, mob_water_verify; ")
		TEXT("importlib.reload(mob_water_verify); mob_water_verify.run()"),
		LOCTEXT("VerifyDone", "MobWater: contract verified. See the Output Log."));
}

IMPLEMENT_MODULE(FMobWaterEditorModule, MobWaterEditor)

#undef LOCTEXT_NAMESPACE
