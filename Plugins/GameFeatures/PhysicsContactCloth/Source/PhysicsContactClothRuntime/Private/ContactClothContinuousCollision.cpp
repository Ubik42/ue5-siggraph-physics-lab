#include "ContactClothContinuousCollision.h"

namespace ContactClothContinuousCollision
{
double DistanceSquaredToSegment(
	const FVector3d& Point,
	const FVector3d& SegmentStart,
	const FVector3d& SegmentEnd,
	FVector3d& OutClosestPoint)
{
	const FVector3d Segment = SegmentEnd - SegmentStart;
	const double SegmentLengthSquared = Segment.SquaredLength();
	const double Alpha = SegmentLengthSquared > UE_DOUBLE_SMALL_NUMBER
		? FMath::Clamp(FVector3d::DotProduct(Point - SegmentStart, Segment) / SegmentLengthSquared, 0.0, 1.0)
		: 0.0;
	OutClosestPoint = SegmentStart + Alpha * Segment;
	return (Point - OutClosestPoint).SquaredLength();
}

bool SolveRaySphereTime(
	const FVector3d& Start,
	const FVector3d& Delta,
	const FVector3d& Center,
	double Radius,
	double& OutTime)
{
	const FVector3d Offset = Start - Center;
	const double A = Delta.SquaredLength();
	const double B = 2.0 * FVector3d::DotProduct(Offset, Delta);
	const double C = Offset.SquaredLength() - Radius * Radius;
	if (C <= 0.0)
	{
		OutTime = 0.0;
		return true;
	}
	if (A <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	const double Discriminant = B * B - 4.0 * A * C;
	if (Discriminant < 0.0)
	{
		return false;
	}
	const double Time = (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
	if (Time < 0.0 || Time > 1.0)
	{
		return false;
	}
	OutTime = Time;
	return true;
}
}

bool FContactClothContinuousCollision::SweepPointSphere(
	const FVector3d& Start,
	const FVector3d& End,
	const FVector3d& SphereCenter,
	double RadiusCm,
	FContactClothCcdHit& OutHit)
{
	OutHit = FContactClothCcdHit();
	if (RadiusCm <= 0.0)
	{
		return false;
	}
	double Time = 1.0;
	if (!ContactClothContinuousCollision::SolveRaySphereTime(Start, End - Start, SphereCenter, RadiusCm, Time))
	{
		return false;
	}
	OutHit.bHit = true;
	OutHit.bStartedOverlapping = (Start - SphereCenter).SquaredLength() <= RadiusCm * RadiusCm;
	OutHit.TimeOfImpact = Time;
	OutHit.Position = FMath::Lerp(Start, End, Time);
	const FVector3d Offset = OutHit.Position - SphereCenter;
	OutHit.Normal = Offset.SquaredLength() > UE_DOUBLE_SMALL_NUMBER ? Offset.GetSafeNormal() : FVector3d::UpVector;
	return true;
}

bool FContactClothContinuousCollision::SweepPointCapsule(
	const FVector3d& Start,
	const FVector3d& End,
	const FVector3d& CapsuleStart,
	const FVector3d& CapsuleEnd,
	double RadiusCm,
	FContactClothCcdHit& OutHit)
{
	OutHit = FContactClothCcdHit();
	if (RadiusCm <= 0.0)
	{
		return false;
	}
	FVector3d StartClosestPoint;
	const double StartDistanceSquared = ContactClothContinuousCollision::DistanceSquaredToSegment(
		Start, CapsuleStart, CapsuleEnd, StartClosestPoint);
	if (StartDistanceSquared <= RadiusCm * RadiusCm)
	{
		OutHit.bHit = true;
		OutHit.bStartedOverlapping = true;
		OutHit.TimeOfImpact = 0.0;
		OutHit.Position = Start;
		const FVector3d Offset = Start - StartClosestPoint;
		OutHit.Normal = Offset.SquaredLength() > UE_DOUBLE_SMALL_NUMBER ? Offset.GetSafeNormal() : FVector3d::UpVector;
		return true;
	}

	const FVector3d Delta = End - Start;
	const FVector3d Axis = CapsuleEnd - CapsuleStart;
	const double AxisLengthSquared = Axis.SquaredLength();
	if (AxisLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
	{
		return SweepPointSphere(Start, End, CapsuleStart, RadiusCm, OutHit);
	}

	double EarliestTime = TNumericLimits<double>::Max();
	const FVector3d OriginOffset = Start - CapsuleStart;
	const double DeltaLengthSquared = Delta.SquaredLength();
	const double AxisDotDelta = FVector3d::DotProduct(Axis, Delta);
	const double AxisDotOrigin = FVector3d::DotProduct(Axis, OriginOffset);
	const double DeltaDotOrigin = FVector3d::DotProduct(Delta, OriginOffset);
	const double OriginLengthSquared = OriginOffset.SquaredLength();
	const double A = AxisLengthSquared * DeltaLengthSquared - AxisDotDelta * AxisDotDelta;
	const double B = AxisLengthSquared * DeltaDotOrigin - AxisDotOrigin * AxisDotDelta;
	const double C = AxisLengthSquared * OriginLengthSquared - AxisDotOrigin * AxisDotOrigin
		- RadiusCm * RadiusCm * AxisLengthSquared;
	const double Discriminant = B * B - A * C;
	if (FMath::Abs(A) > UE_DOUBLE_SMALL_NUMBER && Discriminant >= 0.0)
	{
		const double CylinderTime = (-B - FMath::Sqrt(Discriminant)) / A;
		const double AxisPosition = AxisDotOrigin + CylinderTime * AxisDotDelta;
		if (CylinderTime >= 0.0 && CylinderTime <= 1.0 && AxisPosition >= 0.0 && AxisPosition <= AxisLengthSquared)
		{
			EarliestTime = CylinderTime;
		}
	}

	double CapTime = 1.0;
	if (ContactClothContinuousCollision::SolveRaySphereTime(Start, Delta, CapsuleStart, RadiusCm, CapTime))
	{
		EarliestTime = FMath::Min(EarliestTime, CapTime);
	}
	if (ContactClothContinuousCollision::SolveRaySphereTime(Start, Delta, CapsuleEnd, RadiusCm, CapTime))
	{
		EarliestTime = FMath::Min(EarliestTime, CapTime);
	}
	if (!FMath::IsFinite(EarliestTime) || EarliestTime < 0.0 || EarliestTime > 1.0)
	{
		return false;
	}

	OutHit.bHit = true;
	OutHit.TimeOfImpact = EarliestTime;
	OutHit.Position = FMath::Lerp(Start, End, EarliestTime);
	FVector3d ClosestPoint;
	ContactClothContinuousCollision::DistanceSquaredToSegment(OutHit.Position, CapsuleStart, CapsuleEnd, ClosestPoint);
	const FVector3d Offset = OutHit.Position - ClosestPoint;
	OutHit.Normal = Offset.SquaredLength() > UE_DOUBLE_SMALL_NUMBER ? Offset.GetSafeNormal() : FVector3d::UpVector;
	return true;
}
