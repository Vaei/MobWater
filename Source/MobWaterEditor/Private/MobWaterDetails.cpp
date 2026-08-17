// Copyright (c) Jared Taylor

#include "MobWaterDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FMobWaterDetails::Make(TArray<FName> Categories)
{
	TSharedRef<FMobWaterDetails> Details = MakeShared<FMobWaterDetails>();
	Details->Ordered = MoveTemp(Categories);
	return Details;
}

void FMobWaterDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Spaced rather than consecutive so a category added later can go between two without
	// renumbering the rest, and every one is asked for by name even when it is empty - a category
	// that is only created once something is in it moves as soon as a feature is toggled, and a
	// panel whose sections reorder themselves is worse than one in the wrong order.
	int32 Order = 0;

	for (const FName& Category : Ordered)
	{
		IDetailCategoryBuilder& Builder = DetailBuilder.EditCategory(
			Category, FText::GetEmpty(), ECategoryPriority::Important);

		Builder.SetSortOrder(Order);
		Order += 10;
	}
}
