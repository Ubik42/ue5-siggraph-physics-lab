#pragma once

#include "CoreMinimal.h"

struct FFlowMapGpuImpulseParticle
{
	FVector2f PositionCm = FVector2f::ZeroVector;
	FVector2f ReferenceImpulseCmPerSecond = FVector2f::ZeroVector;
	FVector4f ReferenceImpulseGradientPerSecond = FVector4f::Zero();
	FVector4f BackwardMapJacobian = FVector4f(1.0f, 0.0f, 0.0f, 1.0f);
};

struct FFlowMapGpuImpulseTransferInput
{
	FIntPoint CellCounts = FIntPoint(32, 32);
	float CellSizeCm = 5.0f;
	FVector2f OriginCm = FVector2f::ZeroVector;
	bool bUseGradient = true;
	TArray<FFlowMapGpuImpulseParticle> Particles;
};

struct FFlowMapGpuImpulseTransferResult
{
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	int32 GatherPasses = 0;
	TArray<float> FaceU;
	TArray<float> FaceV;
	FString FailureReason;
};

/** 阻塞式 CPU/GPU impulse P2G 真值门；闭环运行时将复用其确定性面 Gather 数学。 */
PHYSICSRESEARCHSHADERS_API FFlowMapGpuImpulseTransferResult RunFlowMapGpuImpulseTransferForValidation(
	const FFlowMapGpuImpulseTransferInput& Input);
