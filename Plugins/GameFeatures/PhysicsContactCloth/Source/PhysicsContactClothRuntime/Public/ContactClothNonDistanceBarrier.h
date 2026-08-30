#pragma once

#include "CoreMinimal.h"

struct FContactClothNdbSettings
{
	double BaseWeight = 1.0;
	double ActivationMultiplier = 2.0;
	double MaximumWeight = 1048576.0;
};

struct FContactClothNdbState
{
	int32 ConsecutiveActiveIterations = 0;
	double Weight = 0.0;
	bool bActive = false;
};

struct FContactClothNdbStats
{
	int32 TrackedContactCount = 0;
	int32 ActiveContactCount = 0;
	int32 MaximumActivationCount = 0;
	double MaximumWeight = 0.0;
	int64 SaturatedWeightUpdates = 0;
};

/** Stateful CPU truth layer for the paper's non-distance barrier activation schedule. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothNonDistanceBarrier
{
public:
	explicit FContactClothNonDistanceBarrier(const FContactClothNdbSettings& InSettings = FContactClothNdbSettings());

	FContactClothNdbState UpdateContact(uint64 ContactId, bool bActive);
	bool FindState(uint64 ContactId, FContactClothNdbState& OutState) const;
	void EndIteration(TConstArrayView<uint64> ActiveContactIds);
	void Reset();
	FContactClothNdbStats GetStats() const;

private:
	FContactClothNdbSettings Settings;
	TMap<uint64, FContactClothNdbState> States;
	int64 SaturatedWeightUpdates = 0;
};
