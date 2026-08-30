#include "SoftBodyGpuXpbd.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "Misc/ScopeLock.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FSoftBodyXpbdIntegrateCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdIntegrateCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdIntegrateCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, FixedDeltaSecondsSquared)
		SHADER_PARAMETER(float, VelocityDamping)
		SHADER_PARAMETER(FVector3f, GravityCmPerSecondSquared)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CurrentPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PreviousPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RestPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdIntegrateCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "IntegrateCS", SF_Compute);

class FSoftBodyXpbdDistanceConstraintCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdDistanceConstraintCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdDistanceConstraintCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, EdgeConstraintCount)
		SHADER_PARAMETER(float, FixedDeltaSecondsSquared)
		SHADER_PARAMETER(float, SoftEdgeCompliance)
		SHADER_PARAMETER(float, StiffEdgeCompliance)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SolverInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, EdgeVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, EdgeRestLengthsCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EdgeMaterialIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, EdgeLambdas)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, EdgeCorrectionOutputs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdDistanceConstraintCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "DistanceConstraintCS", SF_Compute);

class FSoftBodyXpbdVolumeConstraintCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdVolumeConstraintCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdVolumeConstraintCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TetrahedronConstraintCount)
		SHADER_PARAMETER(float, FixedDeltaSecondsSquared)
		SHADER_PARAMETER(float, SoftVolumeCompliance)
		SHADER_PARAMETER(float, StiffVolumeCompliance)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SolverInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TetrahedronVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, TetrahedronRestVolumesCm3)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TetrahedronMaterialIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, TetrahedronLambdas)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TetrahedronCorrectionOutputs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdVolumeConstraintCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "VolumeConstraintCS", SF_Compute);

class FSoftBodyXpbdGatherApplyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdGatherApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdGatherApplyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, JacobiRelaxation)
		SHADER_PARAMETER(float, MaximumVertexCorrectionCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RestPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexEdgeOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexEdgeConstraintIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexEdgeCorners)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexTetrahedronOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexTetrahedronConstraintIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexTetrahedronCorners)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SolverInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, EdgeCorrectionInputs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TetrahedronCorrectionInputs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SolverOutputPositions)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdGatherApplyCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "GatherApplyCS", SF_Compute);

class FSoftBodyXpbdGatherCorrectionCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdGatherCorrectionCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdGatherCorrectionCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexEdgeOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexEdgeConstraintIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexEdgeCorners)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexTetrahedronOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexTetrahedronConstraintIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexTetrahedronCorners)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, EdgeCorrectionInputs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TetrahedronCorrectionInputs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, LocalCorrectionOutputs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdGatherCorrectionCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "GatherCorrectionCS", SF_Compute);

class FSoftBodyXpbdProjectLowFrequencyPartialCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdProjectLowFrequencyPartialCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdProjectLowFrequencyPartialCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, LowFrequencyBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LocalCorrectionInputs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, LowFrequencyPartialR0Outputs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, LowFrequencyPartialR1Outputs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdProjectLowFrequencyPartialCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "ProjectLowFrequencyPartialCS", SF_Compute);

class FSoftBodyXpbdFinalizeLowFrequencyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdFinalizeLowFrequencyCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdFinalizeLowFrequencyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, LowFrequencyPartialCount)
		SHADER_PARAMETER(FVector4f, LowFrequencyInverseSystem)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LowFrequencyPartialR0Inputs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LowFrequencyPartialR1Inputs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, LowFrequencyCoefficientOutputs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdFinalizeLowFrequencyCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "FinalizeLowFrequencyCS", SF_Compute);

class FSoftBodyXpbdApplyLowFrequencyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdApplyLowFrequencyCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdApplyLowFrequencyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, JacobiRelaxation)
		SHADER_PARAMETER(float, MaximumVertexCorrectionCm)
		SHADER_PARAMETER(float, LowFrequencyCorrectionStrength)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RestPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SolverInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, LowFrequencyBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LocalCorrectionInputs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LowFrequencyCoefficientInputs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SolverOutputPositions)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdApplyLowFrequencyCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "ApplyLowFrequencyCS", SF_Compute);

class FSoftBodyXpbdGroundContactCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSoftBodyXpbdGroundContactCS);
	SHADER_USE_PARAMETER_STRUCT(FSoftBodyXpbdGroundContactCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, GroundPlaneZCm)
		SHADER_PARAMETER(float, GroundContactStiffness)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RestPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ContactInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ContactOutputPositions)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		Environment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSoftBodyXpbdGroundContactCS,
	"/Plugin/PhysicsResearch/Private/SoftBodyGpuXpbd.usf", "GroundContactCS", SF_Compute);

class FSoftBodyGpuXpbdState : public TSharedFromThis<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe>
{
public:
	struct FGpuTimingSlot
	{
		FRenderQueryRHIRef StartQuery;
		FRenderQueryRHIRef EndQuery;
		int64 Step = 0;
		bool bPending = false;
	};

	mutable FCriticalSection DataLock;
	FSoftBodyGpuXpbdSettings Settings;
	FSoftBodyGpuXpbdInput Input;
	TArray<FVector4f> RestPositionUpload;
	TArray<FVector2f> LowFrequencyBasisUpload;
	FVector4f LowFrequencyInverseSystem = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	TArray<FVector3f> LatestPositions;
	TRefCountPtr<FRDGPooledBuffer> CurrentBuffer;
	TRefCountPtr<FRDGPooledBuffer> PreviousBuffer;
	TRefCountPtr<FRDGPooledBuffer> WorkBufferA;
	TRefCountPtr<FRDGPooledBuffer> WorkBufferB;
	TRefCountPtr<FRDGPooledBuffer> RestPositionBuffer;
	TRefCountPtr<FRDGPooledBuffer> InverseMassBuffer;
	TRefCountPtr<FRDGPooledBuffer> EdgeVertexBuffer;
	TRefCountPtr<FRDGPooledBuffer> EdgeRestLengthBuffer;
	TRefCountPtr<FRDGPooledBuffer> EdgeMaterialBuffer;
	TRefCountPtr<FRDGPooledBuffer> VertexEdgeOffsetBuffer;
	TRefCountPtr<FRDGPooledBuffer> VertexEdgeIndexBuffer;
	TRefCountPtr<FRDGPooledBuffer> VertexEdgeCornerBuffer;
	TRefCountPtr<FRDGPooledBuffer> TetVertexBuffer;
	TRefCountPtr<FRDGPooledBuffer> TetRestVolumeBuffer;
	TRefCountPtr<FRDGPooledBuffer> TetMaterialBuffer;
	TRefCountPtr<FRDGPooledBuffer> VertexTetOffsetBuffer;
	TRefCountPtr<FRDGPooledBuffer> VertexTetIndexBuffer;
	TRefCountPtr<FRDGPooledBuffer> VertexTetCornerBuffer;
	TRefCountPtr<FRDGPooledBuffer> EdgeLambdaBuffer;
	TRefCountPtr<FRDGPooledBuffer> TetLambdaBuffer;
	TRefCountPtr<FRDGPooledBuffer> EdgeCorrectionBuffer;
	TRefCountPtr<FRDGPooledBuffer> TetCorrectionBuffer;
	TRefCountPtr<FRDGPooledBuffer> LowFrequencyBasisBuffer;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadback;
	TArray<FGpuTimingSlot> GpuTimingSlots;
	TArray<int64> GpuTimingSteps;
	TArray<double> GpuStepMilliseconds;
	int64 PendingReadbackStep = 0;
	FSoftBodyGpuXpbdStats Stats;
	bool bLatestPositionsAvailable = false;
	bool bShutdown = false;
};

namespace SoftBodyGpuXpbd
{
template <typename ElementType>
FRDGBufferRef CreateUploadedBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, const TArray<ElementType>& Values)
{
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(ElementType), Values.Num()), Name);
	GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(ElementType));
	return Buffer;
}

bool IsFiniteVector(const FVector3f& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}

bool ValidateInput(const FSoftBodyGpuXpbdInput& Input, const FSoftBodyGpuXpbdSettings& Settings)
{
	const int32 VertexCount = Input.RestPositions.Num();
	if (VertexCount <= 0 || Input.InverseMasses.Num() != VertexCount
		|| Input.EdgeVertices.IsEmpty() || Input.EdgeRestLengthsCm.Num() != Input.EdgeVertices.Num()
		|| Input.EdgeMaterialIds.Num() != Input.EdgeVertices.Num()
		|| Input.VertexEdgeOffsets.Num() != VertexCount + 1
		|| Input.VertexEdgeConstraintIndices.Num() != Input.EdgeVertices.Num() * 2
		|| Input.VertexEdgeCorners.Num() != Input.VertexEdgeConstraintIndices.Num()
		|| Input.TetrahedronVertices.IsEmpty()
		|| Input.TetrahedronRestVolumesCm3.Num() != Input.TetrahedronVertices.Num()
		|| Input.TetrahedronMaterialIds.Num() != Input.TetrahedronVertices.Num()
		|| Input.VertexTetrahedronOffsets.Num() != VertexCount + 1
		|| Input.VertexTetrahedronConstraintIndices.Num() != Input.TetrahedronVertices.Num() * 4
		|| Input.VertexTetrahedronCorners.Num() != Input.VertexTetrahedronConstraintIndices.Num())
	{
		return false;
	}
	if (Input.VertexEdgeOffsets[0] != 0 || Input.VertexEdgeOffsets.Last() != static_cast<uint32>(Input.VertexEdgeConstraintIndices.Num())
		|| Input.VertexTetrahedronOffsets[0] != 0
		|| Input.VertexTetrahedronOffsets.Last() != static_cast<uint32>(Input.VertexTetrahedronConstraintIndices.Num()))
	{
		return false;
	}
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
	{
		if (!IsFiniteVector(Input.RestPositions[Vertex]) || !FMath::IsFinite(Input.InverseMasses[Vertex])
			|| Input.InverseMasses[Vertex] < 0.0f
			|| Input.VertexEdgeOffsets[Vertex] > Input.VertexEdgeOffsets[Vertex + 1]
			|| Input.VertexTetrahedronOffsets[Vertex] > Input.VertexTetrahedronOffsets[Vertex + 1]) return false;
	}
	for (int32 EdgeIndex = 0; EdgeIndex < Input.EdgeVertices.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = Input.EdgeVertices[EdgeIndex];
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= VertexCount || Edge.Y >= VertexCount || Edge.X == Edge.Y
			|| !FMath::IsFinite(Input.EdgeRestLengthsCm[EdgeIndex]) || Input.EdgeRestLengthsCm[EdgeIndex] <= 0.0f
			|| Input.EdgeMaterialIds[EdgeIndex] > 1u) return false;
	}
	for (int32 TetIndex = 0; TetIndex < Input.TetrahedronVertices.Num(); ++TetIndex)
	{
		const FIntVector4 Tet = Input.TetrahedronVertices[TetIndex];
		if (Tet.X < 0 || Tet.Y < 0 || Tet.Z < 0 || Tet.W < 0
			|| Tet.X >= VertexCount || Tet.Y >= VertexCount || Tet.Z >= VertexCount || Tet.W >= VertexCount
			|| !FMath::IsFinite(Input.TetrahedronRestVolumesCm3[TetIndex])
			|| Input.TetrahedronRestVolumesCm3[TetIndex] <= 0.0f
			|| Input.TetrahedronMaterialIds[TetIndex] > 1u) return false;
	}
	const bool bFiniteSettings = FMath::IsFinite(Settings.FixedDeltaSeconds)
		&& IsFiniteVector(Settings.GravityCmPerSecondSquared)
		&& FMath::IsFinite(Settings.VelocityDamping) && FMath::IsFinite(Settings.JacobiRelaxation)
		&& FMath::IsFinite(Settings.MaximumVertexCorrectionCm)
		&& FMath::IsFinite(Settings.SoftEdgeCompliance) && FMath::IsFinite(Settings.StiffEdgeCompliance)
		&& FMath::IsFinite(Settings.SoftVolumeCompliance) && FMath::IsFinite(Settings.StiffVolumeCompliance)
		&& FMath::IsFinite(Settings.LowFrequencyCorrectionStrength)
		&& FMath::IsFinite(Settings.GroundPlaneZCm)
		&& FMath::IsFinite(Settings.GroundContactStiffness);
	return bFiniteSettings && Settings.FixedDeltaSeconds > 0.0f && Settings.SolverIterations > 0
		&& Settings.VelocityDamping >= 0.0f && Settings.VelocityDamping <= 1.0f
		&& Settings.JacobiRelaxation > 0.0f && Settings.JacobiRelaxation <= 1.0f
		&& Settings.MaximumVertexCorrectionCm > 0.0f
		&& Settings.SoftEdgeCompliance >= 0.0f && Settings.StiffEdgeCompliance >= 0.0f
		&& Settings.SoftVolumeCompliance >= 0.0f && Settings.StiffVolumeCompliance >= 0.0f
		&& Settings.ReadbackIntervalSteps > 0 && Settings.GpuTimingWarmupSteps >= 0
		&& Settings.LowFrequencyCorrectionStrength >= 0.0f && Settings.LowFrequencyCorrectionStrength <= 1.0f
		&& Settings.LowFrequencyCorrectionInterval > 0
		&& Settings.GroundContactStiffness >= 0.0f && Settings.GroundContactStiffness <= 1.0f;
}

bool BuildLowFrequencyBasis(FSoftBodyGpuXpbdState& State)
{
	const TArray<FVector3f>& Rest = State.Input.RestPositions;
	FVector3f Minimum = Rest[0];
	FVector3f Maximum = Rest[0];
	for (const FVector3f& Position : Rest)
	{
		Minimum.X = FMath::Min(Minimum.X, Position.X); Minimum.Y = FMath::Min(Minimum.Y, Position.Y); Minimum.Z = FMath::Min(Minimum.Z, Position.Z);
		Maximum.X = FMath::Max(Maximum.X, Position.X); Maximum.Y = FMath::Max(Maximum.Y, Position.Y); Maximum.Z = FMath::Max(Maximum.Z, Position.Z);
	}
	const FVector3f Extent = Maximum - Minimum;
	const int32 Axis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
	const float AxisMinimum = Minimum[Axis];
	const float AxisExtent = Extent[Axis];
	if (AxisExtent <= UE_SMALL_NUMBER) return false;
	double S00 = 0.0, S01 = 0.0, S11 = 0.0;
	State.LowFrequencyBasisUpload.SetNumUninitialized(Rest.Num());
	for (int32 Vertex = 0; Vertex < Rest.Num(); ++Vertex)
	{
		const float T = FMath::Clamp((Rest[Vertex][Axis] - AxisMinimum) / AxisExtent, 0.0f, 1.0f);
		const FVector2f Phi(T, T * T);
		State.LowFrequencyBasisUpload[Vertex] = Phi;
		if (State.Input.InverseMasses[Vertex] > 0.0f)
		{
			S00 += Phi.X * Phi.X; S01 += Phi.X * Phi.Y; S11 += Phi.Y * Phi.Y;
		}
	}
	const double Determinant = S00 * S11 - S01 * S01;
	if (Determinant <= UE_DOUBLE_SMALL_NUMBER) return false;
	State.LowFrequencyInverseSystem = FVector4f(
		static_cast<float>(S11 / Determinant), static_cast<float>(-S01 / Determinant),
		static_cast<float>(-S01 / Determinant), static_cast<float>(S00 / Determinant));
	return true;
}

void UpdateTimingStatistics(FSoftBodyGpuXpbdState& State)
{
	if (State.GpuStepMilliseconds.IsEmpty()) return;
	double Total = 0.0;
	for (double Sample : State.GpuStepMilliseconds) Total += Sample;
	State.Stats.MeanGpuStepMilliseconds = Total / State.GpuStepMilliseconds.Num();
	TArray<double> Sorted = State.GpuStepMilliseconds;
	Sorted.Sort();
	State.Stats.MedianGpuStepMilliseconds = Sorted[Sorted.Num() / 2];
	auto Percentile = [](const TArray<double>& Values, double P)
	{
		const int32 Index = FMath::Clamp(FMath::CeilToInt(P * Values.Num()) - 1, 0, Values.Num() - 1);
		return Values[Index];
	};
	State.Stats.P95GpuStepMilliseconds = Percentile(Sorted, 0.95);
	State.Stats.P99GpuStepMilliseconds = Percentile(Sorted, 0.99);
	State.Stats.MaximumGpuStepMilliseconds = Sorted.Last();
	TArray<double> Steady;
	for (int32 Index = 0; Index < State.GpuStepMilliseconds.Num(); ++Index)
	{
		if (State.GpuTimingSteps[Index] > State.Settings.GpuTimingWarmupSteps) Steady.Add(State.GpuStepMilliseconds[Index]);
	}
	State.Stats.SteadyStateGpuTimingSamples = Steady.Num();
	if (!Steady.IsEmpty())
	{
		double SteadyTotal = 0.0;
		for (double Sample : Steady) SteadyTotal += Sample;
		State.Stats.MeanSteadyStateGpuStepMilliseconds = SteadyTotal / Steady.Num();
		Steady.Sort();
		State.Stats.MedianSteadyStateGpuStepMilliseconds = Steady[Steady.Num() / 2];
		State.Stats.P95SteadyStateGpuStepMilliseconds = Percentile(Steady, 0.95);
		State.Stats.P99SteadyStateGpuStepMilliseconds = Percentile(Steady, 0.99);
		State.Stats.MaximumSteadyStateGpuStepMilliseconds = Steady.Last();
	}
}

void ConsumeReadyGpuTimings_RenderThread(const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe>& State)
{
	if (!State->Stats.bGpuTimingSupported) return;
	for (FSoftBodyGpuXpbdState::FGpuTimingSlot& Slot : State->GpuTimingSlots)
	{
		if (!Slot.bPending) continue;
		uint64 StartMicroseconds = 0;
		uint64 EndMicroseconds = 0;
		if (!RHIGetRenderQueryResult(Slot.StartQuery.GetReference(), StartMicroseconds, false)
			|| !RHIGetRenderQueryResult(Slot.EndQuery.GetReference(), EndMicroseconds, false)) continue;
		FScopeLock Lock(&State->DataLock);
		if (EndMicroseconds > StartMicroseconds)
		{
			State->GpuTimingSteps.Add(Slot.Step);
			State->GpuStepMilliseconds.Add(static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0);
			State->Stats.GpuTimingSamplesCompleted++;
			UpdateTimingStatistics(*State);
		}
		else
		{
			State->Stats.GpuTimingSamplesSkipped++;
		}
		Slot.Step = 0;
		Slot.bPending = false;
	}
}

void ConsumeReadyReadback_RenderThread(const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe>& State)
{
	if (!State->PendingReadback || !State->PendingReadback->IsReady()) return;
	const uint32 NumBytes = State->RestPositionUpload.Num() * sizeof(FVector4f);
	const void* Data = State->PendingReadback->Lock(NumBytes);
	if (Data)
	{
		const FVector4f* Values = static_cast<const FVector4f*>(Data);
		FScopeLock Lock(&State->DataLock);
		State->LatestPositions.SetNumUninitialized(State->RestPositionUpload.Num());
		for (int32 Index = 0; Index < State->LatestPositions.Num(); ++Index)
		{
			State->LatestPositions[Index] = FVector3f(Values[Index].X, Values[Index].Y, Values[Index].Z);
		}
		State->Stats.ReadbacksCompleted++;
		State->Stats.LatestReadbackStep = State->PendingReadbackStep;
		State->Stats.bReadbackPending = false;
		State->bLatestPositionsAvailable = true;
	}
	State->PendingReadback->Unlock();
	State->PendingReadback.Reset();
	State->PendingReadbackStep = 0;
}
}

FSoftBodyGpuXpbdSimulation::FSoftBodyGpuXpbdSimulation() = default;
FSoftBodyGpuXpbdSimulation::~FSoftBodyGpuXpbdSimulation() { Shutdown(); }

bool FSoftBodyGpuXpbdSimulation::Initialize(
	const FSoftBodyGpuXpbdInput& Input,
	const FSoftBodyGpuXpbdSettings& Settings)
{
	if (!IsInGameThread() || !GDynamicRHI || !SoftBodyGpuXpbd::ValidateInput(Input, Settings)) return false;
	Shutdown();
	State = MakeShared<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe>();
	State->Input = Input;
	State->Settings = Settings;
	State->RestPositionUpload.Reserve(Input.RestPositions.Num());
	for (const FVector3f& Position : Input.RestPositions) State->RestPositionUpload.Emplace(Position, 0.0f);
	if (Settings.bEnableLowFrequencyCorrection && !SoftBodyGpuXpbd::BuildLowFrequencyBasis(*State))
	{
		State.Reset();
		return false;
	}
	const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdInitialize)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown) return;
		bool bGpuTimingSupported = GSupportsTimestampRenderQueries;
		if (bGpuTimingSupported)
		{
			StateRef->GpuTimingSlots.SetNum(8);
			for (FSoftBodyGpuXpbdState::FGpuTimingSlot& Slot : StateRef->GpuTimingSlots)
			{
				Slot.StartQuery = RHICreateRenderQuery(RQT_AbsoluteTime);
				Slot.EndQuery = RHICreateRenderQuery(RQT_AbsoluteTime);
				bGpuTimingSupported = bGpuTimingSupported && Slot.StartQuery.IsValid() && Slot.EndQuery.IsValid();
			}
			if (!bGpuTimingSupported) StateRef->GpuTimingSlots.Reset();
		}
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGBufferDesc PositionDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->RestPositionUpload.Num());
		PositionDesc.Usage = static_cast<EBufferUsageFlags>(PositionDesc.Usage | BUF_SourceCopy);
		FRDGBufferRef Current = GraphBuilder.CreateBuffer(PositionDesc, TEXT("SoftBody.Xpbd.Current"));
		FRDGBufferRef Previous = GraphBuilder.CreateBuffer(PositionDesc, TEXT("SoftBody.Xpbd.Previous"));
		FRDGBufferRef WorkA = GraphBuilder.CreateBuffer(PositionDesc, TEXT("SoftBody.Xpbd.WorkA"));
		FRDGBufferRef WorkB = GraphBuilder.CreateBuffer(PositionDesc, TEXT("SoftBody.Xpbd.WorkB"));
		const uint32 PositionBytes = StateRef->RestPositionUpload.Num() * sizeof(FVector4f);
		GraphBuilder.QueueBufferUpload(Current, StateRef->RestPositionUpload.GetData(), PositionBytes);
		GraphBuilder.QueueBufferUpload(Previous, StateRef->RestPositionUpload.GetData(), PositionBytes);

		FRDGBufferRef RestPositions = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.RestPositions"), StateRef->RestPositionUpload);
		FRDGBufferRef InverseMasses = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.InverseMasses"), StateRef->Input.InverseMasses);
		FRDGBufferRef EdgeVertices = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.EdgeVertices"), StateRef->Input.EdgeVertices);
		FRDGBufferRef EdgeRestLengths = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.EdgeRestLengths"), StateRef->Input.EdgeRestLengthsCm);
		FRDGBufferRef EdgeMaterials = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.EdgeMaterials"), StateRef->Input.EdgeMaterialIds);
		FRDGBufferRef VertexEdgeOffsets = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.VertexEdgeOffsets"), StateRef->Input.VertexEdgeOffsets);
		FRDGBufferRef VertexEdgeIndices = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.VertexEdgeIndices"), StateRef->Input.VertexEdgeConstraintIndices);
		FRDGBufferRef VertexEdgeCorners = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.VertexEdgeCorners"), StateRef->Input.VertexEdgeCorners);
		FRDGBufferRef TetVertices = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.TetVertices"), StateRef->Input.TetrahedronVertices);
		FRDGBufferRef TetRestVolumes = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.TetRestVolumes"), StateRef->Input.TetrahedronRestVolumesCm3);
		FRDGBufferRef TetMaterials = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.TetMaterials"), StateRef->Input.TetrahedronMaterialIds);
		FRDGBufferRef VertexTetOffsets = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.VertexTetOffsets"), StateRef->Input.VertexTetrahedronOffsets);
		FRDGBufferRef VertexTetIndices = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.VertexTetIndices"), StateRef->Input.VertexTetrahedronConstraintIndices);
		FRDGBufferRef VertexTetCorners = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.VertexTetCorners"), StateRef->Input.VertexTetrahedronCorners);
		FRDGBufferRef EdgeLambdas = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), StateRef->Input.EdgeVertices.Num()), TEXT("SoftBody.Xpbd.EdgeLambdas"));
		FRDGBufferRef TetLambdas = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), StateRef->Input.TetrahedronVertices.Num()), TEXT("SoftBody.Xpbd.TetLambdas"));
		FRDGBufferRef EdgeCorrections = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->Input.EdgeVertices.Num() * 2), TEXT("SoftBody.Xpbd.EdgeCorrections"));
		FRDGBufferRef TetCorrections = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->Input.TetrahedronVertices.Num() * 4), TEXT("SoftBody.Xpbd.TetCorrections"));
		FRDGBufferRef LowFrequencyBasis = nullptr;
		if (StateRef->Settings.bEnableLowFrequencyCorrection)
		{
			LowFrequencyBasis = SoftBodyGpuXpbd::CreateUploadedBuffer(GraphBuilder, TEXT("SoftBody.Xpbd.LowFrequencyBasis"), StateRef->LowFrequencyBasisUpload);
		}
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EdgeLambdas), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TetLambdas), 0u);

		StateRef->CurrentBuffer = GraphBuilder.ConvertToExternalBuffer(Current);
		StateRef->PreviousBuffer = GraphBuilder.ConvertToExternalBuffer(Previous);
		StateRef->WorkBufferA = GraphBuilder.ConvertToExternalBuffer(WorkA);
		StateRef->WorkBufferB = GraphBuilder.ConvertToExternalBuffer(WorkB);
		StateRef->RestPositionBuffer = GraphBuilder.ConvertToExternalBuffer(RestPositions);
		StateRef->InverseMassBuffer = GraphBuilder.ConvertToExternalBuffer(InverseMasses);
		StateRef->EdgeVertexBuffer = GraphBuilder.ConvertToExternalBuffer(EdgeVertices);
		StateRef->EdgeRestLengthBuffer = GraphBuilder.ConvertToExternalBuffer(EdgeRestLengths);
		StateRef->EdgeMaterialBuffer = GraphBuilder.ConvertToExternalBuffer(EdgeMaterials);
		StateRef->VertexEdgeOffsetBuffer = GraphBuilder.ConvertToExternalBuffer(VertexEdgeOffsets);
		StateRef->VertexEdgeIndexBuffer = GraphBuilder.ConvertToExternalBuffer(VertexEdgeIndices);
		StateRef->VertexEdgeCornerBuffer = GraphBuilder.ConvertToExternalBuffer(VertexEdgeCorners);
		StateRef->TetVertexBuffer = GraphBuilder.ConvertToExternalBuffer(TetVertices);
		StateRef->TetRestVolumeBuffer = GraphBuilder.ConvertToExternalBuffer(TetRestVolumes);
		StateRef->TetMaterialBuffer = GraphBuilder.ConvertToExternalBuffer(TetMaterials);
		StateRef->VertexTetOffsetBuffer = GraphBuilder.ConvertToExternalBuffer(VertexTetOffsets);
		StateRef->VertexTetIndexBuffer = GraphBuilder.ConvertToExternalBuffer(VertexTetIndices);
		StateRef->VertexTetCornerBuffer = GraphBuilder.ConvertToExternalBuffer(VertexTetCorners);
		StateRef->EdgeLambdaBuffer = GraphBuilder.ConvertToExternalBuffer(EdgeLambdas);
		StateRef->TetLambdaBuffer = GraphBuilder.ConvertToExternalBuffer(TetLambdas);
		StateRef->EdgeCorrectionBuffer = GraphBuilder.ConvertToExternalBuffer(EdgeCorrections);
		StateRef->TetCorrectionBuffer = GraphBuilder.ConvertToExternalBuffer(TetCorrections);
		if (LowFrequencyBasis) StateRef->LowFrequencyBasisBuffer = GraphBuilder.ConvertToExternalBuffer(LowFrequencyBasis);
		GraphBuilder.Execute();
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->Stats.bInitialized = true;
		StateRef->Stats.bGpuTimingSupported = bGpuTimingSupported;
	});
	return true;
}

bool FSoftBodyGpuXpbdSimulation::EnqueueStep()
{
	if (!IsInGameThread() || !State.IsValid()) return false;
	const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	int64 StepToExecute = 0;
	{
		FScopeLock Lock(&StateRef->DataLock);
		if (StateRef->bShutdown) return false;
		StepToExecute = ++StateRef->Stats.StepsEnqueued;
	}
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdStep)([StateRef, StepToExecute](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown || !StateRef->CurrentBuffer) return;
		SoftBodyGpuXpbd::ConsumeReadyReadback_RenderThread(StateRef);
		SoftBodyGpuXpbd::ConsumeReadyGpuTimings_RenderThread(StateRef);
		FSoftBodyGpuXpbdState::FGpuTimingSlot* GpuTimingSlot = nullptr;
		if (StateRef->Stats.bGpuTimingSupported)
		{
			for (FSoftBodyGpuXpbdState::FGpuTimingSlot& Slot : StateRef->GpuTimingSlots)
			{
				if (!Slot.bPending) { GpuTimingSlot = &Slot; break; }
			}
			if (GpuTimingSlot) RHICmdList.EndRenderQuery(GpuTimingSlot->StartQuery.GetReference());
			else
			{
				FScopeLock Lock(&StateRef->DataLock);
				StateRef->Stats.GpuTimingSamplesSkipped++;
			}
		}
		FRDGBuilder GraphBuilder(RHICmdList);
		auto Register = [&GraphBuilder](const TRefCountPtr<FRDGPooledBuffer>& Buffer, const TCHAR* Name)
		{
			return GraphBuilder.RegisterExternalBuffer(Buffer, Name);
		};
		FRDGBufferRef Current = Register(StateRef->CurrentBuffer, TEXT("SoftBody.Xpbd.Current"));
		FRDGBufferRef Previous = Register(StateRef->PreviousBuffer, TEXT("SoftBody.Xpbd.Previous"));
		FRDGBufferRef WorkA = Register(StateRef->WorkBufferA, TEXT("SoftBody.Xpbd.WorkA"));
		FRDGBufferRef WorkB = Register(StateRef->WorkBufferB, TEXT("SoftBody.Xpbd.WorkB"));
		FRDGBufferRef RestPositions = Register(StateRef->RestPositionBuffer, TEXT("SoftBody.Xpbd.RestPositions"));
		FRDGBufferRef InverseMasses = Register(StateRef->InverseMassBuffer, TEXT("SoftBody.Xpbd.InverseMasses"));
		FRDGBufferRef EdgeVertices = Register(StateRef->EdgeVertexBuffer, TEXT("SoftBody.Xpbd.EdgeVertices"));
		FRDGBufferRef EdgeRestLengths = Register(StateRef->EdgeRestLengthBuffer, TEXT("SoftBody.Xpbd.EdgeRestLengths"));
		FRDGBufferRef EdgeMaterials = Register(StateRef->EdgeMaterialBuffer, TEXT("SoftBody.Xpbd.EdgeMaterials"));
		FRDGBufferRef VertexEdgeOffsets = Register(StateRef->VertexEdgeOffsetBuffer, TEXT("SoftBody.Xpbd.VertexEdgeOffsets"));
		FRDGBufferRef VertexEdgeIndices = Register(StateRef->VertexEdgeIndexBuffer, TEXT("SoftBody.Xpbd.VertexEdgeIndices"));
		FRDGBufferRef VertexEdgeCorners = Register(StateRef->VertexEdgeCornerBuffer, TEXT("SoftBody.Xpbd.VertexEdgeCorners"));
		FRDGBufferRef TetVertices = Register(StateRef->TetVertexBuffer, TEXT("SoftBody.Xpbd.TetVertices"));
		FRDGBufferRef TetRestVolumes = Register(StateRef->TetRestVolumeBuffer, TEXT("SoftBody.Xpbd.TetRestVolumes"));
		FRDGBufferRef TetMaterials = Register(StateRef->TetMaterialBuffer, TEXT("SoftBody.Xpbd.TetMaterials"));
		FRDGBufferRef VertexTetOffsets = Register(StateRef->VertexTetOffsetBuffer, TEXT("SoftBody.Xpbd.VertexTetOffsets"));
		FRDGBufferRef VertexTetIndices = Register(StateRef->VertexTetIndexBuffer, TEXT("SoftBody.Xpbd.VertexTetIndices"));
		FRDGBufferRef VertexTetCorners = Register(StateRef->VertexTetCornerBuffer, TEXT("SoftBody.Xpbd.VertexTetCorners"));
		FRDGBufferRef EdgeLambdas = Register(StateRef->EdgeLambdaBuffer, TEXT("SoftBody.Xpbd.EdgeLambdas"));
		FRDGBufferRef TetLambdas = Register(StateRef->TetLambdaBuffer, TEXT("SoftBody.Xpbd.TetLambdas"));
		FRDGBufferRef EdgeCorrections = Register(StateRef->EdgeCorrectionBuffer, TEXT("SoftBody.Xpbd.EdgeCorrections"));
		FRDGBufferRef TetCorrections = Register(StateRef->TetCorrectionBuffer, TEXT("SoftBody.Xpbd.TetCorrections"));
		const bool bLowFrequencyEnabled = StateRef->Settings.bEnableLowFrequencyCorrection && StateRef->LowFrequencyBasisBuffer.IsValid();
		FRDGBufferRef LowFrequencyBasis = bLowFrequencyEnabled
			? Register(StateRef->LowFrequencyBasisBuffer, TEXT("SoftBody.Xpbd.LowFrequencyBasis")) : nullptr;
		FRDGBufferRef LocalCorrections = bLowFrequencyEnabled
			? GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->Input.RestPositions.Num()), TEXT("SoftBody.Xpbd.LocalCorrections")) : nullptr;
		FRDGBufferRef LowFrequencyCoefficients = bLowFrequencyEnabled
			? GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 2), TEXT("SoftBody.Xpbd.LowFrequencyCoefficients")) : nullptr;
		const int32 LowFrequencyPartialCount = FMath::DivideAndRoundUp(StateRef->Input.RestPositions.Num(), 64);
		FRDGBufferRef LowFrequencyPartialR0 = bLowFrequencyEnabled
			? GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), LowFrequencyPartialCount), TEXT("SoftBody.Xpbd.LowFrequencyPartialR0")) : nullptr;
		FRDGBufferRef LowFrequencyPartialR1 = bLowFrequencyEnabled
			? GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), LowFrequencyPartialCount), TEXT("SoftBody.Xpbd.LowFrequencyPartialR1")) : nullptr;
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EdgeLambdas), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TetLambdas), 0u);

		TShaderMapRef<FSoftBodyXpbdIntegrateCS> IntegrateShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* Integrate = GraphBuilder.AllocParameters<FSoftBodyXpbdIntegrateCS::FParameters>();
		Integrate->ParticleCount = StateRef->Input.RestPositions.Num();
		Integrate->FixedDeltaSecondsSquared = FMath::Square(StateRef->Settings.FixedDeltaSeconds);
		Integrate->VelocityDamping = StateRef->Settings.VelocityDamping;
		Integrate->GravityCmPerSecondSquared = StateRef->Settings.GravityCmPerSecondSquared;
		Integrate->CurrentPositions = GraphBuilder.CreateSRV(Current);
		Integrate->PreviousPositions = GraphBuilder.CreateSRV(Previous);
		Integrate->RestPositions = GraphBuilder.CreateSRV(RestPositions);
		Integrate->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
		Integrate->PredictedPositions = GraphBuilder.CreateUAV(WorkA);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Integrate %lld", StepToExecute),
			IntegrateShader, Integrate, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.RestPositions.Num(), 64), 1, 1));

		FRDGBufferRef SolverCurrent = WorkA;
		FRDGBufferRef SolverNext = WorkB;
		TShaderMapRef<FSoftBodyXpbdDistanceConstraintCS> DistanceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdVolumeConstraintCS> VolumeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdGatherApplyCS> GatherShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdGatherCorrectionCS> GatherCorrectionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdProjectLowFrequencyPartialCS> ProjectLowFrequencyPartialShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdFinalizeLowFrequencyCS> FinalizeLowFrequencyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdApplyLowFrequencyCS> ApplyLowFrequencyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FSoftBodyXpbdGroundContactCS> GroundContactShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		int32 LowFrequencyPassesThisStep = 0;
		for (int32 Iteration = 0; Iteration < StateRef->Settings.SolverIterations; ++Iteration)
		{
			auto* Distance = GraphBuilder.AllocParameters<FSoftBodyXpbdDistanceConstraintCS::FParameters>();
			Distance->EdgeConstraintCount = StateRef->Input.EdgeVertices.Num();
			Distance->FixedDeltaSecondsSquared = FMath::Square(StateRef->Settings.FixedDeltaSeconds);
			Distance->SoftEdgeCompliance = StateRef->Settings.SoftEdgeCompliance;
			Distance->StiffEdgeCompliance = StateRef->Settings.StiffEdgeCompliance;
			Distance->SolverInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
			Distance->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
			Distance->EdgeVertices = GraphBuilder.CreateSRV(EdgeVertices);
			Distance->EdgeRestLengthsCm = GraphBuilder.CreateSRV(EdgeRestLengths);
			Distance->EdgeMaterialIds = GraphBuilder.CreateSRV(EdgeMaterials);
			Distance->EdgeLambdas = GraphBuilder.CreateUAV(EdgeLambdas);
			Distance->EdgeCorrectionOutputs = GraphBuilder.CreateUAV(EdgeCorrections);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Distance %d", Iteration),
				DistanceShader, Distance, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.EdgeVertices.Num(), 64), 1, 1));

			auto* Volume = GraphBuilder.AllocParameters<FSoftBodyXpbdVolumeConstraintCS::FParameters>();
			Volume->TetrahedronConstraintCount = StateRef->Input.TetrahedronVertices.Num();
			Volume->FixedDeltaSecondsSquared = FMath::Square(StateRef->Settings.FixedDeltaSeconds);
			Volume->SoftVolumeCompliance = StateRef->Settings.SoftVolumeCompliance;
			Volume->StiffVolumeCompliance = StateRef->Settings.StiffVolumeCompliance;
			Volume->SolverInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
			Volume->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
			Volume->TetrahedronVertices = GraphBuilder.CreateSRV(TetVertices);
			Volume->TetrahedronRestVolumesCm3 = GraphBuilder.CreateSRV(TetRestVolumes);
			Volume->TetrahedronMaterialIds = GraphBuilder.CreateSRV(TetMaterials);
			Volume->TetrahedronLambdas = GraphBuilder.CreateUAV(TetLambdas);
			Volume->TetrahedronCorrectionOutputs = GraphBuilder.CreateUAV(TetCorrections);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Volume %d", Iteration),
				VolumeShader, Volume, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.TetrahedronVertices.Num(), 64), 1, 1));

			const bool bApplyLowFrequency = bLowFrequencyEnabled
				&& Iteration % StateRef->Settings.LowFrequencyCorrectionInterval == 0;
			if (bApplyLowFrequency)
			{
				auto* GatherCorrection = GraphBuilder.AllocParameters<FSoftBodyXpbdGatherCorrectionCS::FParameters>();
				GatherCorrection->ParticleCount = StateRef->Input.RestPositions.Num();
				GatherCorrection->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
				GatherCorrection->VertexEdgeOffsets = GraphBuilder.CreateSRV(VertexEdgeOffsets);
				GatherCorrection->VertexEdgeConstraintIndices = GraphBuilder.CreateSRV(VertexEdgeIndices);
				GatherCorrection->VertexEdgeCorners = GraphBuilder.CreateSRV(VertexEdgeCorners);
				GatherCorrection->VertexTetrahedronOffsets = GraphBuilder.CreateSRV(VertexTetOffsets);
				GatherCorrection->VertexTetrahedronConstraintIndices = GraphBuilder.CreateSRV(VertexTetIndices);
				GatherCorrection->VertexTetrahedronCorners = GraphBuilder.CreateSRV(VertexTetCorners);
				GatherCorrection->EdgeCorrectionInputs = GraphBuilder.CreateSRV(EdgeCorrections);
				GatherCorrection->TetrahedronCorrectionInputs = GraphBuilder.CreateSRV(TetCorrections);
				GatherCorrection->LocalCorrectionOutputs = GraphBuilder.CreateUAV(LocalCorrections);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Gather Correction %d", Iteration),
					GatherCorrectionShader, GatherCorrection, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.RestPositions.Num(), 64), 1, 1));

				auto* Project = GraphBuilder.AllocParameters<FSoftBodyXpbdProjectLowFrequencyPartialCS::FParameters>();
				Project->ParticleCount = StateRef->Input.RestPositions.Num();
				Project->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
				Project->LowFrequencyBasisValues = GraphBuilder.CreateSRV(LowFrequencyBasis);
				Project->LocalCorrectionInputs = GraphBuilder.CreateSRV(LocalCorrections);
				Project->LowFrequencyPartialR0Outputs = GraphBuilder.CreateUAV(LowFrequencyPartialR0);
				Project->LowFrequencyPartialR1Outputs = GraphBuilder.CreateUAV(LowFrequencyPartialR1);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Low Frequency Partial %d", Iteration),
					ProjectLowFrequencyPartialShader, Project, FIntVector(LowFrequencyPartialCount, 1, 1));

				auto* Finalize = GraphBuilder.AllocParameters<FSoftBodyXpbdFinalizeLowFrequencyCS::FParameters>();
				Finalize->LowFrequencyPartialCount = LowFrequencyPartialCount;
				Finalize->LowFrequencyInverseSystem = StateRef->LowFrequencyInverseSystem;
				Finalize->LowFrequencyPartialR0Inputs = GraphBuilder.CreateSRV(LowFrequencyPartialR0);
				Finalize->LowFrequencyPartialR1Inputs = GraphBuilder.CreateSRV(LowFrequencyPartialR1);
				Finalize->LowFrequencyCoefficientOutputs = GraphBuilder.CreateUAV(LowFrequencyCoefficients);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Low Frequency Finalize %d", Iteration),
					FinalizeLowFrequencyShader, Finalize, FIntVector(1, 1, 1));

				auto* Apply = GraphBuilder.AllocParameters<FSoftBodyXpbdApplyLowFrequencyCS::FParameters>();
				Apply->ParticleCount = StateRef->Input.RestPositions.Num();
				Apply->JacobiRelaxation = StateRef->Settings.JacobiRelaxation;
				Apply->MaximumVertexCorrectionCm = StateRef->Settings.MaximumVertexCorrectionCm;
				Apply->LowFrequencyCorrectionStrength = StateRef->Settings.LowFrequencyCorrectionStrength;
				Apply->RestPositions = GraphBuilder.CreateSRV(RestPositions);
				Apply->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
				Apply->SolverInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
				Apply->LowFrequencyBasisValues = GraphBuilder.CreateSRV(LowFrequencyBasis);
				Apply->LocalCorrectionInputs = GraphBuilder.CreateSRV(LocalCorrections);
				Apply->LowFrequencyCoefficientInputs = GraphBuilder.CreateSRV(LowFrequencyCoefficients);
				Apply->SolverOutputPositions = GraphBuilder.CreateUAV(SolverNext);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Low Frequency Apply %d", Iteration),
					ApplyLowFrequencyShader, Apply, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.RestPositions.Num(), 64), 1, 1));
				++LowFrequencyPassesThisStep;
			}
			else
			{
				auto* Gather = GraphBuilder.AllocParameters<FSoftBodyXpbdGatherApplyCS::FParameters>();
				Gather->ParticleCount = StateRef->Input.RestPositions.Num();
				Gather->JacobiRelaxation = StateRef->Settings.JacobiRelaxation;
				Gather->MaximumVertexCorrectionCm = StateRef->Settings.MaximumVertexCorrectionCm;
				Gather->RestPositions = GraphBuilder.CreateSRV(RestPositions);
				Gather->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
				Gather->VertexEdgeOffsets = GraphBuilder.CreateSRV(VertexEdgeOffsets);
				Gather->VertexEdgeConstraintIndices = GraphBuilder.CreateSRV(VertexEdgeIndices);
				Gather->VertexEdgeCorners = GraphBuilder.CreateSRV(VertexEdgeCorners);
				Gather->VertexTetrahedronOffsets = GraphBuilder.CreateSRV(VertexTetOffsets);
				Gather->VertexTetrahedronConstraintIndices = GraphBuilder.CreateSRV(VertexTetIndices);
				Gather->VertexTetrahedronCorners = GraphBuilder.CreateSRV(VertexTetCorners);
				Gather->SolverInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
				Gather->EdgeCorrectionInputs = GraphBuilder.CreateSRV(EdgeCorrections);
				Gather->TetrahedronCorrectionInputs = GraphBuilder.CreateSRV(TetCorrections);
				Gather->SolverOutputPositions = GraphBuilder.CreateUAV(SolverNext);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Gather %d", Iteration),
					GatherShader, Gather, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.RestPositions.Num(), 64), 1, 1));
			}
			Swap(SolverCurrent, SolverNext);
		}
		int32 GroundContactPassesThisStep = 0;
		if (StateRef->Settings.bEnableGroundContact)
		{
			auto* Contact = GraphBuilder.AllocParameters<FSoftBodyXpbdGroundContactCS::FParameters>();
			Contact->ParticleCount = StateRef->Input.RestPositions.Num();
			Contact->GroundPlaneZCm = StateRef->Settings.GroundPlaneZCm;
			Contact->GroundContactStiffness = StateRef->Settings.GroundContactStiffness;
			Contact->RestPositions = GraphBuilder.CreateSRV(RestPositions);
			Contact->InverseMasses = GraphBuilder.CreateSRV(InverseMasses);
			Contact->ContactInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
			Contact->ContactOutputPositions = GraphBuilder.CreateUAV(SolverNext);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SoftBody XPBD Ground Contact"),
				GroundContactShader, Contact, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.RestPositions.Num(), 64), 1, 1));
			Swap(SolverCurrent, SolverNext);
			GroundContactPassesThisStep = 1;
		}

		const bool bResultInWorkA = SolverCurrent == WorkA;
		if (!StateRef->PendingReadback && StepToExecute % StateRef->Settings.ReadbackIntervalSteps == 0)
		{
			StateRef->PendingReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("SoftBody.Xpbd.AsyncReadback"));
			StateRef->PendingReadbackStep = StepToExecute;
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadback.Get(), SolverCurrent,
				StateRef->RestPositionUpload.Num() * sizeof(FVector4f));
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.bReadbackPending = true;
		}
		GraphBuilder.Execute();
		if (GpuTimingSlot)
		{
			RHICmdList.EndRenderQuery(GpuTimingSlot->EndQuery.GetReference());
			RHICmdList.SubmitCommandsHint();
			GpuTimingSlot->Step = StepToExecute;
			GpuTimingSlot->bPending = true;
		}

		TRefCountPtr<FRDGPooledBuffer> OldPrevious = StateRef->PreviousBuffer;
		TRefCountPtr<FRDGPooledBuffer> OldCurrent = StateRef->CurrentBuffer;
		TRefCountPtr<FRDGPooledBuffer> Result = bResultInWorkA ? StateRef->WorkBufferA : StateRef->WorkBufferB;
		StateRef->PreviousBuffer = OldCurrent;
		StateRef->CurrentBuffer = Result;
		if (bResultInWorkA) StateRef->WorkBufferA = OldPrevious; else StateRef->WorkBufferB = OldPrevious;
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->Stats.StepsExecuted = StepToExecute;
		StateRef->Stats.DistancePassesExecuted += StateRef->Settings.SolverIterations;
		StateRef->Stats.VolumePassesExecuted += StateRef->Settings.SolverIterations;
		StateRef->Stats.GatherPassesExecuted += StateRef->Settings.SolverIterations;
		StateRef->Stats.LowFrequencyProjectionPassesExecuted += LowFrequencyPassesThisStep;
		StateRef->Stats.LowFrequencyApplyPassesExecuted += LowFrequencyPassesThisStep;
		StateRef->Stats.LowFrequencyReductionDispatchesExecuted += LowFrequencyPassesThisStep * 2;
		StateRef->Stats.LowFrequencyModeCount = bLowFrequencyEnabled ? 2 : 0;
		StateRef->Stats.GroundContactPassesExecuted += GroundContactPassesThisStep;
	});
	return true;
}

void FSoftBodyGpuXpbdSimulation::RequestReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdRequestReadback)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown || !StateRef->CurrentBuffer) return;
		SoftBodyGpuXpbd::ConsumeReadyReadback_RenderThread(StateRef);
		SoftBodyGpuXpbd::ConsumeReadyGpuTimings_RenderThread(StateRef);
		if (StateRef->PendingReadback) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGBufferRef Current = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentBuffer, TEXT("SoftBody.Xpbd.ReadbackCurrent"));
		StateRef->PendingReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("SoftBody.Xpbd.RequestedReadback"));
		{
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->PendingReadbackStep = StateRef->Stats.StepsExecuted;
			StateRef->Stats.bReadbackPending = true;
		}
		AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadback.Get(), Current,
			StateRef->RestPositionUpload.Num() * sizeof(FVector4f));
		GraphBuilder.Execute();
	});
}

void FSoftBodyGpuXpbdSimulation::PollReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdPollReadback)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (!StateRef->bShutdown)
		{
			SoftBodyGpuXpbd::ConsumeReadyReadback_RenderThread(StateRef);
			SoftBodyGpuXpbd::ConsumeReadyGpuTimings_RenderThread(StateRef);
		}
	});
}

bool FSoftBodyGpuXpbdSimulation::ConsumeLatestPositions(TArray<FVector3f>& OutPositions, int64& OutStepIndex)
{
	if (!State.IsValid()) return false;
	FScopeLock Lock(&State->DataLock);
	if (!State->bLatestPositionsAvailable) return false;
	OutPositions = State->LatestPositions;
	OutStepIndex = State->Stats.LatestReadbackStep;
	State->bLatestPositionsAvailable = false;
	return true;
}

FSoftBodyGpuXpbdStats FSoftBodyGpuXpbdSimulation::GetStats() const
{
	if (!State.IsValid()) return FSoftBodyGpuXpbdStats();
	FScopeLock Lock(&State->DataLock);
	return State->Stats;
}

void FSoftBodyGpuXpbdSimulation::GetGpuTimingSamples(
	TArray<int64>& OutSteps,
	TArray<double>& OutMilliseconds) const
{
	OutSteps.Reset();
	OutMilliseconds.Reset();
	if (!State.IsValid()) return;
	FScopeLock Lock(&State->DataLock);
	OutSteps = State->GpuTimingSteps;
	OutMilliseconds = State->GpuStepMilliseconds;
}

void FSoftBodyGpuXpbdSimulation::FlushAndWaitForReadbackForValidation()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdValidationIdle)([](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	RequestReadback();
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdValidationReadbackIdle)([](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	PollReadback();
	FlushRenderingCommands();
}

void FSoftBodyGpuXpbdSimulation::Shutdown()
{
	if (!State.IsValid()) return;
	const TSharedRef<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	{
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->bShutdown = true;
	}
	ENQUEUE_RENDER_COMMAND(SoftBodyGpuXpbdShutdown)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		StateRef->PendingReadback.Reset();
		StateRef->GpuTimingSlots.Reset(); StateRef->GpuTimingSteps.Reset(); StateRef->GpuStepMilliseconds.Reset();
		StateRef->CurrentBuffer.SafeRelease(); StateRef->PreviousBuffer.SafeRelease();
		StateRef->WorkBufferA.SafeRelease(); StateRef->WorkBufferB.SafeRelease();
		StateRef->RestPositionBuffer.SafeRelease(); StateRef->InverseMassBuffer.SafeRelease();
		StateRef->EdgeVertexBuffer.SafeRelease(); StateRef->EdgeRestLengthBuffer.SafeRelease(); StateRef->EdgeMaterialBuffer.SafeRelease();
		StateRef->VertexEdgeOffsetBuffer.SafeRelease(); StateRef->VertexEdgeIndexBuffer.SafeRelease(); StateRef->VertexEdgeCornerBuffer.SafeRelease();
		StateRef->TetVertexBuffer.SafeRelease(); StateRef->TetRestVolumeBuffer.SafeRelease(); StateRef->TetMaterialBuffer.SafeRelease();
		StateRef->VertexTetOffsetBuffer.SafeRelease(); StateRef->VertexTetIndexBuffer.SafeRelease(); StateRef->VertexTetCornerBuffer.SafeRelease();
		StateRef->EdgeLambdaBuffer.SafeRelease(); StateRef->TetLambdaBuffer.SafeRelease();
		StateRef->EdgeCorrectionBuffer.SafeRelease(); StateRef->TetCorrectionBuffer.SafeRelease();
		StateRef->LowFrequencyBasisBuffer.SafeRelease();
	});
	State.Reset();
}
