#include "ContactClothNonDistanceBarrier.h"

FContactClothNonDistanceBarrier::FContactClothNonDistanceBarrier(const FContactClothNdbSettings& InSettings)
	: Settings(InSettings)
{
	Settings.BaseWeight = FMath::Max(InSettings.BaseWeight, UE_DOUBLE_SMALL_NUMBER);
	Settings.ActivationMultiplier = FMath::Max(InSettings.ActivationMultiplier, 1.0);
	Settings.MaximumWeight = FMath::Max(InSettings.MaximumWeight, Settings.BaseWeight);
}

FContactClothNdbState FContactClothNonDistanceBarrier::UpdateContact(uint64 ContactId, bool bActive)
{
	FContactClothNdbState& State = States.FindOrAdd(ContactId);
	if (!bActive)
	{
		State = FContactClothNdbState();
		return State;
	}
	State.bActive = true;
	State.ConsecutiveActiveIterations++;
	const double PreviousWeight = State.ConsecutiveActiveIterations == 1 ? Settings.BaseWeight : State.Weight;
	const double CandidateWeight = State.ConsecutiveActiveIterations == 1
		? Settings.BaseWeight * Settings.ActivationMultiplier
		: PreviousWeight * Settings.ActivationMultiplier;
	State.Weight = FMath::Min(CandidateWeight, Settings.MaximumWeight);
	if (CandidateWeight >= Settings.MaximumWeight)
	{
		SaturatedWeightUpdates++;
	}
	return State;
}

bool FContactClothNonDistanceBarrier::FindState(uint64 ContactId, FContactClothNdbState& OutState) const
{
	if (const FContactClothNdbState* State = States.Find(ContactId))
	{
		OutState = *State;
		return true;
	}
	return false;
}

void FContactClothNonDistanceBarrier::EndIteration(TConstArrayView<uint64> ActiveContactIds)
{
	TSet<uint64> ActiveSet;
	ActiveSet.Reserve(ActiveContactIds.Num());
	for (uint64 ContactId : ActiveContactIds)
	{
		ActiveSet.Add(ContactId);
	}
	for (TPair<uint64, FContactClothNdbState>& Pair : States)
	{
		if (!ActiveSet.Contains(Pair.Key))
		{
			Pair.Value = FContactClothNdbState();
		}
	}
}

void FContactClothNonDistanceBarrier::Reset()
{
	States.Reset();
	SaturatedWeightUpdates = 0;
}

FContactClothNdbStats FContactClothNonDistanceBarrier::GetStats() const
{
	FContactClothNdbStats Stats;
	Stats.TrackedContactCount = States.Num();
	Stats.SaturatedWeightUpdates = SaturatedWeightUpdates;
	for (const TPair<uint64, FContactClothNdbState>& Pair : States)
	{
		if (Pair.Value.bActive)
		{
			Stats.ActiveContactCount++;
			Stats.MaximumActivationCount = FMath::Max(Stats.MaximumActivationCount, Pair.Value.ConsecutiveActiveIterations);
			Stats.MaximumWeight = FMath::Max(Stats.MaximumWeight, Pair.Value.Weight);
		}
	}
	return Stats;
}
