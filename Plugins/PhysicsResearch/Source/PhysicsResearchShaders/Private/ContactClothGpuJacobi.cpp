#include "ContactClothGpuJacobi.h"

#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"

class FContactClothJacobiCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothJacobiCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothJacobiCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(uint32, Columns)
		SHADER_PARAMETER(uint32, Rows)
		SHADER_PARAMETER(float, RestSpacingCm)
		SHADER_PARAMETER(float, Relaxation)
		SHADER_PARAMETER(FVector3f, SphereCenter)
		SHADER_PARAMETER(float, SphereRadiusWithThicknessCm)
		SHADER_PARAMETER(FVector3f, CapsuleStart)
		SHADER_PARAMETER(float, CapsuleRadiusWithThicknessCm)
		SHADER_PARAMETER(FVector3f, CapsuleEnd)
		SHADER_PARAMETER(uint32, CollisionSphereCount)
		SHADER_PARAMETER(uint32, CollisionCapsuleCount)
		SHADER_PARAMETER(uint32, CollisionPrimitiveCapacity)
		SHADER_PARAMETER(uint32, NdbEnabled)
		SHADER_PARAMETER(float, NdbBaseWeight)
		SHADER_PARAMETER(float, NdbActivationMultiplier)
		SHADER_PARAMETER(float, NdbMaximumWeight)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CollisionActivationCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InputPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutputPositions)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothJacobiCS,
	"/Plugin/PhysicsResearch/Private/ContactClothJacobi.usf",
	"MainCS",
	SF_Compute);

class FContactClothIntegrateCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothIntegrateCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothIntegrateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(uint32, Columns)
		SHADER_PARAMETER(float, FixedDeltaSecondsSquared)
		SHADER_PARAMETER(float, VelocityDamping)
		SHADER_PARAMETER(FVector3f, GravityCmPerSecondSquared)
		SHADER_PARAMETER(uint32, ResidualReuseEnabled)
		SHADER_PARAMETER(float, ResidualForwardScale)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CurrentPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PreviousPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ForwardedResiduals)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothIntegrateCS,
	"/Plugin/PhysicsResearch/Private/ContactClothIntegrate.usf",
	"MainCS",
	SF_Compute);

class FContactClothModalProjectCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothModalProjectCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothModalProjectCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ModalParticleCount)
		SHADER_PARAMETER(uint32, ModalModeCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ModalInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ModalRestPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ModalBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ModalOutputValues)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothModalProjectCS,
	"/Plugin/PhysicsResearch/Private/ContactClothModalCorrection.usf",
	"ProjectRhsCS",
	SF_Compute);

class FContactClothModalApplyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothModalApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothModalApplyCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ModalParticleCount)
		SHADER_PARAMETER(uint32, ModalModeCount)
		SHADER_PARAMETER(uint32, ModalColumns)
		SHADER_PARAMETER(float, ModalCorrectionStrength)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ModalInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ModalBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ModalInverseSystemValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ModalRhsValues)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ModalOutputValues)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothModalApplyCS,
	"/Plugin/PhysicsResearch/Private/ContactClothModalCorrection.usf",
	"SolveAndApplyCS",
	SF_Compute);

class FContactClothRankOneAssemblyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothRankOneAssemblyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothRankOneAssemblyCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, RankOneModeCount)
		SHADER_PARAMETER(uint32, RankOneContactCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RankOneBaseSystem)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RankOneParticleOuterProducts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, RankOneContactParticles)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RankOneContactNormalWeights)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, RankOneOutputMatrix)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothRankOneAssemblyCS,
	"/Plugin/PhysicsResearch/Private/ContactClothRankOneAssembly.usf",
	"MainCS",
	SF_Compute);

class FContactClothRealtimeRankOneAssemblyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothRealtimeRankOneAssemblyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothRealtimeRankOneAssemblyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, RealtimeRankOneParticleCount)
		SHADER_PARAMETER(uint32, RealtimeRankOneModeCount)
		SHADER_PARAMETER(uint32, RealtimeRankOneColumns)
		SHADER_PARAMETER(uint32, RealtimeRankOneSphereCount)
		SHADER_PARAMETER(uint32, RealtimeRankOneCapsuleCount)
		SHADER_PARAMETER(uint32, RealtimeRankOnePrimitiveCapacity)
		SHADER_PARAMETER(float, RealtimeRankOneBaseWeight)
		SHADER_PARAMETER(float, RealtimeRankOneActivationMultiplier)
		SHADER_PARAMETER(float, RealtimeRankOneMaximumWeight)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RealtimeRankOnePositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RealtimeRankOneCollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RealtimeRankOneCollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RealtimeRankOneCollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, RealtimeRankOneActivationCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneBaseSystem)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneParticleOuterProducts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, RealtimeRankOneOutputMatrix)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, RealtimeRankOneOutputRhs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothRealtimeRankOneAssemblyCS, "/Plugin/PhysicsResearch/Private/ContactClothRankOneAssembly.usf", "AssembleContactsCS", SF_Compute);

class FContactClothRealtimeRankOneSolveCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothRealtimeRankOneSolveCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothRealtimeRankOneSolveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, RealtimeRankOneSolveModeCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneSolveMatrix)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneSolveRhs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, RealtimeRankOneSolution)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FContactClothRealtimeRankOneSolveCS, "/Plugin/PhysicsResearch/Private/ContactClothRankOneAssembly.usf", "SolveContactsCS", SF_Compute);

class FContactClothRealtimeRankOneApplyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothRealtimeRankOneApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothRealtimeRankOneApplyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, RealtimeRankOneApplyParticleCount)
		SHADER_PARAMETER(uint32, RealtimeRankOneApplyModeCount)
		SHADER_PARAMETER(uint32, RealtimeRankOneApplyColumns)
		SHADER_PARAMETER(float, RealtimeRankOneMaximumCorrectionCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RealtimeRankOneApplyInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneApplyBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, RealtimeRankOneApplySolution)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, RealtimeRankOneApplyOutputPositions)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothRealtimeRankOneApplyCS, "/Plugin/PhysicsResearch/Private/ContactClothRankOneAssembly.usf", "ApplyContactsCS", SF_Compute);

class FContactClothBarrierStateCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothBarrierStateCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothBarrierStateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, BarrierParticleCount)
		SHADER_PARAMETER(uint32, BarrierColumns)
		SHADER_PARAMETER(uint32, BarrierSphereCount)
		SHADER_PARAMETER(uint32, BarrierCapsuleCount)
		SHADER_PARAMETER(uint32, BarrierPrimitiveCapacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BarrierPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BarrierCollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BarrierCollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BarrierCollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BarrierActivationCounts)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothBarrierStateCS,
	"/Plugin/PhysicsResearch/Private/ContactClothBarrierState.usf",
	"MainCS",
	SF_Compute);

class FContactClothCompleteCcdCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothCompleteCcdCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothCompleteCcdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CcdParticleCount)
		SHADER_PARAMETER(uint32, CcdColumns)
		SHADER_PARAMETER(uint32, CcdSphereCount)
		SHADER_PARAMETER(uint32, CcdCapsuleCount)
		SHADER_PARAMETER(float, CcdSafetyOffsetCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CcdStartPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CcdCandidatePositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CcdCollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CcdCollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CcdCollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, CcdOutputPositions)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FContactClothCompleteCcdCS,
	"/Plugin/PhysicsResearch/Private/ContactClothCompleteCcd.usf",
	"MainCS",
	SF_Compute);

class FContactClothResidualCaptureCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothResidualCaptureCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothResidualCaptureCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ResidualParticleCount)
		SHADER_PARAMETER(uint32, ResidualColumns)
		SHADER_PARAMETER(float, ResidualMaximumCm)
		SHADER_PARAMETER(uint32, ResidualResetState)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ResidualCandidatePositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ResidualPreviousIterationPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ResidualState)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothResidualCaptureCS, "/Plugin/PhysicsResearch/Private/ContactClothResidualReuse.usf", "CaptureCS", SF_Compute);

class FContactClothResidualCcdGateCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothResidualCcdGateCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothResidualCcdGateCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ResidualParticleCount)
		SHADER_PARAMETER(uint32, ResidualColumns)
		SHADER_PARAMETER(float, ResidualMaximumCcdCorrectionCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ResidualCandidatePositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ResidualFinalPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ResidualState)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothResidualCcdGateCS, "/Plugin/PhysicsResearch/Private/ContactClothResidualReuse.usf", "GateCcdCS", SF_Compute);

namespace ContactClothGpuJacobi
{
struct FBlockingDispatchState
{
	TArray<FVector4f> Input;
	TArray<FVector4f> Output;
	FContactClothGpuJacobiSettings Settings;
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	FString FailureReason;
};

struct FRankOneAssemblyDispatchState
{
	int32 ParticleCount = 0;
	int32 ModeCount = 0;
	TArray<float> BaseSystem;
	TArray<float> ParticleOuterProducts;
	TArray<uint32> ContactParticles;
	TArray<FVector4f> ContactNormalWeights;
	TArray<float> Output;
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	FString FailureReason;
};

struct FPackedCollisionData
{
	TArray<FVector4f> Spheres;
	TArray<FVector4f> CapsuleStarts;
	TArray<FVector4f> CapsuleEnds;
	uint32 SphereCount = 0;
	uint32 CapsuleCount = 0;
};

FPackedCollisionData PackCollisionData(
	TConstArrayView<FContactClothGpuSphere> Spheres,
	TConstArrayView<FContactClothGpuCapsule> Capsules,
	float CollisionThicknessCm)
{
	FPackedCollisionData Packed;
	const float Thickness = FMath::Max(0.0f, CollisionThicknessCm);
	for (const FContactClothGpuSphere& Sphere : Spheres)
	{
		if (Sphere.RadiusCm > 0.0f)
		{
			Packed.Spheres.Emplace(Sphere.Center, Sphere.RadiusCm + Thickness);
		}
	}
	for (const FContactClothGpuCapsule& Capsule : Capsules)
	{
		if (Capsule.RadiusCm > 0.0f)
		{
			Packed.CapsuleStarts.Emplace(Capsule.Start, Capsule.RadiusCm + Thickness);
			Packed.CapsuleEnds.Emplace(Capsule.End, 0.0f);
		}
	}
	Packed.SphereCount = Packed.Spheres.Num();
	Packed.CapsuleCount = Packed.CapsuleStarts.Num();
	if (Packed.Spheres.IsEmpty())
	{
		Packed.Spheres.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	}
	if (Packed.CapsuleStarts.IsEmpty())
	{
		Packed.CapsuleStarts.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
		Packed.CapsuleEnds.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	}
	return Packed;
}

FRDGBufferRef CreateCollisionBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	TConstArrayView<FVector4f> Data)
{
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), Data.Num()),
		Name);
	GraphBuilder.QueueBufferUpload(Buffer, Data.GetData(), Data.Num() * sizeof(FVector4f));
	return Buffer;
}
}

FContactClothGpuRankOneAssemblyResult FContactClothGpuRankOneAssembly::RunBlocking(
	int32 ParticleCount,
	int32 ModeCount,
	TConstArrayView<float> BaseReducedSystem,
	TConstArrayView<float> ParticleOuterProducts,
	TConstArrayView<FContactClothGpuModalContact> Contacts)
{
	FContactClothGpuRankOneAssemblyResult Result;
	Result.ModeCount = ModeCount;
	if (!IsInGameThread() || !GDynamicRHI || ParticleCount < 1 || ModeCount < 1
		|| BaseReducedSystem.Num() != ModeCount * ModeCount
		|| ParticleOuterProducts.Num() != ParticleCount * ModeCount * ModeCount)
	{
		Result.FailureReason = TEXT("Invalid rank-one assembly input or unavailable RHI");
		return Result;
	}
	TSharedRef<ContactClothGpuJacobi::FRankOneAssemblyDispatchState, ESPMode::ThreadSafe> State
		= MakeShared<ContactClothGpuJacobi::FRankOneAssemblyDispatchState, ESPMode::ThreadSafe>();
	State->ParticleCount = ParticleCount;
	State->ModeCount = ModeCount;
	State->BaseSystem.Append(BaseReducedSystem.GetData(), BaseReducedSystem.Num());
	State->ParticleOuterProducts.Append(ParticleOuterProducts.GetData(), ParticleOuterProducts.Num());
	for (const FContactClothGpuModalContact& Contact : Contacts)
	{
		if (Contact.ParticleIndex < 0 || Contact.ParticleIndex >= ParticleCount || Contact.Weight < 0.0f
			|| !FMath::IsFinite(Contact.Weight)
			|| !FMath::IsFinite(Contact.Normal.X) || !FMath::IsFinite(Contact.Normal.Y) || !FMath::IsFinite(Contact.Normal.Z)
			|| Contact.Normal.IsNearlyZero())
		{
			Result.FailureReason = TEXT("Invalid modal contact");
			return Result;
		}
		const FVector3f Normal = Contact.Normal.GetSafeNormal();
		State->ContactParticles.Add(static_cast<uint32>(Contact.ParticleIndex));
		State->ContactNormalWeights.Emplace(Normal, Contact.Weight);
	}
	// Keep SRVs valid even when the contact list is empty; RankOneContactCount remains zero.
	if (State->ContactParticles.IsEmpty())
	{
		State->ContactParticles.Add(0u);
		State->ContactNormalWeights.Add(FVector4f::Zero());
	}
	const uint32 ContactCount = Contacts.Num();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRankOneAssemblyBlockingDispatch)(
		[State, ContactCount](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			auto UploadFloatBuffer = [&GraphBuilder](const TCHAR* Name, TConstArrayView<float> Values)
			{
				FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), Values.Num()), Name);
				GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(float));
				return Buffer;
			};
			FRDGBufferRef Base = UploadFloatBuffer(TEXT("ContactCloth.RankOne.Base"), State->BaseSystem);
			FRDGBufferRef Outer = UploadFloatBuffer(TEXT("ContactCloth.RankOne.OuterProducts"), State->ParticleOuterProducts);
			FRDGBufferRef ContactParticles = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), State->ContactParticles.Num()), TEXT("ContactCloth.RankOne.ContactParticles"));
			GraphBuilder.QueueBufferUpload(ContactParticles, State->ContactParticles.GetData(), State->ContactParticles.Num() * sizeof(uint32));
			FRDGBufferRef ContactData = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), State->ContactNormalWeights.Num()), TEXT("ContactCloth.RankOne.ContactData"));
			GraphBuilder.QueueBufferUpload(ContactData, State->ContactNormalWeights.GetData(), State->ContactNormalWeights.Num() * sizeof(FVector4f));
			const int32 DofCount = State->ModeCount * 3;
			FRDGBufferDesc OutputDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount * DofCount);
			OutputDesc.Usage = static_cast<EBufferUsageFlags>(OutputDesc.Usage | BUF_SourceCopy);
			FRDGBufferRef Output = GraphBuilder.CreateBuffer(OutputDesc, TEXT("ContactCloth.RankOne.Output"));
			TShaderMapRef<FContactClothRankOneAssemblyCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FContactClothRankOneAssemblyCS::FParameters* Parameters = GraphBuilder.AllocParameters<FContactClothRankOneAssemblyCS::FParameters>();
			Parameters->RankOneModeCount = State->ModeCount;
			Parameters->RankOneContactCount = ContactCount;
			Parameters->RankOneBaseSystem = GraphBuilder.CreateSRV(Base);
			Parameters->RankOneParticleOuterProducts = GraphBuilder.CreateSRV(Outer);
			Parameters->RankOneContactParticles = GraphBuilder.CreateSRV(ContactParticles);
			Parameters->RankOneContactNormalWeights = GraphBuilder.CreateSRV(ContactData);
			Parameters->RankOneOutputMatrix = GraphBuilder.CreateUAV(Output);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth GPU Rank-One Matrix Assembly"), Shader, Parameters,
				FIntVector(FMath::DivideAndRoundUp(DofCount * DofCount, 64), 1, 1));
			FRHIGPUBufferReadback Readback(TEXT("ContactCloth.RankOne.Readback"));
			const uint32 NumBytes = DofCount * DofCount * sizeof(float);
			AddEnqueueCopyPass(GraphBuilder, &Readback, Output, NumBytes);
			GraphBuilder.Execute();
			State->bDispatched = true;
			RHICmdList.BlockUntilGPUIdle();
			if (Readback.IsReady())
			{
				const void* Data = Readback.Lock(NumBytes);
				if (Data)
				{
					State->Output.SetNumUninitialized(DofCount * DofCount);
					FMemory::Memcpy(State->Output.GetData(), Data, NumBytes);
					State->bReadbackSucceeded = true;
				}
				Readback.Unlock();
			}
			else { State->FailureReason = TEXT("Rank-one GPU readback was not ready"); }
		});
	FlushRenderingCommands();
	Result.bDispatched = State->bDispatched;
	Result.bReadbackSucceeded = State->bReadbackSucceeded;
	Result.Matrix = MoveTemp(State->Output);
	Result.FailureReason = State->FailureReason;
	return Result;
}

class FContactClothGpuRealtimeState : public TSharedFromThis<FContactClothGpuRealtimeState, ESPMode::ThreadSafe>
{
public:
	mutable FCriticalSection DataLock;
	FContactClothGpuRealtimeSettings Settings;
	TArray<FVector4f> InitialPositions;
	TArray<FVector3f> LatestPositions;
	TRefCountPtr<FRDGPooledBuffer> CurrentBuffer;
	TRefCountPtr<FRDGPooledBuffer> PreviousBuffer;
	TRefCountPtr<FRDGPooledBuffer> WorkBufferA;
	TRefCountPtr<FRDGPooledBuffer> WorkBufferB;
	TRefCountPtr<FRDGPooledBuffer> RestBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalBasisBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalInverseSystemBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalRhsBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalSystemBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalParticleOuterProductBuffer;
	TRefCountPtr<FRDGPooledBuffer> RankOneMatrixBuffer;
	TRefCountPtr<FRDGPooledBuffer> RankOneRhsBuffer;
	TRefCountPtr<FRDGPooledBuffer> RankOneSolutionBuffer;
	TRefCountPtr<FRDGPooledBuffer> CollisionActivationBuffer;
	TRefCountPtr<FRDGPooledBuffer> ResidualBuffer;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadback;
	TUniquePtr<FRHIGPUBufferReadback> PendingResidualReadback;
	TUniquePtr<FRHIGPUBufferReadback> PendingActivationReadback;
	int64 PendingReadbackStep = 0;
	FContactClothGpuRealtimeStats Stats;
	bool bLatestPositionsAvailable = false;
	bool bShutdown = false;
	int32 ResidualPrimitiveCount = 0;
	bool bResidualTopologyResetRequested = false;
};

namespace ContactClothGpuRealtime
{
void ConsumeReadyReadback_RenderThread(const TSharedRef<FContactClothGpuRealtimeState, ESPMode::ThreadSafe>& State)
{
	const bool bResidualReadbackRequired = State->Settings.bEnableResidualReuse;
	const bool bActivationReadbackRequired = State->Settings.bEnableNonDistanceBarrier;
	if (!State->PendingReadback || !State->PendingReadback->IsReady()
		|| (bResidualReadbackRequired && (!State->PendingResidualReadback || !State->PendingResidualReadback->IsReady()))
		|| (bActivationReadbackRequired && (!State->PendingActivationReadback || !State->PendingActivationReadback->IsReady())))
	{
		return;
	}
	const uint32 NumBytes = State->InitialPositions.Num() * sizeof(FVector4f);
	const void* Data = State->PendingReadback->Lock(NumBytes);
	if (Data)
	{
		const FVector4f* Positions = static_cast<const FVector4f*>(Data);
		FScopeLock Lock(&State->DataLock);
		State->LatestPositions.SetNumUninitialized(State->InitialPositions.Num());
		for (int32 Index = 0; Index < State->LatestPositions.Num(); ++Index)
		{
			State->LatestPositions[Index] = FVector3f(Positions[Index].X, Positions[Index].Y, Positions[Index].Z);
		}
		State->Stats.ReadbacksCompleted++;
		State->Stats.LatestReadbackStep = State->PendingReadbackStep;
		State->Stats.bReadbackPending = false;
		State->bLatestPositionsAvailable = true;
		if (bResidualReadbackRequired)
		{
			const void* ResidualData = State->PendingResidualReadback->Lock(NumBytes);
			if (ResidualData)
			{
				const FVector4f* Residuals = static_cast<const FVector4f*>(ResidualData);
				int64 FallbackParticles = 0;
				float MaximumResidual = 0.0f;
				for (int32 Index = 0; Index < State->InitialPositions.Num(); ++Index)
				{
					FallbackParticles += FMath::RoundToInt64(Residuals[Index].W);
					MaximumResidual = FMath::Max(MaximumResidual, FVector3f(Residuals[Index].X, Residuals[Index].Y, Residuals[Index].Z).Length());
				}
				State->Stats.ResidualFallbackEvents = FallbackParticles;
				State->Stats.MaximumForwardedResidualCm = MaximumResidual;
			}
			State->PendingResidualReadback->Unlock();
		}
		if (bActivationReadbackRequired)
		{
			const uint32 ActivationCount = State->InitialPositions.Num() * State->Settings.CollisionPrimitiveCapacity;
			const uint32 ActivationBytes = ActivationCount * sizeof(uint32);
			const void* ActivationData = State->PendingActivationReadback->Lock(ActivationBytes);
			if (ActivationData)
			{
				const uint32* ActivationValues = static_cast<const uint32*>(ActivationData);
				int32 ActivePairs = 0;
				int32 MaximumActivation = 0;
				for (uint32 Index = 0; Index < ActivationCount; ++Index)
				{
					if (ActivationValues[Index] > 0)
					{
						ActivePairs++;
						MaximumActivation = FMath::Max(MaximumActivation, static_cast<int32>(ActivationValues[Index]));
					}
				}
				State->Stats.LatestActiveContactPairs = ActivePairs;
				State->Stats.MaximumActiveContactPairs = FMath::Max(State->Stats.MaximumActiveContactPairs, ActivePairs);
				State->Stats.LatestMaximumNdbActivationCount = MaximumActivation;
				State->Stats.MaximumNdbActivationCount = FMath::Max(State->Stats.MaximumNdbActivationCount, MaximumActivation);
			}
			State->PendingActivationReadback->Unlock();
		}
	}
	State->PendingReadback->Unlock();
	State->PendingReadback.Reset();
	State->PendingResidualReadback.Reset();
	State->PendingActivationReadback.Reset();
	State->PendingReadbackStep = 0;
}

void FillJacobiParameters(
	FContactClothJacobiCS::FParameters& Parameters,
	const FContactClothGpuRealtimeState& State,
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef Input,
	FRDGBufferRef Output,
	FRDGBufferRef CollisionSpheres,
	FRDGBufferRef CollisionCapsuleStarts,
	FRDGBufferRef CollisionCapsuleEnds,
	FRDGBufferRef CollisionActivationCounts,
	uint32 CollisionSphereCount,
	uint32 CollisionCapsuleCount)
{
	Parameters.ParticleCount = State.InitialPositions.Num();
	Parameters.Columns = State.Settings.Columns;
	Parameters.Rows = State.Settings.Rows;
	Parameters.RestSpacingCm = State.Settings.SpacingCm;
	Parameters.Relaxation = State.Settings.Relaxation;
	Parameters.SphereCenter = State.Settings.SphereCenter;
	Parameters.SphereRadiusWithThicknessCm = State.Settings.SphereRadiusCm > 0.0f
		? State.Settings.SphereRadiusCm + FMath::Max(0.0f, State.Settings.CollisionThicknessCm)
		: 0.0f;
	Parameters.CapsuleStart = State.Settings.CapsuleStart;
	Parameters.CapsuleEnd = State.Settings.CapsuleEnd;
	Parameters.CapsuleRadiusWithThicknessCm = State.Settings.CapsuleRadiusCm > 0.0f
		? State.Settings.CapsuleRadiusCm + FMath::Max(0.0f, State.Settings.CollisionThicknessCm)
		: 0.0f;
	Parameters.CollisionSphereCount = CollisionSphereCount;
	Parameters.CollisionCapsuleCount = CollisionCapsuleCount;
	Parameters.CollisionPrimitiveCapacity = State.Settings.CollisionPrimitiveCapacity;
	Parameters.NdbEnabled = State.Settings.bEnableNonDistanceBarrier ? 1u : 0u;
	Parameters.NdbBaseWeight = State.Settings.NdbBaseWeight;
	Parameters.NdbActivationMultiplier = State.Settings.NdbActivationMultiplier;
	Parameters.NdbMaximumWeight = State.Settings.NdbMaximumWeight;
	Parameters.CollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
	Parameters.CollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
	Parameters.CollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
	Parameters.CollisionActivationCounts = GraphBuilder.CreateSRV(CollisionActivationCounts);
	Parameters.InputPositions = GraphBuilder.CreateSRV(Input);
	Parameters.OutputPositions = GraphBuilder.CreateUAV(Output);
}
}

FContactClothGpuJacobiResult FContactClothGpuJacobi::RunBlocking(
	TConstArrayView<FVector3f> InputPositions,
	const FContactClothGpuJacobiSettings& InSettings)
{
	FContactClothGpuJacobiResult Result;
	const double StartSeconds = FPlatformTime::Seconds();
	if (!IsInGameThread())
	{
		Result.FailureReason = TEXT("RunBlocking must be called on the game thread");
		return Result;
	}
	if (InputPositions.IsEmpty() || InSettings.Columns < 2 || InSettings.Rows < 2 || InputPositions.Num() != InSettings.Columns * InSettings.Rows)
	{
		Result.FailureReason = TEXT("Input grid dimensions do not match the position count");
		return Result;
	}
	if (!GDynamicRHI)
	{
		Result.FailureReason = TEXT("No dynamic RHI is available");
		return Result;
	}

	TSharedRef<ContactClothGpuJacobi::FBlockingDispatchState, ESPMode::ThreadSafe> State = MakeShared<ContactClothGpuJacobi::FBlockingDispatchState, ESPMode::ThreadSafe>();
	State->Settings = InSettings;
	State->Settings.Iterations = FMath::Max(InSettings.Iterations, 1);
	State->Input.Reserve(InputPositions.Num());
	for (const FVector3f& Position : InputPositions)
	{
		State->Input.Emplace(Position, 0.0f);
	}

	ENQUEUE_RENDER_COMMAND(ContactClothGpuJacobiBlockingDispatch)(
		[State](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			const ContactClothGpuJacobi::FPackedCollisionData CollisionData = ContactClothGpuJacobi::PackCollisionData(
				State->Settings.CollisionSpheres,
				State->Settings.CollisionCapsules,
				State->Settings.CollisionThicknessCm);
			FRDGBufferRef CollisionSpheres = ContactClothGpuJacobi::CreateCollisionBuffer(GraphBuilder, TEXT("ContactCloth.CollisionSpheres"), CollisionData.Spheres);
			FRDGBufferRef CollisionCapsuleStarts = ContactClothGpuJacobi::CreateCollisionBuffer(GraphBuilder, TEXT("ContactCloth.CollisionCapsuleStarts"), CollisionData.CapsuleStarts);
			FRDGBufferRef CollisionCapsuleEnds = ContactClothGpuJacobi::CreateCollisionBuffer(GraphBuilder, TEXT("ContactCloth.CollisionCapsuleEnds"), CollisionData.CapsuleEnds);
			const uint32 ZeroActivation = 0;
			FRDGBufferRef CollisionActivationCounts = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
				TEXT("ContactCloth.CollisionActivationCounts.Disabled"));
			GraphBuilder.QueueBufferUpload(CollisionActivationCounts, &ZeroActivation, sizeof(uint32));
			FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), State->Input.Num());
			BufferDesc.Usage = static_cast<EBufferUsageFlags>(BufferDesc.Usage | BUF_SourceCopy);
			FRDGBufferRef BufferA = GraphBuilder.CreateBuffer(BufferDesc, TEXT("ContactCloth.Jacobi.PositionsA"));
			FRDGBufferRef BufferB = GraphBuilder.CreateBuffer(BufferDesc, TEXT("ContactCloth.Jacobi.PositionsB"));
			GraphBuilder.QueueBufferUpload(BufferA, State->Input.GetData(), State->Input.Num() * sizeof(FVector4f));

			TShaderMapRef<FContactClothJacobiCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRDGBufferRef Current = BufferA;
			FRDGBufferRef Next = BufferB;
			for (int32 Iteration = 0; Iteration < State->Settings.Iterations; ++Iteration)
			{
				FContactClothJacobiCS::FParameters* Parameters = GraphBuilder.AllocParameters<FContactClothJacobiCS::FParameters>();
				Parameters->ParticleCount = State->Input.Num();
				Parameters->Columns = State->Settings.Columns;
				Parameters->Rows = State->Settings.Rows;
				Parameters->RestSpacingCm = State->Settings.SpacingCm;
				Parameters->Relaxation = State->Settings.Relaxation;
				Parameters->SphereCenter = State->Settings.SphereCenter;
				Parameters->SphereRadiusWithThicknessCm = State->Settings.SphereRadiusCm > 0.0f
					? State->Settings.SphereRadiusCm + FMath::Max(0.0f, State->Settings.CollisionThicknessCm)
					: 0.0f;
				Parameters->CapsuleStart = State->Settings.CapsuleStart;
				Parameters->CapsuleEnd = State->Settings.CapsuleEnd;
				Parameters->CapsuleRadiusWithThicknessCm = State->Settings.CapsuleRadiusCm > 0.0f
					? State->Settings.CapsuleRadiusCm + FMath::Max(0.0f, State->Settings.CollisionThicknessCm)
					: 0.0f;
				Parameters->CollisionSphereCount = CollisionData.SphereCount;
				Parameters->CollisionCapsuleCount = CollisionData.CapsuleCount;
				Parameters->CollisionPrimitiveCapacity = 1;
				Parameters->NdbEnabled = 0;
				Parameters->NdbBaseWeight = 1.0f;
				Parameters->NdbActivationMultiplier = 1.0f;
				Parameters->NdbMaximumWeight = 1.0f;
				Parameters->CollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				Parameters->CollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				Parameters->CollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				Parameters->CollisionActivationCounts = GraphBuilder.CreateSRV(CollisionActivationCounts);
				Parameters->InputPositions = GraphBuilder.CreateSRV(Current);
				Parameters->OutputPositions = GraphBuilder.CreateUAV(Next);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth GPU Jacobi %d", Iteration),
					ComputeShader,
					Parameters,
					FIntVector(FMath::DivideAndRoundUp(State->Input.Num(), 64), 1, 1));
				Swap(Current, Next);
			}

			FRHIGPUBufferReadback Readback(TEXT("ContactCloth.GpuJacobi.Readback"));
			AddEnqueueCopyPass(GraphBuilder, &Readback, Current, State->Input.Num() * sizeof(FVector4f));
			GraphBuilder.Execute();
			State->bDispatched = true;
			RHICmdList.BlockUntilGPUIdle();
			if (Readback.IsReady())
			{
				const uint32 NumBytes = State->Input.Num() * sizeof(FVector4f);
				const void* Data = Readback.Lock(NumBytes);
				if (Data)
				{
					State->Output.SetNumUninitialized(State->Input.Num());
					FMemory::Memcpy(State->Output.GetData(), Data, NumBytes);
					State->bReadbackSucceeded = true;
				}
				Readback.Unlock();
			}
			else
			{
				State->FailureReason = TEXT("GPU readback was not ready after BlockUntilGPUIdle");
			}
		});

	FlushRenderingCommands();
	Result.bDispatched = State->bDispatched;
	Result.bReadbackSucceeded = State->bReadbackSucceeded;
	Result.FailureReason = State->FailureReason;
	Result.Positions.Reserve(State->Output.Num());
	for (const FVector4f& Position : State->Output)
	{
		Result.Positions.Emplace(Position.X, Position.Y, Position.Z);
	}
	Result.InputMaximumEdgeErrorCm = MeasureMaximumStructuralEdgeErrorCm(InputPositions, InSettings.Columns, InSettings.Rows, InSettings.SpacingCm);
	Result.OutputMaximumEdgeErrorCm = MeasureMaximumStructuralEdgeErrorCm(Result.Positions, InSettings.Columns, InSettings.Rows, InSettings.SpacingCm);
	const float CapsuleRadiusWithThickness = InSettings.CapsuleRadiusCm > 0.0f
		? InSettings.CapsuleRadiusCm + FMath::Max(0.0f, InSettings.CollisionThicknessCm)
		: 0.0f;
	Result.InputMaximumCapsulePenetrationCm = MeasureMaximumCapsulePenetrationCm(InputPositions, InSettings.CapsuleStart, InSettings.CapsuleEnd, CapsuleRadiusWithThickness);
	Result.OutputMaximumCapsulePenetrationCm = MeasureMaximumCapsulePenetrationCm(Result.Positions, InSettings.CapsuleStart, InSettings.CapsuleEnd, CapsuleRadiusWithThickness);
	Result.BlockingWallMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}

double FContactClothGpuJacobi::MeasureMaximumCapsulePenetrationCm(
	TConstArrayView<FVector3f> Positions,
	const FVector3f& CapsuleStart,
	const FVector3f& CapsuleEnd,
	float RadiusWithThicknessCm)
{
	if (RadiusWithThicknessCm <= 0.0f)
	{
		return 0.0;
	}
	const FVector3f Segment = CapsuleEnd - CapsuleStart;
	const float SegmentLengthSquared = Segment.SquaredLength();
	double MaximumPenetration = 0.0;
	for (const FVector3f& Position : Positions)
	{
		const float Alpha = SegmentLengthSquared > UE_SMALL_NUMBER
			? FMath::Clamp(FVector3f::DotProduct(Position - CapsuleStart, Segment) / SegmentLengthSquared, 0.0f, 1.0f)
			: 0.0f;
		const FVector3f ClosestPoint = CapsuleStart + Alpha * Segment;
		MaximumPenetration = FMath::Max(MaximumPenetration, static_cast<double>(RadiusWithThicknessCm - (Position - ClosestPoint).Length()));
	}
	return FMath::Max(MaximumPenetration, 0.0);
}

double FContactClothGpuJacobi::MeasureMaximumStructuralEdgeErrorCm(
	TConstArrayView<FVector3f> Positions,
	int32 Columns,
	int32 Rows,
	float RestSpacingCm)
{
	if (Positions.Num() != Columns * Rows || Columns < 2 || Rows < 2)
	{
		return TNumericLimits<double>::Max();
	}
	double MaximumError = 0.0;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const int32 Index = Row * Columns + Column;
			if (Column + 1 < Columns)
			{
				MaximumError = FMath::Max(MaximumError, FMath::Abs(static_cast<double>((Positions[Index] - Positions[Index + 1]).Length()) - RestSpacingCm));
			}
			if (Row + 1 < Rows)
			{
				MaximumError = FMath::Max(MaximumError, FMath::Abs(static_cast<double>((Positions[Index] - Positions[Index + Columns]).Length()) - RestSpacingCm));
			}
		}
	}
	return MaximumError;
}

FContactClothGpuRealtimeSimulation::FContactClothGpuRealtimeSimulation() = default;

FContactClothGpuRealtimeSimulation::~FContactClothGpuRealtimeSimulation()
{
	Shutdown();
}

bool FContactClothGpuRealtimeSimulation::Initialize(
	TConstArrayView<FVector3f> InitialPositions,
	const FContactClothGpuRealtimeSettings& InSettings)
{
	if (!IsInGameThread() || !GDynamicRHI || InitialPositions.IsEmpty()
		|| InSettings.Columns < 2 || InSettings.Rows < 2
		|| InitialPositions.Num() != InSettings.Columns * InSettings.Rows)
	{
		return false;
	}
	const bool bModalRequested = InSettings.ModalModeCount > 0 || InSettings.ModalCorrectionStrength > 0.0f
		|| !InSettings.ModalBasisValues.IsEmpty() || !InSettings.ModalInverseSystemValues.IsEmpty();
	if (bModalRequested && (InSettings.ModalModeCount < 1 || InSettings.ModalCorrectionStrength <= 0.0f
		|| InSettings.ModalBasisValues.Num() != InitialPositions.Num() * InSettings.ModalModeCount
		|| InSettings.ModalInverseSystemValues.Num() != InSettings.ModalModeCount * InSettings.ModalModeCount))
	{
		return false;
	}
	const bool bRankOneRequested = InSettings.bEnableRankOneCollisionHessian;
	if (bRankOneRequested && (!bModalRequested || !InSettings.bEnableNonDistanceBarrier || InSettings.ModalModeCount > 16
		|| InSettings.ModalSystemValues.Num() != InSettings.ModalModeCount * InSettings.ModalModeCount
		|| InSettings.ModalParticleOuterProductValues.Num() != InitialPositions.Num() * InSettings.ModalModeCount * InSettings.ModalModeCount))
	{
		return false;
	}
	const ContactClothGpuJacobi::FPackedCollisionData InitialCollisionData = ContactClothGpuJacobi::PackCollisionData(
		InSettings.CollisionSpheres,
		InSettings.CollisionCapsules,
		InSettings.CollisionThicknessCm);
	const int32 InitialPrimitiveCount = InitialCollisionData.SphereCount + InitialCollisionData.CapsuleCount;
	const int32 RequestedPrimitiveCapacity = InSettings.CollisionPrimitiveCapacity > 0
		? InSettings.CollisionPrimitiveCapacity
		: FMath::Max(InitialPrimitiveCount, 1);
	if (InSettings.bEnableNonDistanceBarrier && InitialPrimitiveCount > RequestedPrimitiveCapacity)
	{
		return false;
	}

	Shutdown();
	State = MakeShared<FContactClothGpuRealtimeState, ESPMode::ThreadSafe>();
	State->Settings = InSettings;
	State->Settings.Iterations = FMath::Max(1, InSettings.Iterations);
	State->Settings.FixedDeltaSeconds = FMath::Max(InSettings.FixedDeltaSeconds, UE_SMALL_NUMBER);
	State->Settings.VelocityDamping = FMath::Clamp(InSettings.VelocityDamping, 0.0f, 1.0f);
	State->Settings.ReadbackIntervalSteps = FMath::Max(1, InSettings.ReadbackIntervalSteps);
	State->Settings.ModalCorrectionStrength = FMath::Clamp(InSettings.ModalCorrectionStrength, 0.0f, 1.0f);
	State->Settings.CollisionPrimitiveCapacity = FMath::Max(RequestedPrimitiveCapacity, 1);
	State->Settings.NdbBaseWeight = FMath::Max(InSettings.NdbBaseWeight, UE_SMALL_NUMBER);
	State->Settings.NdbActivationMultiplier = FMath::Max(InSettings.NdbActivationMultiplier, 1.0f);
	State->Settings.NdbMaximumWeight = FMath::Max(InSettings.NdbMaximumWeight, State->Settings.NdbBaseWeight);
	State->Settings.CcdSafetyOffsetCm = FMath::Max(InSettings.CcdSafetyOffsetCm, 0.0f);
	State->Settings.ResidualForwardScale = FMath::Clamp(InSettings.ResidualForwardScale, 0.0f, 1.0f);
	State->Settings.MaximumResidualCm = FMath::Max(InSettings.MaximumResidualCm, 0.0f);
	State->Settings.MaximumResidualCcdCorrectionCm = FMath::Max(InSettings.MaximumResidualCcdCorrectionCm, 0.0f);
	State->Settings.RankOneMaximumCorrectionCm = FMath::Max(InSettings.RankOneMaximumCorrectionCm, 0.0f);
	State->ResidualPrimitiveCount = InitialPrimitiveCount;
	State->InitialPositions.Reserve(InitialPositions.Num());
	for (const FVector3f& Position : InitialPositions)
	{
		State->InitialPositions.Emplace(Position, 0.0f);
	}

	const TSharedRef<FContactClothGpuRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimeInitialize)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			if (StateRef->bShutdown)
			{
				return;
			}
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->InitialPositions.Num());
			Desc.Usage = static_cast<EBufferUsageFlags>(Desc.Usage | BUF_SourceCopy);
			FRDGBufferRef Current = GraphBuilder.CreateBuffer(Desc, TEXT("ContactCloth.Realtime.Current"));
			FRDGBufferRef Previous = GraphBuilder.CreateBuffer(Desc, TEXT("ContactCloth.Realtime.Previous"));
			FRDGBufferRef WorkA = GraphBuilder.CreateBuffer(Desc, TEXT("ContactCloth.Realtime.WorkA"));
			FRDGBufferRef WorkB = GraphBuilder.CreateBuffer(Desc, TEXT("ContactCloth.Realtime.WorkB"));
			const uint32 NumBytes = StateRef->InitialPositions.Num() * sizeof(FVector4f);
			GraphBuilder.QueueBufferUpload(Current, StateRef->InitialPositions.GetData(), NumBytes);
			GraphBuilder.QueueBufferUpload(Previous, StateRef->InitialPositions.GetData(), NumBytes);
			StateRef->CurrentBuffer = GraphBuilder.ConvertToExternalBuffer(Current);
			StateRef->PreviousBuffer = GraphBuilder.ConvertToExternalBuffer(Previous);
			StateRef->WorkBufferA = GraphBuilder.ConvertToExternalBuffer(WorkA);
			StateRef->WorkBufferB = GraphBuilder.ConvertToExternalBuffer(WorkB);
			TArray<FVector4f> ZeroResiduals;
			ZeroResiduals.Init(FVector4f::Zero(), StateRef->InitialPositions.Num());
			FRDGBufferRef Residual = GraphBuilder.CreateBuffer(Desc, TEXT("ContactCloth.Realtime.Residual"));
			GraphBuilder.QueueBufferUpload(Residual, ZeroResiduals.GetData(), NumBytes);
			StateRef->ResidualBuffer = GraphBuilder.ConvertToExternalBuffer(Residual);
			const int32 ActivationCount = StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity;
			TArray<uint32> ZeroActivations;
			ZeroActivations.Init(0u, ActivationCount);
			FRDGBufferRef CollisionActivations = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), ActivationCount),
				TEXT("ContactCloth.Realtime.CollisionActivations"));
			GraphBuilder.QueueBufferUpload(
				CollisionActivations,
				ZeroActivations.GetData(),
				ZeroActivations.Num() * sizeof(uint32));
			StateRef->CollisionActivationBuffer = GraphBuilder.ConvertToExternalBuffer(CollisionActivations);
			if (StateRef->Settings.ModalModeCount > 0)
			{
				FRDGBufferRef Rest = GraphBuilder.CreateBuffer(Desc, TEXT("ContactCloth.Realtime.Rest"));
				GraphBuilder.QueueBufferUpload(Rest, StateRef->InitialPositions.GetData(), NumBytes);
				FRDGBufferRef ModalBasis = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(float), StateRef->Settings.ModalBasisValues.Num()),
					TEXT("ContactCloth.Realtime.ModalBasis"));
				GraphBuilder.QueueBufferUpload(
					ModalBasis,
					StateRef->Settings.ModalBasisValues.GetData(),
					StateRef->Settings.ModalBasisValues.Num() * sizeof(float));
				FRDGBufferRef ModalInverse = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(float), StateRef->Settings.ModalInverseSystemValues.Num()),
					TEXT("ContactCloth.Realtime.ModalInverseSystem"));
				GraphBuilder.QueueBufferUpload(
					ModalInverse,
					StateRef->Settings.ModalInverseSystemValues.GetData(),
					StateRef->Settings.ModalInverseSystemValues.Num() * sizeof(float));
				FRDGBufferRef ModalRhs = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->Settings.ModalModeCount),
					TEXT("ContactCloth.Realtime.ModalRhs"));
				StateRef->RestBuffer = GraphBuilder.ConvertToExternalBuffer(Rest);
				StateRef->ModalBasisBuffer = GraphBuilder.ConvertToExternalBuffer(ModalBasis);
				StateRef->ModalInverseSystemBuffer = GraphBuilder.ConvertToExternalBuffer(ModalInverse);
				StateRef->ModalRhsBuffer = GraphBuilder.ConvertToExternalBuffer(ModalRhs);
				if (StateRef->Settings.bEnableRankOneCollisionHessian)
				{
					FRDGBufferRef ModalSystem = GraphBuilder.CreateBuffer(
						FRDGBufferDesc::CreateStructuredDesc(sizeof(float), StateRef->Settings.ModalSystemValues.Num()),
						TEXT("ContactCloth.Realtime.ModalSystem"));
					GraphBuilder.QueueBufferUpload(ModalSystem, StateRef->Settings.ModalSystemValues.GetData(), StateRef->Settings.ModalSystemValues.Num() * sizeof(float));
					FRDGBufferRef OuterProducts = GraphBuilder.CreateBuffer(
						FRDGBufferDesc::CreateStructuredDesc(sizeof(float), StateRef->Settings.ModalParticleOuterProductValues.Num()),
						TEXT("ContactCloth.Realtime.ModalOuterProducts"));
					GraphBuilder.QueueBufferUpload(OuterProducts, StateRef->Settings.ModalParticleOuterProductValues.GetData(), StateRef->Settings.ModalParticleOuterProductValues.Num() * sizeof(float));
					const int32 DofCount = StateRef->Settings.ModalModeCount * 3;
					FRDGBufferRef RankOneMatrix = GraphBuilder.CreateBuffer(
						FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount * DofCount), TEXT("ContactCloth.Realtime.RankOneMatrix"));
					FRDGBufferRef RankOneRhs = GraphBuilder.CreateBuffer(
						FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount), TEXT("ContactCloth.Realtime.RankOneRhs"));
					FRDGBufferRef RankOneSolution = GraphBuilder.CreateBuffer(
						FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount), TEXT("ContactCloth.Realtime.RankOneSolution"));
					StateRef->ModalSystemBuffer = GraphBuilder.ConvertToExternalBuffer(ModalSystem);
					StateRef->ModalParticleOuterProductBuffer = GraphBuilder.ConvertToExternalBuffer(OuterProducts);
					StateRef->RankOneMatrixBuffer = GraphBuilder.ConvertToExternalBuffer(RankOneMatrix);
					StateRef->RankOneRhsBuffer = GraphBuilder.ConvertToExternalBuffer(RankOneRhs);
					StateRef->RankOneSolutionBuffer = GraphBuilder.ConvertToExternalBuffer(RankOneSolution);
				}
			}
			GraphBuilder.Execute();
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.bInitialized = true;
		});
	return true;
}

bool FContactClothGpuRealtimeSimulation::EnqueueStep()
{
	if (!IsInGameThread() || !State.IsValid())
	{
		return false;
	}
	const TSharedRef<FContactClothGpuRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	int64 StepToExecute = 0;
	bool bResetResidualForTopology = false;
	ContactClothGpuJacobi::FPackedCollisionData CollisionData;
	{
		FScopeLock Lock(&StateRef->DataLock);
		if (StateRef->bShutdown)
		{
			return false;
		}
		CollisionData = ContactClothGpuJacobi::PackCollisionData(
			StateRef->Settings.CollisionSpheres,
			StateRef->Settings.CollisionCapsules,
			StateRef->Settings.CollisionThicknessCm);
		if (StateRef->Settings.bEnableNonDistanceBarrier
			&& CollisionData.SphereCount + CollisionData.CapsuleCount > static_cast<uint32>(StateRef->Settings.CollisionPrimitiveCapacity))
		{
			return false;
		}
		const int32 PrimitiveCount = CollisionData.SphereCount + CollisionData.CapsuleCount;
		if (StateRef->Settings.bEnableResidualReuse && PrimitiveCount != StateRef->ResidualPrimitiveCount)
		{
			StateRef->ResidualPrimitiveCount = PrimitiveCount;
			StateRef->bResidualTopologyResetRequested = true;
		}
		bResetResidualForTopology = StateRef->bResidualTopologyResetRequested;
		StateRef->bResidualTopologyResetRequested = false;
		StepToExecute = ++StateRef->Stats.StepsEnqueued;
	}

	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimeStep)(
		[StateRef, StepToExecute, bResetResidualForTopology, CollisionData = MoveTemp(CollisionData)](FRHICommandListImmediate& RHICmdList)
		{
			if (StateRef->bShutdown || !StateRef->CurrentBuffer || !StateRef->PreviousBuffer || !StateRef->WorkBufferA || !StateRef->WorkBufferB
				|| !StateRef->CollisionActivationBuffer || !StateRef->ResidualBuffer)
			{
				return;
			}
			ContactClothGpuRealtime::ConsumeReadyReadback_RenderThread(StateRef);
			const double RenderSubmissionStartSeconds = FPlatformTime::Seconds();

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef Current = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentBuffer, TEXT("ContactCloth.Realtime.Current"));
			FRDGBufferRef Previous = GraphBuilder.RegisterExternalBuffer(StateRef->PreviousBuffer, TEXT("ContactCloth.Realtime.Previous"));
			FRDGBufferRef WorkA = GraphBuilder.RegisterExternalBuffer(StateRef->WorkBufferA, TEXT("ContactCloth.Realtime.WorkA"));
			FRDGBufferRef WorkB = GraphBuilder.RegisterExternalBuffer(StateRef->WorkBufferB, TEXT("ContactCloth.Realtime.WorkB"));
			FRDGBufferRef CollisionActivations = GraphBuilder.RegisterExternalBuffer(
				StateRef->CollisionActivationBuffer,
				TEXT("ContactCloth.Realtime.CollisionActivations"));
			FRDGBufferRef Residual = GraphBuilder.RegisterExternalBuffer(StateRef->ResidualBuffer, TEXT("ContactCloth.Realtime.Residual"));
			FRDGBufferRef CollisionSpheres = ContactClothGpuJacobi::CreateCollisionBuffer(
				GraphBuilder, TEXT("ContactCloth.Realtime.CollisionSpheres"), CollisionData.Spheres);
			FRDGBufferRef CollisionCapsuleStarts = ContactClothGpuJacobi::CreateCollisionBuffer(
				GraphBuilder, TEXT("ContactCloth.Realtime.CollisionCapsuleStarts"), CollisionData.CapsuleStarts);
			FRDGBufferRef CollisionCapsuleEnds = ContactClothGpuJacobi::CreateCollisionBuffer(
				GraphBuilder, TEXT("ContactCloth.Realtime.CollisionCapsuleEnds"), CollisionData.CapsuleEnds);

			TShaderMapRef<FContactClothIntegrateCS> IntegrateShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FContactClothIntegrateCS::FParameters* IntegrateParameters = GraphBuilder.AllocParameters<FContactClothIntegrateCS::FParameters>();
			IntegrateParameters->ParticleCount = StateRef->InitialPositions.Num();
			IntegrateParameters->Columns = StateRef->Settings.Columns;
			IntegrateParameters->FixedDeltaSecondsSquared = FMath::Square(StateRef->Settings.FixedDeltaSeconds);
			IntegrateParameters->VelocityDamping = StateRef->Settings.VelocityDamping;
			IntegrateParameters->GravityCmPerSecondSquared = StateRef->Settings.GravityCmPerSecondSquared;
			IntegrateParameters->ResidualReuseEnabled = StateRef->Settings.bEnableResidualReuse ? 1u : 0u;
			IntegrateParameters->ResidualForwardScale = StateRef->Settings.ResidualForwardScale;
			IntegrateParameters->CurrentPositions = GraphBuilder.CreateSRV(Current);
			IntegrateParameters->PreviousPositions = GraphBuilder.CreateSRV(Previous);
			IntegrateParameters->ForwardedResiduals = GraphBuilder.CreateSRV(Residual);
			IntegrateParameters->PredictedPositions = GraphBuilder.CreateUAV(WorkA);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ContactCloth GPU Integrate Step %lld", StepToExecute),
				IntegrateShader,
				IntegrateParameters,
				FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));

			const bool bModalEnabled = StateRef->Settings.ModalModeCount > 0;
			FRDGBufferRef ModalCorrected = WorkA;
			FRDGBufferRef ModalScratch = WorkB;
			if (bModalEnabled)
			{
				if (!StateRef->RestBuffer || !StateRef->ModalBasisBuffer || !StateRef->ModalInverseSystemBuffer || !StateRef->ModalRhsBuffer)
				{
					return;
				}
				FRDGBufferRef Rest = GraphBuilder.RegisterExternalBuffer(StateRef->RestBuffer, TEXT("ContactCloth.Realtime.Rest"));
				FRDGBufferRef ModalBasis = GraphBuilder.RegisterExternalBuffer(StateRef->ModalBasisBuffer, TEXT("ContactCloth.Realtime.ModalBasis"));
				FRDGBufferRef ModalInverse = GraphBuilder.RegisterExternalBuffer(StateRef->ModalInverseSystemBuffer, TEXT("ContactCloth.Realtime.ModalInverseSystem"));
				FRDGBufferRef ModalRhs = GraphBuilder.RegisterExternalBuffer(StateRef->ModalRhsBuffer, TEXT("ContactCloth.Realtime.ModalRhs"));

				TShaderMapRef<FContactClothModalProjectCS> ModalProjectShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothModalProjectCS::FParameters* ModalProjectParameters = GraphBuilder.AllocParameters<FContactClothModalProjectCS::FParameters>();
				ModalProjectParameters->ModalParticleCount = StateRef->InitialPositions.Num();
				ModalProjectParameters->ModalModeCount = StateRef->Settings.ModalModeCount;
				ModalProjectParameters->ModalInputPositions = GraphBuilder.CreateSRV(WorkA);
				ModalProjectParameters->ModalRestPositions = GraphBuilder.CreateSRV(Rest);
				ModalProjectParameters->ModalBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				ModalProjectParameters->ModalOutputValues = GraphBuilder.CreateUAV(ModalRhs);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Modal Project Step %lld", StepToExecute),
					ModalProjectShader,
					ModalProjectParameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->Settings.ModalModeCount, 64), 1, 1));

				TShaderMapRef<FContactClothModalApplyCS> ModalApplyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothModalApplyCS::FParameters* ModalApplyParameters = GraphBuilder.AllocParameters<FContactClothModalApplyCS::FParameters>();
				ModalApplyParameters->ModalParticleCount = StateRef->InitialPositions.Num();
				ModalApplyParameters->ModalModeCount = StateRef->Settings.ModalModeCount;
				ModalApplyParameters->ModalColumns = StateRef->Settings.Columns;
				ModalApplyParameters->ModalCorrectionStrength = StateRef->Settings.ModalCorrectionStrength;
				ModalApplyParameters->ModalInputPositions = GraphBuilder.CreateSRV(WorkA);
				ModalApplyParameters->ModalBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				ModalApplyParameters->ModalInverseSystemValues = GraphBuilder.CreateSRV(ModalInverse);
				ModalApplyParameters->ModalRhsValues = GraphBuilder.CreateSRV(ModalRhs);
				ModalApplyParameters->ModalOutputValues = GraphBuilder.CreateUAV(WorkB);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Modal Solve Apply Step %lld", StepToExecute),
					ModalApplyShader,
					ModalApplyParameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				ModalCorrected = WorkB;
				ModalScratch = WorkA;
			}

			const bool bNdbEnabled = StateRef->Settings.bEnableNonDistanceBarrier;
			if (bNdbEnabled)
			{
				TShaderMapRef<FContactClothBarrierStateCS> BarrierShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothBarrierStateCS::FParameters* BarrierParameters = GraphBuilder.AllocParameters<FContactClothBarrierStateCS::FParameters>();
				BarrierParameters->BarrierParticleCount = StateRef->InitialPositions.Num();
				BarrierParameters->BarrierColumns = StateRef->Settings.Columns;
				BarrierParameters->BarrierSphereCount = CollisionData.SphereCount;
				BarrierParameters->BarrierCapsuleCount = CollisionData.CapsuleCount;
				BarrierParameters->BarrierPrimitiveCapacity = StateRef->Settings.CollisionPrimitiveCapacity;
				BarrierParameters->BarrierPositions = GraphBuilder.CreateSRV(ModalCorrected);
				BarrierParameters->BarrierCollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				BarrierParameters->BarrierCollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				BarrierParameters->BarrierCollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				BarrierParameters->BarrierActivationCounts = GraphBuilder.CreateUAV(CollisionActivations);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth NDB State Step %lld", StepToExecute),
					BarrierShader,
					BarrierParameters,
					FIntVector(FMath::DivideAndRoundUp(
						StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity,
						64), 1, 1));
			}

			if (StateRef->Settings.bEnableRankOneCollisionHessian)
			{
				if (!StateRef->ModalBasisBuffer || !StateRef->ModalSystemBuffer || !StateRef->ModalParticleOuterProductBuffer
					|| !StateRef->RankOneMatrixBuffer || !StateRef->RankOneRhsBuffer || !StateRef->RankOneSolutionBuffer)
				{
					return;
				}
				FRDGBufferRef ModalBasis = GraphBuilder.RegisterExternalBuffer(StateRef->ModalBasisBuffer, TEXT("ContactCloth.Realtime.RankOneBasis"));
				FRDGBufferRef ModalSystem = GraphBuilder.RegisterExternalBuffer(StateRef->ModalSystemBuffer, TEXT("ContactCloth.Realtime.RankOneBase"));
				FRDGBufferRef OuterProducts = GraphBuilder.RegisterExternalBuffer(StateRef->ModalParticleOuterProductBuffer, TEXT("ContactCloth.Realtime.RankOneOuterProducts"));
				FRDGBufferRef RankOneMatrix = GraphBuilder.RegisterExternalBuffer(StateRef->RankOneMatrixBuffer, TEXT("ContactCloth.Realtime.RankOneMatrix"));
				FRDGBufferRef RankOneRhs = GraphBuilder.RegisterExternalBuffer(StateRef->RankOneRhsBuffer, TEXT("ContactCloth.Realtime.RankOneRhs"));
				FRDGBufferRef RankOneSolution = GraphBuilder.RegisterExternalBuffer(StateRef->RankOneSolutionBuffer, TEXT("ContactCloth.Realtime.RankOneSolution"));
				const int32 DofCount = StateRef->Settings.ModalModeCount * 3;

				TShaderMapRef<FContactClothRealtimeRankOneAssemblyCS> AssemblyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothRealtimeRankOneAssemblyCS::FParameters* AssemblyParameters = GraphBuilder.AllocParameters<FContactClothRealtimeRankOneAssemblyCS::FParameters>();
				AssemblyParameters->RealtimeRankOneParticleCount = StateRef->InitialPositions.Num();
				AssemblyParameters->RealtimeRankOneModeCount = StateRef->Settings.ModalModeCount;
				AssemblyParameters->RealtimeRankOneColumns = StateRef->Settings.Columns;
				AssemblyParameters->RealtimeRankOneSphereCount = CollisionData.SphereCount;
				AssemblyParameters->RealtimeRankOneCapsuleCount = CollisionData.CapsuleCount;
				AssemblyParameters->RealtimeRankOnePrimitiveCapacity = StateRef->Settings.CollisionPrimitiveCapacity;
				AssemblyParameters->RealtimeRankOneBaseWeight = StateRef->Settings.NdbBaseWeight;
				AssemblyParameters->RealtimeRankOneActivationMultiplier = StateRef->Settings.NdbActivationMultiplier;
				AssemblyParameters->RealtimeRankOneMaximumWeight = StateRef->Settings.NdbMaximumWeight;
				AssemblyParameters->RealtimeRankOnePositions = GraphBuilder.CreateSRV(ModalCorrected);
				AssemblyParameters->RealtimeRankOneCollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				AssemblyParameters->RealtimeRankOneCollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				AssemblyParameters->RealtimeRankOneCollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				AssemblyParameters->RealtimeRankOneActivationCounts = GraphBuilder.CreateSRV(CollisionActivations);
				AssemblyParameters->RealtimeRankOneBaseSystem = GraphBuilder.CreateSRV(ModalSystem);
				AssemblyParameters->RealtimeRankOneBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				AssemblyParameters->RealtimeRankOneParticleOuterProducts = GraphBuilder.CreateSRV(OuterProducts);
				AssemblyParameters->RealtimeRankOneOutputMatrix = GraphBuilder.CreateUAV(RankOneMatrix);
				AssemblyParameters->RealtimeRankOneOutputRhs = GraphBuilder.CreateUAV(RankOneRhs);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Rank-One Assemble Step %lld", StepToExecute),
					AssemblyShader, AssemblyParameters, FIntVector(FMath::DivideAndRoundUp(DofCount * DofCount, 64), 1, 1));

				TShaderMapRef<FContactClothRealtimeRankOneSolveCS> SolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothRealtimeRankOneSolveCS::FParameters* SolveParameters = GraphBuilder.AllocParameters<FContactClothRealtimeRankOneSolveCS::FParameters>();
				SolveParameters->RealtimeRankOneSolveModeCount = StateRef->Settings.ModalModeCount;
				SolveParameters->RealtimeRankOneSolveMatrix = GraphBuilder.CreateSRV(RankOneMatrix);
				SolveParameters->RealtimeRankOneSolveRhs = GraphBuilder.CreateSRV(RankOneRhs);
				SolveParameters->RealtimeRankOneSolution = GraphBuilder.CreateUAV(RankOneSolution);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Rank-One Solve Step %lld", StepToExecute),
					SolveShader, SolveParameters, FIntVector(1, 1, 1));

				TShaderMapRef<FContactClothRealtimeRankOneApplyCS> ApplyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothRealtimeRankOneApplyCS::FParameters* ApplyParameters = GraphBuilder.AllocParameters<FContactClothRealtimeRankOneApplyCS::FParameters>();
				ApplyParameters->RealtimeRankOneApplyParticleCount = StateRef->InitialPositions.Num();
				ApplyParameters->RealtimeRankOneApplyModeCount = StateRef->Settings.ModalModeCount;
				ApplyParameters->RealtimeRankOneApplyColumns = StateRef->Settings.Columns;
				ApplyParameters->RealtimeRankOneMaximumCorrectionCm = StateRef->Settings.RankOneMaximumCorrectionCm;
				ApplyParameters->RealtimeRankOneApplyInputPositions = GraphBuilder.CreateSRV(ModalCorrected);
				ApplyParameters->RealtimeRankOneApplyBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				ApplyParameters->RealtimeRankOneApplySolution = GraphBuilder.CreateSRV(RankOneSolution);
				ApplyParameters->RealtimeRankOneApplyOutputPositions = GraphBuilder.CreateUAV(ModalScratch);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Rank-One Apply Step %lld", StepToExecute),
					ApplyShader, ApplyParameters, FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				Swap(ModalCorrected, ModalScratch);
			}

			TShaderMapRef<FContactClothJacobiCS> JacobiShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRDGBufferRef JacobiCurrent = ModalCorrected;
			FRDGBufferRef JacobiNext = ModalScratch;
			for (int32 Iteration = 0; Iteration < StateRef->Settings.Iterations; ++Iteration)
			{
				FContactClothJacobiCS::FParameters* Parameters = GraphBuilder.AllocParameters<FContactClothJacobiCS::FParameters>();
				ContactClothGpuRealtime::FillJacobiParameters(
					*Parameters,
					*StateRef,
					GraphBuilder,
					JacobiCurrent,
					JacobiNext,
					CollisionSpheres,
					CollisionCapsuleStarts,
					CollisionCapsuleEnds,
					CollisionActivations,
					CollisionData.SphereCount,
					CollisionData.CapsuleCount);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Realtime Jacobi %d", Iteration),
					JacobiShader,
					Parameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				Swap(JacobiCurrent, JacobiNext);
			}

			const bool bResidualReuseEnabled = StateRef->Settings.bEnableResidualReuse;
			FRDGBufferRef ResidualCandidate = JacobiCurrent;
			if (bResidualReuseEnabled)
			{
				TShaderMapRef<FContactClothResidualCaptureCS> ResidualShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothResidualCaptureCS::FParameters* ResidualParameters = GraphBuilder.AllocParameters<FContactClothResidualCaptureCS::FParameters>();
				ResidualParameters->ResidualParticleCount = StateRef->InitialPositions.Num();
				ResidualParameters->ResidualColumns = StateRef->Settings.Columns;
				ResidualParameters->ResidualMaximumCm = StateRef->Settings.MaximumResidualCm;
				ResidualParameters->ResidualResetState = bResetResidualForTopology ? 1u : 0u;
				ResidualParameters->ResidualCandidatePositions = GraphBuilder.CreateSRV(JacobiCurrent);
				ResidualParameters->ResidualPreviousIterationPositions = GraphBuilder.CreateSRV(JacobiNext);
				ResidualParameters->ResidualState = GraphBuilder.CreateUAV(Residual);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Residual Capture Step %lld", StepToExecute),
					ResidualShader,
					ResidualParameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
			}

			const bool bCompleteCcdEnabled = StateRef->Settings.bEnableCompleteCcd;
			if (bCompleteCcdEnabled)
			{
				TShaderMapRef<FContactClothCompleteCcdCS> CcdShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothCompleteCcdCS::FParameters* CcdParameters = GraphBuilder.AllocParameters<FContactClothCompleteCcdCS::FParameters>();
				CcdParameters->CcdParticleCount = StateRef->InitialPositions.Num();
				CcdParameters->CcdColumns = StateRef->Settings.Columns;
				CcdParameters->CcdSphereCount = CollisionData.SphereCount;
				CcdParameters->CcdCapsuleCount = CollisionData.CapsuleCount;
				CcdParameters->CcdSafetyOffsetCm = StateRef->Settings.CcdSafetyOffsetCm;
				CcdParameters->CcdStartPositions = GraphBuilder.CreateSRV(Current);
				CcdParameters->CcdCandidatePositions = GraphBuilder.CreateSRV(JacobiCurrent);
				CcdParameters->CcdCollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				CcdParameters->CcdCollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				CcdParameters->CcdCollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				CcdParameters->CcdOutputPositions = GraphBuilder.CreateUAV(JacobiNext);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Complete CCD Step %lld", StepToExecute),
					CcdShader,
					CcdParameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				JacobiCurrent = JacobiNext;
			}
			if (bResidualReuseEnabled && bCompleteCcdEnabled)
			{
				TShaderMapRef<FContactClothResidualCcdGateCS> ResidualGateShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothResidualCcdGateCS::FParameters* GateParameters = GraphBuilder.AllocParameters<FContactClothResidualCcdGateCS::FParameters>();
				GateParameters->ResidualParticleCount = StateRef->InitialPositions.Num();
				GateParameters->ResidualColumns = StateRef->Settings.Columns;
				GateParameters->ResidualMaximumCcdCorrectionCm = StateRef->Settings.MaximumResidualCcdCorrectionCm;
				GateParameters->ResidualCandidatePositions = GraphBuilder.CreateSRV(ResidualCandidate);
				GateParameters->ResidualFinalPositions = GraphBuilder.CreateSRV(JacobiCurrent);
				GateParameters->ResidualState = GraphBuilder.CreateUAV(Residual);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Residual CCD Gate Step %lld", StepToExecute),
					ResidualGateShader,
					GateParameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
			}

			const bool bResultInWorkA = JacobiCurrent == WorkA;
			if (!StateRef->PendingReadback && StepToExecute % StateRef->Settings.ReadbackIntervalSteps == 0)
			{
				StateRef->PendingReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.Realtime.AsyncReadback"));
				StateRef->PendingReadbackStep = StepToExecute;
				AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadback.Get(), JacobiCurrent, StateRef->InitialPositions.Num() * sizeof(FVector4f));
				if (bResidualReuseEnabled)
				{
					StateRef->PendingResidualReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.Realtime.ResidualReadback"));
					AddEnqueueCopyPass(GraphBuilder, StateRef->PendingResidualReadback.Get(), Residual, StateRef->InitialPositions.Num() * sizeof(FVector4f));
				}
				if (bNdbEnabled)
				{
					StateRef->PendingActivationReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.Realtime.ActivationReadback"));
					AddEnqueueCopyPass(GraphBuilder, StateRef->PendingActivationReadback.Get(), CollisionActivations,
						StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity * sizeof(uint32));
				}
				FScopeLock Lock(&StateRef->DataLock);
				StateRef->Stats.bReadbackPending = true;
			}

			GraphBuilder.Execute();
			const double RenderSubmissionMilliseconds = (FPlatformTime::Seconds() - RenderSubmissionStartSeconds) * 1000.0;
			TRefCountPtr<FRDGPooledBuffer> OldPrevious = StateRef->PreviousBuffer;
			TRefCountPtr<FRDGPooledBuffer> OldCurrent = StateRef->CurrentBuffer;
			TRefCountPtr<FRDGPooledBuffer> ResultBuffer = bResultInWorkA ? StateRef->WorkBufferA : StateRef->WorkBufferB;
			StateRef->PreviousBuffer = OldCurrent;
			StateRef->CurrentBuffer = ResultBuffer;
			if (bResultInWorkA)
			{
				StateRef->WorkBufferA = OldPrevious;
			}
			else
			{
				StateRef->WorkBufferB = OldPrevious;
			}
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.StepsExecuted = StepToExecute;
			StateRef->Stats.LatestRenderSubmissionMilliseconds = RenderSubmissionMilliseconds;
			StateRef->Stats.TotalRenderSubmissionMilliseconds += RenderSubmissionMilliseconds;
			StateRef->Stats.MaximumRenderSubmissionMilliseconds = FMath::Max(StateRef->Stats.MaximumRenderSubmissionMilliseconds, RenderSubmissionMilliseconds);
			if (bModalEnabled)
			{
				StateRef->Stats.ModalCorrectionsExecuted++;
			}
			if (StateRef->Settings.bEnableRankOneCollisionHessian)
			{
				StateRef->Stats.RankOneCollisionSystemsExecuted++;
			}
			if (bNdbEnabled)
			{
				StateRef->Stats.NdbStateUpdatesExecuted++;
			}
			if (bCompleteCcdEnabled)
			{
				StateRef->Stats.CompleteCcdPassesExecuted++;
			}
			if (bResidualReuseEnabled)
			{
				StateRef->Stats.ResidualReusePassesExecuted++;
			}
		});
	return true;
}

void FContactClothGpuRealtimeSimulation::UpdateCollisionPrimitives(
	TConstArrayView<FContactClothGpuSphere> Spheres,
	TConstArrayView<FContactClothGpuCapsule> Capsules)
{
	if (!IsInGameThread() || !State.IsValid())
	{
		return;
	}
	FScopeLock Lock(&State->DataLock);
	if (State->bShutdown)
	{
		return;
	}
	State->Settings.CollisionSpheres.Reset(Spheres.Num());
	State->Settings.CollisionSpheres.Append(Spheres.GetData(), Spheres.Num());
	State->Settings.CollisionCapsules.Reset(Capsules.Num());
	State->Settings.CollisionCapsules.Append(Capsules.GetData(), Capsules.Num());
}

void FContactClothGpuRealtimeSimulation::PollReadback()
{
	if (!IsInGameThread() || !State.IsValid())
	{
		return;
	}
	const TSharedRef<FContactClothGpuRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimePollReadback)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			if (!StateRef->bShutdown)
			{
				ContactClothGpuRealtime::ConsumeReadyReadback_RenderThread(StateRef);
			}
		});
}

void FContactClothGpuRealtimeSimulation::RequestReadback()
{
	if (!IsInGameThread() || !State.IsValid())
	{
		return;
	}
	const TSharedRef<FContactClothGpuRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimeRequestReadback)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			if (StateRef->bShutdown || !StateRef->CurrentBuffer)
			{
				return;
			}
			ContactClothGpuRealtime::ConsumeReadyReadback_RenderThread(StateRef);
			if (StateRef->PendingReadback || StateRef->PendingResidualReadback || StateRef->PendingActivationReadback)
			{
				return;
			}
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef Current = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentBuffer, TEXT("ContactCloth.Realtime.ReadbackCurrent"));
			FRDGBufferRef Residual = GraphBuilder.RegisterExternalBuffer(StateRef->ResidualBuffer, TEXT("ContactCloth.Realtime.ReadbackResidual"));
			FRDGBufferRef CollisionActivations = GraphBuilder.RegisterExternalBuffer(StateRef->CollisionActivationBuffer, TEXT("ContactCloth.Realtime.ReadbackActivations"));
			StateRef->PendingReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.Realtime.RequestedReadback"));
			{
				FScopeLock Lock(&StateRef->DataLock);
				StateRef->PendingReadbackStep = StateRef->Stats.StepsExecuted;
				StateRef->Stats.bReadbackPending = true;
			}
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadback.Get(), Current, StateRef->InitialPositions.Num() * sizeof(FVector4f));
			if (StateRef->Settings.bEnableResidualReuse)
			{
				StateRef->PendingResidualReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.Realtime.RequestedResidualReadback"));
				AddEnqueueCopyPass(GraphBuilder, StateRef->PendingResidualReadback.Get(), Residual, StateRef->InitialPositions.Num() * sizeof(FVector4f));
			}
			if (StateRef->Settings.bEnableNonDistanceBarrier)
			{
				StateRef->PendingActivationReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.Realtime.RequestedActivationReadback"));
				AddEnqueueCopyPass(GraphBuilder, StateRef->PendingActivationReadback.Get(), CollisionActivations,
					StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity * sizeof(uint32));
			}
			GraphBuilder.Execute();
		});
}

bool FContactClothGpuRealtimeSimulation::ConsumeLatestPositions(TArray<FVector3f>& OutPositions, int64& OutStepIndex)
{
	if (!State.IsValid())
	{
		return false;
	}
	FScopeLock Lock(&State->DataLock);
	if (!State->bLatestPositionsAvailable)
	{
		return false;
	}
	OutPositions = State->LatestPositions;
	OutStepIndex = State->Stats.LatestReadbackStep;
	State->bLatestPositionsAvailable = false;
	return true;
}

FContactClothGpuRealtimeStats FContactClothGpuRealtimeSimulation::GetStats() const
{
	if (!State.IsValid())
	{
		return FContactClothGpuRealtimeStats();
	}
	FScopeLock Lock(&State->DataLock);
	return State->Stats;
}

void FContactClothGpuRealtimeSimulation::FlushAndWaitForReadbackForValidation()
{
	if (!IsInGameThread() || !State.IsValid())
	{
		return;
	}
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimeValidationWait)(
		[](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.BlockUntilGPUIdle();
		});
	FlushRenderingCommands();
	RequestReadback();
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimeValidationReadbackWait)(
		[](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.BlockUntilGPUIdle();
		});
	FlushRenderingCommands();
	PollReadback();
	FlushRenderingCommands();
}

void FContactClothGpuRealtimeSimulation::Shutdown()
{
	if (!State.IsValid())
	{
		return;
	}
	const TSharedRef<FContactClothGpuRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	{
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->bShutdown = true;
	}
	ENQUEUE_RENDER_COMMAND(ContactClothGpuRealtimeShutdown)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			StateRef->PendingReadback.Reset();
			StateRef->PendingResidualReadback.Reset();
			StateRef->PendingActivationReadback.Reset();
			StateRef->CurrentBuffer.SafeRelease();
			StateRef->PreviousBuffer.SafeRelease();
			StateRef->WorkBufferA.SafeRelease();
			StateRef->WorkBufferB.SafeRelease();
			StateRef->RestBuffer.SafeRelease();
			StateRef->ModalBasisBuffer.SafeRelease();
			StateRef->ModalInverseSystemBuffer.SafeRelease();
			StateRef->ModalRhsBuffer.SafeRelease();
			StateRef->ModalSystemBuffer.SafeRelease();
			StateRef->ModalParticleOuterProductBuffer.SafeRelease();
			StateRef->RankOneMatrixBuffer.SafeRelease();
			StateRef->RankOneRhsBuffer.SafeRelease();
			StateRef->RankOneSolutionBuffer.SafeRelease();
			StateRef->CollisionActivationBuffer.SafeRelease();
			StateRef->ResidualBuffer.SafeRelease();
		});
	State.Reset();
}
