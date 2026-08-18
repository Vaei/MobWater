// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.h"
#include "Components/StaticMeshComponent.h"
#include "MobWaterFallComponent.generated.h"

class UMobWaterFallSplineComponent;
class UMobWaterLookPreset;

/**
 * A sheet of falling water.
 *
 * Its own component and its own material, and not a fifth shape of UMobWaterComponent. Every meaning
 * a body of water attaches to its own geometry is horizontal: the ripple field is looked down on, the
 * shore distance is measured across the ground, an exclusion volume is a footprint, and a Gerstner
 * wave moves an XY vertex in Z. None of those survive being stood on end - a wave on a vertical sheet
 * would push the water sideways out of the fall, and the distance to the shore would be the distance
 * to nothing.
 *
 * What is shared is what is genuinely shared: the clock, the foam and normal textures, the sky, and
 * the look preset's colours. A fall that does not match the river it comes out of is worse than no
 * fall at all.
 *
 * It does not register as a body of water, and that is load bearing rather than an omission. Nothing
 * floats on a waterfall, and a buoyancy query that found one would answer with a surface standing on
 * end and lift whatever asked straight up the cliff.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Fall"),
	hidecategories=(Collision, Physics, Navigation, HLOD, VirtualTexture, RayTracing))
class MOBWATER_API UMobWaterFallComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMobWaterFallComponent();

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnRegister() override;
	//~ End UActorComponent Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * A look to start from.
	 *
	 * The same preset a body of water takes, and only the parts of it a fall has: the colours, the
	 * gradient row, the foam, the glint and the reflection. There is nothing here to receive a
	 * shoreline or a caustic, so those are read and ignored rather than approximated.
	 *
	 * Applied when it is set, then forgotten, so a preset is a starting point rather than a link that
	 * overrides every adjustment on load.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall")
	TObjectPtr<UMobWaterLookPreset> LookPreset;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Fall")
	void ApplyLookPreset();

	/**
	 * How much water is in the sheet as it leaves the lip, in world units.
	 *
	 * The colour is graded by this the way a body of water is graded by its depth, so a fall and the
	 * river feeding it can carry one gradient row between them. It is not a thickness the sheet is
	 * drawn at - the sheet is a plane and has none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Thickness = 60.f;

	/**
	 * How fast the water is going as it leaves the lip.
	 *
	 * The one number everything the sheet does is measured against. It sets how far a streak travels
	 * in a second, how quickly the water outruns itself on the way down, and therefore where the
	 * sheet thins and where it comes apart. A fall that reads as slack has this too low, not its
	 * streaks too long.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="1.0", ForceUnits="cm/s"))
	float LipSpeed = 260.f;

	/**
	 * What accelerates it on the way down.
	 *
	 * The world's own gravity by default, because that is what makes a fall read as tall. Lowering it
	 * gives water that hangs, which is a stylized fall rather than a slow one: at zero the sheet never
	 * stretches, so it never thins and never breaks into strands, and the pattern simply pans.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="0.0", ForceUnits="cm/s2"))
	float Gravity = 980.f;

	/** How long a streak is where it leaves the lip. Below that it stretches on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="1.0", ForceUnits="cm"))
	float StreakLength = 320.f;

	/** How wide one is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="1.0", ForceUnits="cm"))
	float StreakWidth = 180.f;

	/**
	 * How much the sheet thins as the water is drawn out.
	 *
	 * The same water passing a faster section has to be thinner in it, so this is not a fade someone
	 * chose - it is what puts the weight at the top of a fall. 0 holds the sheet at full thickness
	 * the whole way down, which reads as a printed curtain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ThinAmount = 0.7f;

	/**
	 * How far the stretch pulls the sheet apart into strands.
	 *
	 * Weighted by how far the water has been drawn out rather than by how far down it is, so a short
	 * fall stays whole and a long one breaks up on its own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Breakup = 0.35f;

	/** How far in from each side the sheet fades, as a fraction of its width. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fall", meta=(ClampMin="0.0", ClampMax="0.5"))
	float EdgeFade = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour")
	bool bGradientColor = false;

	/** Which palette in the atlas this fall is graded by. The same rows a body of water reads. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour",
		meta=(EditCondition="bGradientColor", ClampMin="0"))
	int32 GradientRow = 0;

	/** The colour where the sheet has been drawn out thinnest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="!bGradientColor"))
	FLinearColor ThinColor = FLinearColor(0.18f, 0.42f, 0.42f, 1.f);

	/** The colour at the lip, where there is most water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="!bGradientColor"))
	FLinearColor ThickColor = FLinearColor(0.01f, 0.06f, 0.11f, 1.f);

	/**
	 * The water column over which the sheet stops reading as touching what is behind it.
	 *
	 * A fall running down rock is nearly against it, and one hanging free is not, so this is what
	 * turns the polygon edge where the sheet meets the cliff and the pool into a soft line. Raising
	 * it makes the whole sheet read as clinging.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="1.0", ForceUnits="cm"))
	float ClarityDepth = 120.f;

	/** How opaque the sheet is where it is running over something. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinOpacity = 0.25f;

	/** How much of the water's colour is emitted rather than lit. 1 is a flat colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Unlit = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Roughness = 0.12f;

	/** How much of the scrolling detail normal reaches the sheet. 0 is glass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float DetailStrength = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="1.0"))
	float GlintGloss = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float GlintStrength = 1.6f;

	/** How much sky the sheet reflects. A vertical surface reflects the horizon, not the zenith. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float ReflectionStrength = 0.6f;

	/**
	 * Streaks along the water, white at the lip and white at the plunge.
	 *
	 * A compiled variant rather than an amount of zero, so a sheet without it carries neither the
	 * foam sample nor its maths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam")
	bool bFoam = true;

	/** How much foam the streaks carry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float FoamAmount = 0.6f;

	/** How hard a streak edge is. Low is aerated water; high is a stylized cut line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="1.0", ClampMax="16.0"))
	float FoamSharpness = 2.5f;

	/** How far down from the lip the water is white, as a fraction of the drop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float LipFoam = 0.06f;

	/** How far up from the plunge it is white. This is the part that sells the impact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float BaseFoam = 0.18f;

	/**
	 * Bends what is behind the sheet.
	 *
	 * The one feature that reads scene colour, and the one a platform may refuse outright. Worth less
	 * on a fall than on a pool: a sheet is mostly moving foam and there is rarely much behind it to
	 * bend.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Refraction")
	bool bRefraction = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Refraction",
		meta=(EditCondition="bRefraction", ClampMin="0.0", ClampMax="1.0"))
	float RefractionStrength = 0.2f;

	/**
	 * Whether the top of the sheet follows the water feeding it.
	 *
	 * On, the lip is queried where it stands and the top rows are moved to meet the surface that is
	 * actually there. Off, the sheet stays exactly where it was placed, which is right for a fall
	 * coming out of a pipe or a rock face and wrong for one coming off a river - a swell passing the
	 * lip would detach the fall from it on every wave.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Join")
	bool bJoinToWater = true;

	/**
	 * How far down the sheet the join dies away, as a fraction of the drop.
	 *
	 * The top of a fall belongs to the river and the bottom belongs to the ground, so the offset has
	 * to be spent somewhere between them. Too small and the blend creases; too large and the plunge
	 * moves with the swell, which reads as the whole cliff breathing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Join",
		meta=(EditCondition="bJoinToWater", ClampMin="0.01", ClampMax="1.0"))
	float LipFade = 0.25f;

	/**
	 * The most the lip is allowed to move, in world units.
	 *
	 * A guard rather than a look. If the fall is placed over the wrong body, or over an ocean whose
	 * surface is nowhere near the lip, the offset is a large constant and the sheet would be dragged
	 * off the cliff. Clamped, it is visibly wrong in one place instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Join",
		meta=(EditCondition="bJoinToWater", ClampMin="0.0", ForceUnits="cm"))
	float MaxLipOffset = 150.f;

	/** The lip this sheet was built from. Set by AMobWaterFall. */
	void SetLip(UMobWaterFallSplineComponent* InLip);

	/** Rebuilds what this fall draws: its material and everything that travels as data. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Fall")
	void ApplySurface();

	/** Where the water lands, and how wide the impact is. What the plunge disturbance is placed by. */
	bool GetPlunge(FVector& OutLocation, float& OutRadius) const;

	/**
	 * Reads the water above the lip and moves the top of the sheet onto it, now rather than on tick.
	 *
	 * What the tick calls. Exposed because a fall placed by script has no tick behind it yet, and
	 * because a check that asserts the join has to be able to make it happen at a known instant
	 * rather than wait for a frame that may never come in a commandlet.
	 */
	UFUNCTION(BlueprintCallable, Category="Fall")
	void UpdateLipJoin();

	/** How far the top of the sheet has moved to meet the water above it, at each end of the lip. */
	UFUNCTION(BlueprintPure, Category="Fall")
	FVector2D GetLipOffsets() const { return LipOffsets; }

protected:
	/** Which material variant this fall's settings ask for, as a mask of MobWaterFallVariant flags. */
	int32 WantedVariant() const;

	/** Reads the water above each end of the lip and moves the top of the sheet onto it. */
	void TickLipJoin();

	void WriteFallData(int32 Index, float Value);
	void WriteFallData2(int32 Index, const FVector2D& Value);
	void WriteFallData3(int32 Index, const FLinearColor& Value);

	TWeakObjectPtr<UMobWaterFallSplineComponent> Lip;

	/** What the two ends of the lip are currently offset by. Written to primitive data every frame. */
	FVector2D LipOffsets = FVector2D::ZeroVector;
};
