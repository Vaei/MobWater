// Copyright (c) Jared Taylor

#include "MobWaterSubsystem.h"

#include "MobWaterComponent.h"
#include "MobWaterDisturbanceComponent.h"
#include "MobWaterExclusionComponent.h"
#include "MobWaterModule.h"
#include "MobWaterSettings.h"
#include "MobWaterSpectrum.h"
#include "MobWaterUnderwaterComponent.h"
#include "MobWaterWavePreset.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"


/**
 * How long one step of the ripple field is worth.
 *
 * Fixed, because wave speed, damping and how long foam lasts are all per step. Sixty a second is what
 * the defaults were tuned at, and every other rate is a different body of water.
 */
static constexpr float MobWaterRippleStep = 1.f / 60.f;

/**
 * Where flat sits in the field's signed channels, mirroring MOB_WATER_RIPPLE_NEUTRAL.
 *
 * The step is drawn by a UI domain material, and a UI material's output is clamped to 0..1 whatever
 * the target's format is, so both heights are biased into that range and an empty target is the
 * surface pushed as far down as it goes rather than still water.
 */
static constexpr float MobWaterFieldNeutral = 0.5f;

/**
 * How many pushes one step of the field can carry, mirroring author_ripples.STAMP_SLOTS.
 *
 * The stamps are parameters on one material pass rather than a draw each, so the count is fixed at
 * author time. Eight is a character, its wake and a few impacts landing on the same step.
 */
static constexpr int32 MobWaterStampSlots = 8;

/** Flat water, in stored terms. What a field has to start at and be cleared to. */
static const FLinearColor MobWaterFieldFlat(MobWaterFieldNeutral, MobWaterFieldNeutral, 0.f, 1.f);

/**
 * The names the collection carries.
 *
 * Centralised because the generator writes the same strings from Python, and a rename that reaches
 * one side and not the other produces water that renders perfectly flat with nothing in any log.
 */
namespace MobWaterParams
{
	static const FName Time = TEXT("Time");

	/** Wave count, amplitude scale, speed scale, choppiness scale, in that order. */
	static const FName WaveScales = TEXT("WaveScales");

	/** (Direction.x, Direction.y, Wavelength, Amplitude) per wave. */
	static const FName WaveA[MobWaterWaveConstants::MaxWaves] =
	{
		TEXT("WaveA0"), TEXT("WaveA1"), TEXT("WaveA2"), TEXT("WaveA3"),
		TEXT("WaveA4"), TEXT("WaveA5"), TEXT("WaveA6"), TEXT("WaveA7"),
	};

	/** (Steepness, PhaseOffset, unused, unused) per wave. */
	static const FName WaveB[MobWaterWaveConstants::MaxWaves] =
	{
		TEXT("WaveB0"), TEXT("WaveB1"), TEXT("WaveB2"), TEXT("WaveB3"),
		TEXT("WaveB4"), TEXT("WaveB5"), TEXT("WaveB6"), TEXT("WaveB7"),
	};

	/** Where the ripple field is: (OriginX, OriginY, Extent, 1 / Extent). */
	static const FName RippleArea = TEXT("RippleArea");

	/** (CentreX, CentreY, HalfX, HalfY) per exclusion volume. */
	static const FName ExclusionA[MOB_WATER_EXCLUSION_SLOTS] =
	{
		TEXT("ExclusionA0"), TEXT("ExclusionA1"), TEXT("ExclusionA2"), TEXT("ExclusionA3"),
	};

	/** (CosYaw, SinYaw, IsRectangular, Strength) per exclusion volume. */
	static const FName ExclusionB[MOB_WATER_EXCLUSION_SLOTS] =
	{
		TEXT("ExclusionB0"), TEXT("ExclusionB1"), TEXT("ExclusionB2"), TEXT("ExclusionB3"),
	};

	/** (Softness0, Softness1, Softness2, Softness3), because a float4 is cheaper than four scalars. */
	static const FName ExclusionSoftness = TEXT("ExclusionSoftness");

	/** The baked outlines, on the step material rather than the collection: they are textures. */
	static const FName MeshMask[MOB_WATER_MESH_EXCLUSION_SLOTS] =
	{
		TEXT("MeshMask0"), TEXT("MeshMask1"), TEXT("MeshMask2"), TEXT("MeshMask3"),
	};

	/** (CentreX, CentreY, HalfX, HalfY) per outline. */
	static const FName MeshExclusionA[MOB_WATER_MESH_EXCLUSION_SLOTS] =
	{
		TEXT("MeshA0"), TEXT("MeshA1"), TEXT("MeshA2"), TEXT("MeshA3"),
	};

	/** (CosYaw, SinYaw, SpanOverSoftness, Strength) per outline. */
	static const FName MeshExclusionB[MOB_WATER_MESH_EXCLUSION_SLOTS] =
	{
		TEXT("MeshB0"), TEXT("MeshB1"), TEXT("MeshB2"), TEXT("MeshB3"),
	};

	/** Where the outline window is: (OriginX, OriginY, Extent, 1 / Extent). */
	static const FName ExclusionArea = TEXT("ExclusionArea");

	/** Which way the sun is going, so the surface knows where to put its glint. */
	static const FName SunDirection = TEXT("SunDirection");
	static const FName SunColor = TEXT("SunColor");

	/** (Intensity, Rotation in turns, unused, unused) for the reflected sky. */
	static const FName ReflectionParams = TEXT("ReflectionParams");

	/** (TileSize, LoopPeriod, Resolution, Frames) of the baked sea state. */
	static const FName SpectrumParams = TEXT("SpectrumParams");

	/** (HorizontalScale, VerticalScale, NormalScale, AtlasColumns) of the same. */
	static const FName SpectrumScale = TEXT("SpectrumScale");

	/** (U, V, Radius in UV, Strength) per push waiting on the field. An unused slot is all zero. */
	static const FName Stamp[MobWaterStampSlots] =
	{
		TEXT("S0"), TEXT("S1"), TEXT("S2"), TEXT("S3"),
		TEXT("S4"), TEXT("S5"), TEXT("S6"), TEXT("S7"),
	};

	/** Four stamps' foam each, because a float4 is cheaper than eight scalars. */
	static const FName StampFoamA = TEXT("FoamA");
	static const FName StampFoamB = TEXT("FoamB");
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GMobWaterDebug(
	TEXT("mob.Water.Debug"),
	TEXT("Dumps the water subsystem's clock and everything it publishes."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (const UMobWaterSubsystem* Subsystem = World ? World->GetSubsystem<UMobWaterSubsystem>() : nullptr)
		{
			Subsystem->DumpState();
		}
		else
		{
			UE_LOG(LogMobWater, Warning, TEXT("No water subsystem in this world."));
		}
	}));
#endif

UMobWaterSubsystem* UMobWaterSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UMobWaterSubsystem>() : nullptr;
}

bool UMobWaterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

void UMobWaterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();
	if (const TSoftObjectPtr<UMobWaterWavePreset>* Found = Settings->DefaultWavePresets.Find(EMobWaterShape::Disc))
	{
		if (const UMobWaterWavePreset* Preset = Found->LoadSynchronous())
		{
			DefaultWaves = Preset->Waves;
			bWavesDirty = true;
		}
	}
}

void UMobWaterSubsystem::Deinitialize()
{
	TimeSource.Unbind();

	Super::Deinitialize();
}

TStatId UMobWaterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMobWaterSubsystem, STATGROUP_Tickables);
}

void UMobWaterSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	++TickCount;

	TickClock(DeltaTime);

	// The clock moves every frame and the wave set almost never does, so they are published apart. The
	// set is seventeen vector writes; paying for them every frame to re-send values nobody changed is
	// the sort of cost that does not show up in a profile as anything but a flat tax.
	SetScalar(MobWaterParams::Time, WaterTime);

	if (bWavesDirty)
	{
		PublishWaves(DefaultWaves);
		bWavesDirty = false;
	}

	if (bSpectrumDirty)
	{
		PublishSpectrum();
		bSpectrumDirty = false;
	}

	TickSun();
	PublishExclusions();
	TickRipples(DeltaTime);
	TickMeshExclusions();
	TickUnderwater();
}

void UMobWaterSubsystem::TickUnderwater()
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	// Nothing until there is water to be under. A level with none never builds the plane, and one that
	// gains a body later gets it on the frame the body registers.
	if (!Settings->bAutoUnderwater || Bodies.Num() == 0 || IsRunningDedicatedServer())
	{
		return;
	}

	UClass* Class = Settings->UnderwaterComponent.LoadSynchronous();
	if (!Class)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (!Controller || !Controller->IsLocalController())
		{
			continue;
		}

		APlayerCameraManager* Camera = Controller->PlayerCameraManager;
		if (!Camera || Camera->GetComponentByClass(UMobWaterUnderwaterComponent::StaticClass()))
		{
			continue;
		}

		// On the camera manager, not on the pawn or its camera component: the manager's own transform
		// is the point of view after every modifier, shake and lag has had it, and a plane held in
		// front of anything earlier than that is a plane the view can end up behind.
		UMobWaterUnderwaterComponent* Plane = NewObject<UMobWaterUnderwaterComponent>(Camera, Class);
		if (Plane)
		{
			Plane->SetupAttachment(Camera->GetRootComponent());
			Plane->RegisterComponent();
		}
	}
}

void UMobWaterSubsystem::TickSun()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!SunLight.IsValid())
	{
		// Re-found when it goes away, which is what a level transition is.
		float Brightest = -1.f;

		for (TActorIterator<ADirectionalLight> It(const_cast<UWorld*>(World)); It; ++It)
		{
			ADirectionalLight* Light = *It;
			const ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr;

			if (Component && Component->IsVisible() && Component->Intensity > Brightest)
			{
				Brightest = Component->Intensity;
				SunLight = Light;
			}
		}
	}

	FVector Direction = FVector(0.f, 0.f, -1.f);
	FLinearColor Colour = FLinearColor::White;

	if (const ADirectionalLight* Light = SunLight.Get())
	{
		if (const ULightComponent* Component = Light->GetLightComponent())
		{
			Direction = Component->GetDirection();
			Colour = Component->GetLightColor() * Component->Intensity;
		}
	}

	// Only on the frames it changes. A still sun is one comparison rather than two writes, and the
	// sun is still on almost every frame of almost every game.
	if (!Direction.Equals(LastSunDirection, 0.001) || !Colour.Equals(LastSunColor, 0.001f))
	{
		LastSunDirection = Direction;
		LastSunColor = Colour;

		SetVector(MobWaterParams::SunDirection, FLinearColor(
			static_cast<float>(Direction.X), static_cast<float>(Direction.Y),
			static_cast<float>(Direction.Z), 0.f));
		SetVector(MobWaterParams::SunColor, Colour);
	}
}

bool UMobWaterSubsystem::GetViewLocation(FVector& OutLocation, FVector* OutForward) const
{
	if (OutForward)
	{
		*OutForward = FVector::ZeroVector;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator Rotation;
		PC->GetPlayerViewPoint(OutLocation, Rotation);

		if (OutForward)
		{
			const FVector Facing = Rotation.Vector();
			*OutForward = FVector(Facing.X, Facing.Y, 0.0).GetSafeNormal();
		}

		// What the camera is looking at, when that is something other than the camera itself. A field
		// centred on the eye is centred on nothing in a game whose camera stands twenty metres back,
		// and the thing actually standing in the water is outside it - which reads as ripples being
		// broken rather than as the field being somewhere else.
		if (const AActor* Target = PC->GetViewTarget())
		{
			if (Target != PC && !Target->IsA<APlayerCameraManager>())
			{
				OutLocation = Target->GetActorLocation();

				if (OutForward)
				{
					*OutForward = FVector::ZeroVector;
				}
			}
		}

		return true;
	}

	// What the renderer drew from last frame. This is what makes the field follow an editor viewport
	// with nothing playing, and it costs the runtime module no dependency on the editor to do it -
	// asking a viewport client directly would drag UnrealEd into a module that ships.
	if (World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		OutLocation = World->ViewLocationsRenderedLastFrame[0];
		return true;
	}

	return false;
}

bool UMobWaterSubsystem::GetRippleField(FVector2D& OutOrigin, float& OutExtent) const
{
	OutOrigin = FieldOrigin;
	OutExtent = GetDefault<UMobWaterSettings>()->RippleExtent;
	return bFieldValid;
}

void UMobWaterSubsystem::RegisterBody(UMobWaterComponent* Body)
{
	if (Body)
	{
		Bodies.AddUnique(Body);
	}
}

void UMobWaterSubsystem::UnregisterBody(UMobWaterComponent* Body)
{
	Bodies.RemoveAll([Body](const TWeakObjectPtr<UMobWaterComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Body;
	});
}

UMobWaterComponent* UMobWaterSubsystem::FindBodyAt(const FVector& Location) const
{
	// Height first, and depth only to break a tie. Depth alone is right for two bodies drawn at the
	// same level - a shallow trough across a lake is decoration on it - and badly wrong the moment
	// they are at different heights: a rock pool on a headland sits inside an ocean's footprint, and
	// picking the deeper of the two answers about the sea two hundred metres below. Everything put in
	// the pool then falls through it.
	UMobWaterComponent* Under = nullptr;
	double UnderZ = -TNumericLimits<double>::Max();

	UMobWaterComponent* Over = nullptr;
	double OverZ = TNumericLimits<double>::Max();

	for (const TWeakObjectPtr<UMobWaterComponent>& Entry : Bodies)
	{
		UMobWaterComponent* Body = Entry.Get();
		if (!Body || !Body->ContainsLocation(Location))
		{
			continue;
		}

		// The still surface, not the waved one. A point a wave is about to reach is in that body
		// already, and a rule that changed its mind as a crest passed would flicker.
		const double SurfaceZ = Body->GetComponentLocation().Z;

		if (SurfaceZ >= Location.Z)
		{
			// The point is under this one. The lowest such surface is the water it is actually in.
			if (SurfaceZ < UnderZ || !Under || (SurfaceZ == UnderZ && Under && Body->Depth > Under->Depth))
			{
				UnderZ = SurfaceZ;
				Under = Body;
			}
		}
		else if (SurfaceZ > OverZ || !Over || (SurfaceZ == OverZ && Over && Body->Depth > Over->Depth))
		{
			// Nothing is over it yet. The highest surface below is the one it would fall into.
			OverZ = SurfaceZ;
			Over = Body;
		}
	}

	return Under ? Under : Over;
}

void UMobWaterSubsystem::RegisterExclusion(UMobWaterExclusionComponent* Exclusion)
{
	if (Exclusion)
	{
		Exclusions.AddUnique(Exclusion);
	}
}

void UMobWaterSubsystem::UnregisterExclusion(UMobWaterExclusionComponent* Exclusion)
{
	Exclusions.RemoveAll([Exclusion](const TWeakObjectPtr<UMobWaterExclusionComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Exclusion;
	});
}

float UMobWaterSubsystem::GetExclusionAtLocation(const UObject* WorldContextObject, const FVector& Location)
{
	const UMobWaterSubsystem* Subsystem = Get(WorldContextObject);
	return Subsystem ? Subsystem->GetExclusionAt(Location) : 0.f;
}

float UMobWaterSubsystem::GetExclusionAt(const FVector& Location) const
{
	float Most = 0.f;
	for (const TWeakObjectPtr<UMobWaterExclusionComponent>& Entry : Exclusions)
	{
		if (const UMobWaterExclusionComponent* Exclusion = Entry.Get())
		{
			if (Exclusion->bBlocksSubmersion)
			{
				Most = FMath::Max(Most, Exclusion->GetExclusionAt(Location));

				if (Most >= 1.f)
				{
					// Nothing left to remove, and a hull in a marina is several volumes deep.
					return 1.f;
				}
			}
		}
	}

	return Most;
}

void UMobWaterSubsystem::PublishExclusions()
{
	FVector ViewLocation;
	const bool bHasView = GetViewLocation(ViewLocation);

	TArray<UMobWaterExclusionComponent*> Live;
	Live.Reserve(Exclusions.Num());

	for (const TWeakObjectPtr<UMobWaterExclusionComponent>& Entry : Exclusions)
	{
		if (UMobWaterExclusionComponent* Exclusion = Entry.Get())
		{
			// A baked outline goes into the field instead, and taking a slot as well would carve its
			// bounding rectangle out from under the shape it just cut.
			if (Exclusion->IsActive() && Exclusion->Strength > 0.f && !Exclusion->IsMesh())
			{
				Live.Add(Exclusion);
			}
		}
	}

	if (bHasView && Live.Num() > MOB_WATER_EXCLUSION_SLOTS)
	{
		// Nearest first, so the ones you are standing among are the ones that get a slot.
		Live.Sort([ViewLocation](const UMobWaterExclusionComponent& A, const UMobWaterExclusionComponent& B)
		{
			return FVector::DistSquared2D(A.GetComponentLocation(), ViewLocation)
				< FVector::DistSquared2D(B.GetComponentLocation(), ViewLocation);
		});
	}

	if (Live.Num() > MOB_WATER_EXCLUSION_SLOTS && !bWarnedExclusionOverflow)
	{
		bWarnedExclusionOverflow = true;
		UE_LOG(LogMobWater, Warning,
			TEXT("MobWater: %d exclusion volumes are active and only %d can be rendered. The nearest "
				 "to the view win; the rest hold no water back."),
			Live.Num(), MOB_WATER_EXCLUSION_SLOTS);
	}

	FLinearColor Softness = FLinearColor::Black;

	for (int32 Slot = 0; Slot < MOB_WATER_EXCLUSION_SLOTS; ++Slot)
	{
		FLinearColor A = FLinearColor(0.f, 0.f, 1.f, 1.f);
		FLinearColor B = FLinearColor::Transparent;
		float Soft = 1.f;

		if (Live.IsValidIndex(Slot))
		{
			Live[Slot]->PackForShader(A, B);
			Soft = FMath::Max(Live[Slot]->EdgeSoftness, 1.f);
		}

		SetVector(MobWaterParams::ExclusionA[Slot], A);
		SetVector(MobWaterParams::ExclusionB[Slot], B);

		Softness.Component(Slot) = Soft;
	}

	SetVector(MobWaterParams::ExclusionSoftness, Softness);
}

void UMobWaterSubsystem::TickMeshExclusions()
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	UTextureRenderTarget2D* Target = Settings->ExclusionTarget.LoadSynchronous();
	UMaterialInterface* Draw = Settings->ExclusionFieldMaterial.LoadSynchronous();

	if (!Target || !Draw)
	{
		return;
	}

#if WITH_EDITOR
	// The one target is shared by every world that exists at once, and an editor world keeps ticking
	// while play is in progress. Same reason the ripple field defers.
	if (GetWorld()->WorldType == EWorldType::Editor && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				return;
			}
		}
	}
#endif

	FVector ViewLocation;
	FVector ViewForward;
	if (!GetViewLocation(ViewLocation, &ViewForward))
	{
		return;
	}

	const float Extent = FMath::Max(Settings->RippleExtent, 100.f);
	const float Texel = Extent / FMath::Max(Target->SizeX, 1);

	// Snapped to whole texels, so a window that follows the view does not resample an outline into a
	// crawling edge as the camera moves.
	const FVector Centre = ViewLocation + ViewForward * (Extent * 0.25f);
	ExclusionOrigin = FVector2D(
		FMath::GridSnap(Centre.X, Texel),
		FMath::GridSnap(Centre.Y, Texel));

	SetVector(MobWaterParams::ExclusionArea, FLinearColor(
		static_cast<float>(ExclusionOrigin.X), static_cast<float>(ExclusionOrigin.Y),
		Extent, 1.f / Extent));

	if (!ExclusionMaterial || ExclusionMaterial->Parent != Draw)
	{
		ExclusionMaterial = UMaterialInstanceDynamic::Create(Draw, this);
	}

	if (!ExclusionMaterial)
	{
		return;
	}

	const int32 Found = PublishMeshExclusions(ExclusionMaterial, ExclusionOrigin, Extent);

	if (Found == 0)
	{
		// Cleared once rather than redrawn empty every frame. A level with no baked outline in it -
		// which is most of them - should not be paying for a pass that answers zero.
		if (bExclusionFieldDrawn)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), Target, FLinearColor::Black);
			bExclusionFieldDrawn = false;
		}
		return;
	}

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), Target, ExclusionMaterial);
	bExclusionFieldDrawn = true;
}

int32 UMobWaterSubsystem::PublishMeshExclusions(UMaterialInstanceDynamic* Material,
	const FVector2D& Origin, float Extent) const
{
	if (!Material)
	{
		return 0;
	}

	Material->SetVectorParameterValue(MobWaterParams::ExclusionArea, FLinearColor(
		static_cast<float>(Origin.X), static_cast<float>(Origin.Y), Extent, 1.f / Extent));

	TArray<UMobWaterExclusionComponent*> Live;

	for (const TWeakObjectPtr<UMobWaterExclusionComponent>& Entry : Exclusions)
	{
		UMobWaterExclusionComponent* Exclusion = Entry.Get();
		if (!Exclusion || !Exclusion->IsActive() || Exclusion->Strength <= 0.f || !Exclusion->IsMesh())
		{
			continue;
		}

		// Anything wholly outside the field cannot mark it, and dropping it here leaves the slot for
		// an outline that can. The field is a square Extent across, centred on its origin.
		const FVector2D Offset = FVector2D(Exclusion->GetComponentLocation()) - Origin;
		const FVector2D Reach = Exclusion->GetWorldExtent() + FVector2D(Extent * 0.5, Extent * 0.5);

		if (FMath::Abs(Offset.X) > Reach.X || FMath::Abs(Offset.Y) > Reach.Y)
		{
			continue;
		}

		Live.Add(Exclusion);
	}

	if (Live.Num() > MOB_WATER_MESH_EXCLUSION_SLOTS)
	{
		// Largest first rather than nearest. An outline is only in this list at all because it
		// reaches the field, and the one that carves most of it is the one worth keeping.
		Live.Sort([](const UMobWaterExclusionComponent& A, const UMobWaterExclusionComponent& B)
		{
			return A.GetWorldExtent().SizeSquared() > B.GetWorldExtent().SizeSquared();
		});
	}

	for (int32 Slot = 0; Slot < MOB_WATER_MESH_EXCLUSION_SLOTS; ++Slot)
	{
		FLinearColor A = FLinearColor(0.f, 0.f, 1.f, 1.f);
		FLinearColor B = FLinearColor::Transparent;

		if (Live.IsValidIndex(Slot))
		{
			Live[Slot]->PackMeshForShader(A, B);

			if (UTexture2D* Mask = Live[Slot]->GetSilhouetteTexture())
			{
				Material->SetTextureParameterValue(MobWaterParams::MeshMask[Slot], Mask);
			}
			else
			{
				B.A = 0.f;
			}
		}

		Material->SetVectorParameterValue(MobWaterParams::MeshExclusionA[Slot], A);
		Material->SetVectorParameterValue(MobWaterParams::MeshExclusionB[Slot], B);
	}

	return FMath::Min(Live.Num(), MOB_WATER_MESH_EXCLUSION_SLOTS);
}

void UMobWaterSubsystem::RegisterDisturber(UMobWaterDisturbanceComponent* Disturber)
{
	if (Disturber)
	{
		Disturbers.AddUnique(Disturber);
	}
}

void UMobWaterSubsystem::UnregisterDisturber(UMobWaterDisturbanceComponent* Disturber)
{
	Disturbers.RemoveAll([Disturber](const TWeakObjectPtr<UMobWaterDisturbanceComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Disturber;
	});
}

void UMobWaterSubsystem::AddRipple(const UObject* WorldContextObject, const FVector& Location,
	float Radius, float Strength)
{
	if (UMobWaterSubsystem* Subsystem = Get(WorldContextObject))
	{
		Subsystem->PendingRipples.Add(FVector4(Location.X, Location.Y, Radius, Strength));
	}
}

void UMobWaterSubsystem::ClearRipples()
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	if (UTextureRenderTarget2D* Target = Settings->RippleTarget.LoadSynchronous())
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), Target, MobWaterFieldFlat);
	}
	if (UTextureRenderTarget2D* History = Settings->RippleHistory.LoadSynchronous())
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), History, MobWaterFieldFlat);
	}
}

void UMobWaterSubsystem::TickRipples(float DeltaTime)
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	if (!Settings->bRipplesEnabled)
	{
		return;
	}

#if WITH_EDITOR
	// The field is one pair of render target assets, shared by every world that exists at once. An
	// editor world keeps ticking while play is in progress, so both would step the same targets with
	// their own origins - each scrolling the other's field out from under it, which leaves the water
	// holding whatever survived being resampled twice from two places and never settles.
	if (GetWorld()->WorldType == EWorldType::Editor && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				return;
			}
		}
	}
#endif

	UTextureRenderTarget2D* Target = Settings->RippleTarget.LoadSynchronous();
	UTextureRenderTarget2D* History = Settings->RippleHistory.LoadSynchronous();
	UMaterialInterface* Step = Settings->RippleStepMaterial.LoadSynchronous();
	UMaterialInterface* Copy = Settings->RippleCopyMaterial.LoadSynchronous();
	UMaterialInterface* Stamp = Settings->RippleStampMaterial.LoadSynchronous();

	if (!Target || !History || !Step || !Copy || !Stamp)
	{
		return;
	}

	FVector ViewLocation;
	FVector ViewForward;
	if (!GetViewLocation(ViewLocation, &ViewForward))
	{
		return;
	}

	const float Extent = FMath::Max(Settings->RippleExtent, 100.f);
	const float Texel = Extent / FMath::Max(Target->SizeX, 1);

	// Pushed forward down the view rather than centred on the eye. The extent is the field's whole
	// width, so it only reaches half of it from the middle, and a third person camera stands several
	// metres behind whatever is standing in the water - centred on the eye, the thing making the
	// ripples is at the border where they are faded out, and the water reads as having no ripples.
	const FVector Centre = ViewLocation + ViewForward * (Extent * 0.25f);

	// Snapped to whole texels. A field that scrolls by fractions is resampled every frame, and a
	// wave equation reading its own resampled output smears itself into a blur within a second.
	const FVector2D Wanted(
		FMath::GridSnap(Centre.X, Texel),
		FMath::GridSnap(Centre.Y, Texel));

	// Primed before anything is allowed to sample it, and before the first step is owed rather than
	// on it. The targets are authored with a flat clear colour so they load correct, and this is what
	// covers the case where something else has been drawing into them - the editor world before play,
	// or a previous level. A field left holding its zero is the surface pushed as far down as the
	// range goes, and the step drags the border back to flat while the middle is still railed: that
	// step in the surface travels inward as a square shockwave lasting about a second, seen once, on
	// the first water anything looks at.
	if (!bFieldValid)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), Target, MobWaterFieldFlat);
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), History, MobWaterFieldFlat);

		FieldOrigin = Wanted;
		bFieldValid = true;
	}

	// Advanced at a fixed rate, never once per frame. Wave speed, damping and how long foam lasts are
	// all per step, so a field stepped every frame is a different body of water at 120 fps than it is
	// at 60 - and the one the machine happens to run at is the one nobody tuned. Pending ripples are
	// left alone on a frame that does not step, so nothing that asked for a push loses it.
	RippleAccumulator += DeltaTime;

	const bool bStepping = RippleAccumulator >= MobWaterRippleStep;

	// Capped rather than caught up. A hitch is a hitch; replaying a second of ripples afterwards is
	// slower again and looks like the water briefly running at ten times speed.
	int32 Steps = 0;
	FVector2D Delta = FVector2D::ZeroVector;

	if (bStepping)
	{
		Steps = FMath::Min(FMath::FloorToInt(RippleAccumulator / MobWaterRippleStep), 2);
		RippleAccumulator = FMath::Min(RippleAccumulator - Steps * MobWaterRippleStep, MobWaterRippleStep);

		Delta = Wanted - FieldOrigin;
		FieldOrigin = Wanted;
	}

	// Published every frame, stepping or not, because the surface reads it every frame. Left until
	// after the first step is due, a body of water spends its first frames sampling the field through
	// whatever area the last level published - which puts the ripples somewhere else entirely.
	SetVector(MobWaterParams::RippleArea, FLinearColor(
		static_cast<float>(FieldOrigin.X), static_cast<float>(FieldOrigin.Y), Extent, 1.f / Extent));

	if (!bStepping)
	{
		return;
	}

	if (!StepMaterial || StepMaterial->Parent != Step)
	{
		StepMaterial = UMaterialInstanceDynamic::Create(Step, this);
	}
	if (!CopyMaterial || CopyMaterial->Parent != Copy)
	{
		CopyMaterial = UMaterialInstanceDynamic::Create(Copy, this);
	}
	if (!StampMaterial || StampMaterial->Parent != Stamp)
	{
		StampMaterial = UMaterialInstanceDynamic::Create(Stamp, this);
	}

	if (!StepMaterial || !CopyMaterial || !StampMaterial)
	{
		return;
	}

	StepMaterial->SetTextureParameterValue(TEXT("History"), History);
	StepMaterial->SetScalarParameterValue(TEXT("Speed"), Settings->RippleSpeed);
	StepMaterial->SetScalarParameterValue(TEXT("Damping"), Settings->RippleDamping);
	StepMaterial->SetScalarParameterValue(TEXT("TexelSize"), 1.f / FMath::Max(Target->SizeX, 1));
	CopyMaterial->SetTextureParameterValue(TEXT("Source"), Target);
	StampMaterial->SetTextureParameterValue(TEXT("Source"), Target);

	for (int32 Index = 0; Index < Steps; ++Index)
	{
		// The view moved once, however many steps that frame is worth, so only the first one carries
		// the scroll. Scrolling on both would move the water twice as far as the camera went.
		const FVector2D Scroll = Index == 0 ? Delta : FVector2D::ZeroVector;

		StepMaterial->SetVectorParameterValue(TEXT("Scroll"), FLinearColor(
			static_cast<float>(Scroll.X / Extent), static_cast<float>(Scroll.Y / Extent), 0.f, 0.f));

		UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), Target, StepMaterial);

		// The pushes go in on the way back into the history, and on the last step only - applying them
		// once per step would put a frame's worth of ripples in twice whenever a hitch owed the field
		// two. The history is what the next step reads, so a stamp landing there reaches the surface
		// on the following step, which at sixty a second is not a delay anyone can see.
		if (Index < Steps - 1 || !StampDisturbers(Target, History, Steps * MobWaterRippleStep))
		{
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), History, CopyMaterial);
		}
	}

	UE_LOG(LogMobWater, Verbose, TEXT("Field stepped %d in %s, origin (%.0f, %.0f), %d stamped"),
		Steps, *GetWorld()->GetName(), FieldOrigin.X, FieldOrigin.Y, LastStampCount);
}

bool UMobWaterSubsystem::StampDisturbers(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Into, float DeltaTime)
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	struct FStamp
	{
		FVector2D Location;
		float Radius;
		float Strength;
		float Foam;
	};

	TArray<FStamp> Stamps;

	for (const TWeakObjectPtr<UMobWaterDisturbanceComponent>& Entry : Disturbers)
	{
		UMobWaterDisturbanceComponent* Disturber = Entry.Get();
		if (!Disturber)
		{
			continue;
		}

		FVector Location;
		float Strength;
		if (Disturber->ShouldStamp(DeltaTime, Location, Strength))
		{
			Stamps.Add({ FVector2D(Location.X, Location.Y), Disturber->Radius, Strength, Disturber->Foam });
		}
	}

	for (const FVector4& Pending : PendingRipples)
	{
		// A one-shot push leaves foam in proportion to how hard it was. Something dropped in a pond
		// breaks the surface where it landed, and something that barely touched it does not.
		const float Strength = static_cast<float>(Pending.W);

		Stamps.Add({ FVector2D(Pending.X, Pending.Y), static_cast<float>(Pending.Z), Strength,
			FMath::Clamp(FMath::Abs(Strength) * 0.2f, 0.f, 1.f) });
	}
	PendingRipples.Reset();

	const float Extent = FMath::Max(Settings->RippleExtent, 100.f);

	// A stamp is placed in the field's own UV, and one that falls outside it entirely has nothing to
	// push. Dropped here rather than in the shader so it does not cost a slot something visible wants.
	const float Margin = 0.1f;
	Stamps.RemoveAll([this, Extent, Margin](const FStamp& Entry)
	{
		const FVector2D Local = (Entry.Location - FieldOrigin) / Extent + FVector2D(0.5, 0.5);
		return Local.X < -Margin || Local.X > 1.f + Margin || Local.Y < -Margin || Local.Y > 1.f + Margin;
	});

	LastStampCount = Stamps.Num();

	if (Stamps.Num() == 0)
	{
		return false;
	}

	// Strongest first, so an overflow drops the pushes nobody was going to see rather than whichever
	// happened to be registered last.
	Stamps.Sort([](const FStamp& A, const FStamp& B) { return FMath::Abs(A.Strength) > FMath::Abs(B.Strength); });

	if (Stamps.Num() > MobWaterStampSlots && !bWarnedStampOverflow)
	{
		bWarnedStampOverflow = true;
		UE_LOG(LogMobWater, Warning,
			TEXT("MobWater: %d disturbances landed on one step and only %d fit. The strongest win; "
				 "the rest push nothing."),
			Stamps.Num(), MobWaterStampSlots);
	}

	FLinearColor Foam[2] = { FLinearColor::Transparent, FLinearColor::Transparent };

	for (int32 Slot = 0; Slot < MobWaterStampSlots; ++Slot)
	{
		FLinearColor Packed = FLinearColor::Transparent;

		if (Stamps.IsValidIndex(Slot))
		{
			const FStamp& Entry = Stamps[Slot];
			const FVector2D Local = (Entry.Location - FieldOrigin) / Extent + FVector2D(0.5, 0.5);

			// Strength in the alpha, and it is an impulse rather than a height: what makes one push
			// harder than another is how much is added, not how far across it reaches.
			Packed = FLinearColor(
				static_cast<float>(Local.X), static_cast<float>(Local.Y),
				Entry.Radius / Extent, Entry.Strength);

			// Foam is written here rather than worked out from how fast the surface is moving. Foam
			// from movement appears everywhere the ripple travels, so what should be a wake behind
			// something becomes a white plume spreading across the whole pond in front of it.
			Foam[Slot / 4].Component(Slot % 4) = Entry.Foam;
		}

		StampMaterial->SetVectorParameterValue(MobWaterParams::Stamp[Slot], Packed);
	}

	StampMaterial->SetVectorParameterValue(MobWaterParams::StampFoamA, Foam[0]);
	StampMaterial->SetVectorParameterValue(MobWaterParams::StampFoamB, Foam[1]);
	StampMaterial->SetTextureParameterValue(TEXT("Source"), Source);

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), Into, StampMaterial);

	return true;
}

void UMobWaterSubsystem::TickClock(float DeltaTime)
{
	LocalTime += DeltaTime;

	if (TimeSource.IsBound())
	{
		RawWaterTime = TimeSource.Execute();
	}
	else if (const UWorld* World = GetWorld())
	{
		// Good enough to see water move, and not good enough to keep two machines in phase. The
		// header on FMobWaterTimeSource says why, and a project that cares binds its own.
		const AGameStateBase* GameState = World->GetGameState();
		RawWaterTime = GameState ? GameState->GetServerWorldTimeSeconds() : LocalTime;
	}
	else
	{
		RawWaterTime = LocalTime;
	}

	const float Period = FMath::Max(GetDefault<UMobWaterSettings>()->TimeLoopPeriod, 1.f);

	// Folded in double and handed on as float. Doing it the other way round is the bug this exists to
	// avoid: a large double cast to float has already lost the fraction the fold was meant to keep.
	const double Folded = RawWaterTime - Period * FMath::FloorToDouble(RawWaterTime / Period);
	WaterTime = static_cast<float>(Folded);
}

void UMobWaterSubsystem::SetDefaultWavePreset(UMobWaterWavePreset* Preset)
{
	DefaultWaves = Preset ? Preset->Waves : FMobWaterWaveParams();
	bWavesDirty = true;
}

UMaterialParameterCollection* UMobWaterSubsystem::GetCollection() const
{
	return GetDefault<UMobWaterSettings>()->ParameterCollection.LoadSynchronous();
}

void UMobWaterSubsystem::SetScalar(FName Name, float Value) const
{
	if (UMaterialParameterCollection* Collection = GetCollection())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), Collection, Name, Value);
	}
}

void UMobWaterSubsystem::SetVector(FName Name, const FLinearColor& Value) const
{
	if (UMaterialParameterCollection* Collection = GetCollection())
	{
		UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), Collection, Name, Value);
	}
}

void UMobWaterSubsystem::PublishWaves(const FMobWaterWaveParams& Params) const
{
	if (!GetCollection())
	{
		return;
	}

	SetScalar(MobWaterParams::Time, WaterTime);

	const int32 Count = FMath::Min(Params.Waves.Num(), MobWaterWaveConstants::MaxWaves);

	SetVector(MobWaterParams::WaveScales, FLinearColor(
		static_cast<float>(Count),
		Params.AmplitudeScale,
		Params.SpeedScale,
		Params.ChoppinessScale));

	for (int32 Index = 0; Index < MobWaterWaveConstants::MaxWaves; ++Index)
	{
		// Every slot is written, including the empty ones. A slot left holding the previous set's wave
		// is a wave the shader still evaluates whenever the count goes back up, and it arrives with no
		// relationship to the set around it.
		if (Index < Count)
		{
			const FMobGerstnerWave& Wave = Params.Waves[Index];
			const FVector2f Dir = Wave.Direction.GetSafeNormal();

			SetVector(MobWaterParams::WaveA[Index], FLinearColor(Dir.X, Dir.Y, Wave.Wavelength, Wave.Amplitude));
			SetVector(MobWaterParams::WaveB[Index], FLinearColor(Wave.Steepness, Wave.PhaseOffset, 0.f, 0.f));
		}
		else
		{
			SetVector(MobWaterParams::WaveA[Index], FLinearColor(0.f, 0.f, 100.f, 0.f));
			SetVector(MobWaterParams::WaveB[Index], FLinearColor::Transparent);
		}
	}
}

void UMobWaterSubsystem::SetSpectrum(const UMobWaterSpectrum* InSpectrum)
{
	if (Spectrum.Get() == InSpectrum)
	{
		return;
	}

	if (InSpectrum && Spectrum.IsValid() && !bWarnedSpectrumConflict)
	{
		bWarnedSpectrumConflict = true;
		UE_LOG(LogMobWater, Warning,
			TEXT("Two oceans on two sea states (%s and %s). The collection holds one layout, so both ")
			TEXT("draw the second and only the query tells them apart. Put them on one spectrum."),
			*GetNameSafe(Spectrum.Get()), *GetNameSafe(InSpectrum));
	}

	Spectrum = InSpectrum;
	bSpectrumDirty = true;
}

void UMobWaterSubsystem::PublishSpectrum() const
{
	if (!GetCollection())
	{
		return;
	}

	const UMobWaterSpectrum* Baked = Spectrum.Get();

	// A tile of zero would divide by zero in the shader, so an absent sea state is published as a
	// field with no height in it rather than as nothing at all.
	if (!Baked || !Baked->IsUsable())
	{
		SetVector(MobWaterParams::SpectrumParams, FLinearColor(1024.f, 1.f, 4.f, 2.f));
		SetVector(MobWaterParams::SpectrumScale, FLinearColor(0.f, 0.f, 0.f, 1.f));
		return;
	}

	SetVector(MobWaterParams::SpectrumParams, FLinearColor(
		Baked->TileSize,
		Baked->LoopPeriod,
		static_cast<float>(Baked->Resolution),
		static_cast<float>(Baked->Frames)));

	SetVector(MobWaterParams::SpectrumScale, FLinearColor(
		Baked->HorizontalScale,
		Baked->VerticalScale,
		Baked->NormalScale,
		static_cast<float>(FMath::Max(Baked->AtlasColumns, 1))));
}

void UMobWaterSubsystem::SetReflection(float Intensity, float Rotation)
{
	SetVector(MobWaterParams::ReflectionParams,
		FLinearColor(FMath::Max(Intensity, 0.f), Rotation, 0.f, 0.f));
}

void UMobWaterSubsystem::DumpState() const
{
	UE_LOG(LogMobWater, Display, TEXT("MobWater: ticks %llu, time %.3f (raw %.3f), source %s"),
		TickCount, WaterTime, RawWaterTime, TimeSource.IsBound() ? TEXT("bound") : TEXT("engine fallback"));

	const UMaterialParameterCollection* Collection = GetCollection();
	UE_LOG(LogMobWater, Display, TEXT("  collection %s"),
		Collection ? *Collection->GetPathName() : TEXT("none - water will be flat"));

	UE_LOG(LogMobWater, Display, TEXT("  ripple field %s, origin (%.0f, %.0f), %d disturbers, %d stamped"),
		bFieldValid ? TEXT("valid") : TEXT("not yet centred"),
		FieldOrigin.X, FieldOrigin.Y, Disturbers.Num(), LastStampCount);

	int32 Outlines = 0;
	int32 WithoutMask = 0;
	for (const TWeakObjectPtr<UMobWaterExclusionComponent>& Entry : Exclusions)
	{
		if (const UMobWaterExclusionComponent* Exclusion = Entry.Get())
		{
			if (Exclusion->IsMesh())
			{
				++Outlines;
				WithoutMask += Exclusion->GetSilhouetteTexture() ? 0 : 1;
			}
		}
	}

	UE_LOG(LogMobWater, Display,
		TEXT("  %d exclusion volume(s), %d baked outline(s) (%d without a mask), window (%.0f, %.0f), %s"),
		Exclusions.Num(), Outlines, WithoutMask, ExclusionOrigin.X, ExclusionOrigin.Y,
		bExclusionFieldDrawn ? TEXT("drawn") : TEXT("empty"));

	UE_LOG(LogMobWater, Display, TEXT("  waves %d, amplitude %.2f, speed %.2f, choppiness %.2f"),
		DefaultWaves.Waves.Num(), DefaultWaves.AmplitudeScale, DefaultWaves.SpeedScale, DefaultWaves.ChoppinessScale);

	for (int32 Index = 0; Index < DefaultWaves.Waves.Num(); ++Index)
	{
		const FMobGerstnerWave& Wave = DefaultWaves.Waves[Index];
		UE_LOG(LogMobWater, Display, TEXT("    [%d] dir (%.2f, %.2f) length %.1f amplitude %.1f steepness %.2f"),
			Index, Wave.Direction.X, Wave.Direction.Y, Wave.Wavelength, Wave.Amplitude, Wave.Steepness);
	}
}
