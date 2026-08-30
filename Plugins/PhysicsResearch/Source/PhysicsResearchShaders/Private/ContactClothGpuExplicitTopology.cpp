#include "ContactClothGpuExplicitTopology.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FContactClothExplicitTopologyJacobiCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyJacobiCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyJacobiCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(uint32, CollisionSphereCount)
		SHADER_PARAMETER(uint32, CollisionCapsuleCount)
		SHADER_PARAMETER(uint32, CollisionPrimitiveCapacity)
		SHADER_PARAMETER(uint32, NdbEnabled)
		SHADER_PARAMETER(float, Relaxation)
		SHADER_PARAMETER(float, NdbBaseWeight)
		SHADER_PARAMETER(float, NdbActivationMultiplier)
		SHADER_PARAMETER(float, NdbMaximumWeight)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, AdjacencyOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NeighborIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, NeighborRestLengthsCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CollisionActivationCounts)
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
	FContactClothExplicitTopologyJacobiCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopology.usf",
	"MainCS",
	SF_Compute);

class FContactClothExplicitTopologyIntegrateCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyIntegrateCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyIntegrateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, FixedDeltaSecondsSquared)
		SHADER_PARAMETER(float, VelocityDamping)
		SHADER_PARAMETER(FVector3f, GravityCmPerSecondSquared)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CurrentPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PreviousPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, KinematicTargets)
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
	FContactClothExplicitTopologyIntegrateCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopology.usf",
	"IntegrateCS",
	SF_Compute);

class FContactClothExplicitTopologyCompleteCcdCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyCompleteCcdCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyCompleteCcdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitCcdParticleCount)
		SHADER_PARAMETER(uint32, ExplicitCcdSphereCount)
		SHADER_PARAMETER(uint32, ExplicitCcdCapsuleCount)
		SHADER_PARAMETER(float, ExplicitCcdSafetyOffsetCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitCcdInverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitCcdStartPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitCcdCandidatePositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitCcdCollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitCcdCollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitCcdCollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ExplicitCcdOutputPositions)
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
	FContactClothExplicitTopologyCompleteCcdCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyCcd.usf",
	"MainCS",
	SF_Compute);

class FContactClothExplicitTopologyNdbStateCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyNdbStateCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyNdbStateCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitNdbParticleCount)
		SHADER_PARAMETER(uint32, ExplicitNdbSphereCount)
		SHADER_PARAMETER(uint32, ExplicitNdbCapsuleCount)
		SHADER_PARAMETER(uint32, ExplicitNdbPrimitiveCapacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitNdbInverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitNdbPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitNdbCollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitNdbCollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitNdbCollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ExplicitNdbActivationCounts)
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
	FContactClothExplicitTopologyNdbStateCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyNdb.usf",
	"MainCS",
	SF_Compute);

class FContactClothExplicitTopologyModalProjectCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyModalProjectCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyModalProjectCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitModalParticleCount)
		SHADER_PARAMETER(uint32, ExplicitModalModeCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitModalInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitModalTargetPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitModalInverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitModalBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ExplicitModalOutputValues)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothExplicitTopologyModalProjectCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyModal.usf", "ProjectRhsCS", SF_Compute);

class FContactClothExplicitTopologyModalApplyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyModalApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyModalApplyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitModalParticleCount)
		SHADER_PARAMETER(uint32, ExplicitModalModeCount)
		SHADER_PARAMETER(float, ExplicitModalCorrectionStrength)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitModalInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitModalInverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitModalBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitModalInverseSystemValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitModalRhsValues)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ExplicitModalOutputValues)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothExplicitTopologyModalApplyCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyModal.usf", "SolveAndApplyCS", SF_Compute);

class FContactClothExplicitTopologyRankOneAssemblyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyRankOneAssemblyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyRankOneAssemblyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitRankOneParticleCount)
		SHADER_PARAMETER(uint32, ExplicitRankOneModeCount)
		SHADER_PARAMETER(uint32, ExplicitRankOneSphereCount)
		SHADER_PARAMETER(uint32, ExplicitRankOneCapsuleCount)
		SHADER_PARAMETER(uint32, ExplicitRankOnePrimitiveCapacity)
		SHADER_PARAMETER(float, ExplicitRankOneBaseWeight)
		SHADER_PARAMETER(float, ExplicitRankOneActivationMultiplier)
		SHADER_PARAMETER(float, ExplicitRankOneMaximumWeight)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitRankOnePositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneInverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitRankOneCollisionSpheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitRankOneCollisionCapsuleStarts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitRankOneCollisionCapsuleEnds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ExplicitRankOneActivationCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneBaseSystem)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneParticleOuterProducts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ExplicitRankOneOutputMatrix)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ExplicitRankOneOutputRhs)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothExplicitTopologyRankOneAssemblyCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyRankOne.usf", "AssembleContactsCS", SF_Compute);

class FContactClothExplicitTopologyRankOneSolveCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyRankOneSolveCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyRankOneSolveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitRankOneSolveModeCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneSolveMatrix)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneSolveRhs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ExplicitRankOneSolution)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FContactClothExplicitTopologyRankOneSolveCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyRankOne.usf", "SolveContactsCS", SF_Compute);

class FContactClothExplicitTopologyRankOneApplyCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FContactClothExplicitTopologyRankOneApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FContactClothExplicitTopologyRankOneApplyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ExplicitRankOneApplyParticleCount)
		SHADER_PARAMETER(uint32, ExplicitRankOneApplyModeCount)
		SHADER_PARAMETER(float, ExplicitRankOneMaximumCorrectionCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ExplicitRankOneApplyInputPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneApplyInverseMasses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneApplyBasisValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ExplicitRankOneApplySolution)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ExplicitRankOneApplyOutputPositions)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};
IMPLEMENT_GLOBAL_SHADER(FContactClothExplicitTopologyRankOneApplyCS,
	"/Plugin/PhysicsResearch/Private/ContactClothExplicitTopologyRankOne.usf", "ApplyContactsCS", SF_Compute);

namespace ContactClothGpuExplicitTopology
{
struct FBlockingState
{
	FContactClothGpuExplicitTopologySettings Settings;
	TArray<FVector4f> Input;
	TArray<float> InverseMasses;
	TArray<uint32> AdjacencyOffsets;
	TArray<uint32> NeighborIndices;
	TArray<float> NeighborRestLengthsCm;
	TArray<FVector4f> Output;
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	FString FailureReason;
};

template<typename ElementType>
FRDGBufferRef CreateUploadedBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, const TArray<ElementType>& Values)
{
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(ElementType), Values.Num()),
		Name);
	GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(ElementType));
	return Buffer;
}
}

FContactClothGpuExplicitTopologyResult FContactClothGpuExplicitTopology::RunBlocking(
	TConstArrayView<FVector3f> InputPositions,
	TConstArrayView<float> InverseMasses,
	TConstArrayView<FIntPoint> StructuralEdges,
	TConstArrayView<float> StructuralRestLengthsCm,
	const FContactClothGpuExplicitTopologySettings& InSettings)
{
	FContactClothGpuExplicitTopologyResult Result;
	const double StartSeconds = FPlatformTime::Seconds();
	Result.ParticleCount = InputPositions.Num();
	Result.EdgeCount = StructuralEdges.Num();
	if (!IsInGameThread())
	{
		Result.FailureReason = TEXT("RunBlocking must be called on the game thread");
		return Result;
	}
	if (!GDynamicRHI)
	{
		Result.FailureReason = TEXT("No dynamic RHI is available");
		return Result;
	}
	if (InputPositions.IsEmpty() || InverseMasses.Num() != InputPositions.Num() || StructuralEdges.IsEmpty()
		|| StructuralRestLengthsCm.Num() != StructuralEdges.Num() || InSettings.Iterations < 1
		|| !FMath::IsFinite(InSettings.Relaxation) || InSettings.Relaxation <= 0.0f || InSettings.Relaxation > 1.0f)
	{
		Result.FailureReason = TEXT("Explicit topology arrays or solver settings are invalid");
		return Result;
	}

	TArray<uint32> Degrees;
	Degrees.Init(0, InputPositions.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < StructuralEdges.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = StructuralEdges[EdgeIndex];
		const float RestLength = StructuralRestLengthsCm[EdgeIndex];
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= InputPositions.Num() || Edge.Y >= InputPositions.Num()
			|| Edge.X == Edge.Y || !FMath::IsFinite(RestLength) || RestLength <= 0.0f)
		{
			Result.FailureReason = TEXT("Explicit topology contains an invalid edge");
			return Result;
		}
		Degrees[Edge.X]++;
		Degrees[Edge.Y]++;
	}
	for (int32 ParticleIndex = 0; ParticleIndex < InputPositions.Num(); ++ParticleIndex)
	{
		const FVector3f Position = InputPositions[ParticleIndex];
		if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z)
			|| !FMath::IsFinite(InverseMasses[ParticleIndex]) || InverseMasses[ParticleIndex] < 0.0f)
		{
			Result.FailureReason = TEXT("Explicit topology contains non-finite positions or inverse masses");
			return Result;
		}
	}

	TSharedRef<ContactClothGpuExplicitTopology::FBlockingState, ESPMode::ThreadSafe> State =
		MakeShared<ContactClothGpuExplicitTopology::FBlockingState, ESPMode::ThreadSafe>();
	State->Settings = InSettings;
	State->Input.Reserve(InputPositions.Num());
	for (const FVector3f Position : InputPositions) State->Input.Emplace(Position, 0.0f);
	State->InverseMasses.Append(InverseMasses);
	State->AdjacencyOffsets.SetNumUninitialized(InputPositions.Num() + 1);
	State->AdjacencyOffsets[0] = 0;
	for (int32 ParticleIndex = 0; ParticleIndex < InputPositions.Num(); ++ParticleIndex)
	{
		State->AdjacencyOffsets[ParticleIndex + 1] = State->AdjacencyOffsets[ParticleIndex] + Degrees[ParticleIndex];
	}
	State->NeighborIndices.SetNumUninitialized(StructuralEdges.Num() * 2);
	State->NeighborRestLengthsCm.SetNumUninitialized(StructuralEdges.Num() * 2);
	TArray<uint32> WriteOffsets = State->AdjacencyOffsets;
	for (int32 EdgeIndex = 0; EdgeIndex < StructuralEdges.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = StructuralEdges[EdgeIndex];
		const float RestLength = StructuralRestLengthsCm[EdgeIndex];
		const uint32 AOffset = WriteOffsets[Edge.X]++;
		const uint32 BOffset = WriteOffsets[Edge.Y]++;
		State->NeighborIndices[AOffset] = Edge.Y;
		State->NeighborIndices[BOffset] = Edge.X;
		State->NeighborRestLengthsCm[AOffset] = RestLength;
		State->NeighborRestLengthsCm[BOffset] = RestLength;
	}

	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyBlockingDispatch)(
		[State](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef InverseMassBuffer = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.Explicit.InverseMasses"), State->InverseMasses);
			FRDGBufferRef OffsetBuffer = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.Explicit.AdjacencyOffsets"), State->AdjacencyOffsets);
			FRDGBufferRef NeighborBuffer = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.Explicit.NeighborIndices"), State->NeighborIndices);
			FRDGBufferRef RestLengthBuffer = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.Explicit.NeighborRestLengths"), State->NeighborRestLengthsCm);
			const TArray<uint32> ZeroActivation = {0};
			FRDGBufferRef CollisionActivations = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.Explicit.CollisionActivations.Disabled"), ZeroActivation);
			FRDGBufferDesc PositionDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), State->Input.Num());
			PositionDesc.Usage = static_cast<EBufferUsageFlags>(PositionDesc.Usage | BUF_SourceCopy);
			FRDGBufferRef BufferA = GraphBuilder.CreateBuffer(PositionDesc, TEXT("ContactCloth.Explicit.PositionsA"));
			FRDGBufferRef BufferB = GraphBuilder.CreateBuffer(PositionDesc, TEXT("ContactCloth.Explicit.PositionsB"));
			GraphBuilder.QueueBufferUpload(BufferA, State->Input.GetData(), State->Input.Num() * sizeof(FVector4f));

			TShaderMapRef<FContactClothExplicitTopologyJacobiCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRDGBufferRef Current = BufferA;
			FRDGBufferRef Next = BufferB;
			for (int32 Iteration = 0; Iteration < State->Settings.Iterations; ++Iteration)
			{
				FContactClothExplicitTopologyJacobiCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FContactClothExplicitTopologyJacobiCS::FParameters>();
				Parameters->ParticleCount = State->Input.Num();
				Parameters->CollisionSphereCount = 0;
				Parameters->CollisionCapsuleCount = 0;
				Parameters->CollisionPrimitiveCapacity = 1;
				Parameters->NdbEnabled = 0;
				Parameters->Relaxation = State->Settings.Relaxation;
				Parameters->NdbBaseWeight = 1.0f;
				Parameters->NdbActivationMultiplier = 1.0f;
				Parameters->NdbMaximumWeight = 1.0f;
				Parameters->InverseMasses = GraphBuilder.CreateSRV(InverseMassBuffer);
				Parameters->AdjacencyOffsets = GraphBuilder.CreateSRV(OffsetBuffer);
				Parameters->NeighborIndices = GraphBuilder.CreateSRV(NeighborBuffer);
				Parameters->NeighborRestLengthsCm = GraphBuilder.CreateSRV(RestLengthBuffer);
				Parameters->InputPositions = GraphBuilder.CreateSRV(Current);
				Parameters->CollisionSpheres = GraphBuilder.CreateSRV(BufferA);
				Parameters->CollisionCapsuleStarts = GraphBuilder.CreateSRV(BufferA);
				Parameters->CollisionCapsuleEnds = GraphBuilder.CreateSRV(BufferA);
				Parameters->CollisionActivationCounts = GraphBuilder.CreateSRV(CollisionActivations);
				Parameters->OutputPositions = GraphBuilder.CreateUAV(Next);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Explicit Topology Jacobi %d", Iteration),
					ComputeShader,
					Parameters,
					FIntVector(FMath::DivideAndRoundUp(State->Input.Num(), 64), 1, 1));
				Swap(Current, Next);
			}

			FRHIGPUBufferReadback Readback(TEXT("ContactCloth.Explicit.Readback"));
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
	for (const FVector4f Position : State->Output) Result.Positions.Emplace(Position.X, Position.Y, Position.Z);
	Result.InputMaximumEdgeErrorCm = MeasureMaximumEdgeErrorCm(InputPositions, StructuralEdges, StructuralRestLengthsCm);
	Result.OutputMaximumEdgeErrorCm = MeasureMaximumEdgeErrorCm(Result.Positions, StructuralEdges, StructuralRestLengthsCm);
	for (int32 ParticleIndex = 0; ParticleIndex < Result.Positions.Num(); ++ParticleIndex)
	{
		if (InverseMasses[ParticleIndex] <= 0.0f)
		{
			Result.MaximumFixedParticleDriftCm = FMath::Max(
				Result.MaximumFixedParticleDriftCm,
				static_cast<double>((Result.Positions[ParticleIndex] - InputPositions[ParticleIndex]).Length()));
		}
	}
	Result.BlockingWallMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}

double FContactClothGpuExplicitTopology::MeasureMaximumEdgeErrorCm(
	TConstArrayView<FVector3f> Positions,
	TConstArrayView<FIntPoint> StructuralEdges,
	TConstArrayView<float> StructuralRestLengthsCm)
{
	if (Positions.IsEmpty() || StructuralEdges.Num() != StructuralRestLengthsCm.Num())
	{
		return TNumericLimits<double>::Max();
	}
	double MaximumError = 0.0;
	for (int32 EdgeIndex = 0; EdgeIndex < StructuralEdges.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = StructuralEdges[EdgeIndex];
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= Positions.Num() || Edge.Y >= Positions.Num())
		{
			return TNumericLimits<double>::Max();
		}
		MaximumError = FMath::Max(MaximumError, FMath::Abs(
			static_cast<double>((Positions[Edge.X] - Positions[Edge.Y]).Length()) - StructuralRestLengthsCm[EdgeIndex]));
	}
	return MaximumError;
}

double FContactClothGpuExplicitTopology::MeasureMaximumCollisionPenetrationCm(
	TConstArrayView<FVector3f> Positions,
	TConstArrayView<float> InverseMasses,
	TConstArrayView<FContactClothGpuSphere> Spheres,
	TConstArrayView<FContactClothGpuCapsule> Capsules,
	float CollisionThicknessCm)
{
	if (Positions.Num() != InverseMasses.Num() || !FMath::IsFinite(CollisionThicknessCm))
	{
		return TNumericLimits<double>::Max();
	}
	const float Thickness = FMath::Max(0.0f, CollisionThicknessCm);
	double MaximumPenetration = 0.0;
	for (int32 ParticleIndex = 0; ParticleIndex < Positions.Num(); ++ParticleIndex)
	{
		if (InverseMasses[ParticleIndex] <= 0.0f)
		{
			continue;
		}
		const FVector3f Position = Positions[ParticleIndex];
		for (const FContactClothGpuSphere& Sphere : Spheres)
		{
			MaximumPenetration = FMath::Max(MaximumPenetration,
				static_cast<double>(Sphere.RadiusCm + Thickness - (Position - Sphere.Center).Length()));
		}
		for (const FContactClothGpuCapsule& Capsule : Capsules)
		{
			const FVector3f Segment = Capsule.End - Capsule.Start;
			const float SegmentLengthSquared = Segment.SquaredLength();
			const float Alpha = SegmentLengthSquared > UE_SMALL_NUMBER
				? FMath::Clamp(FVector3f::DotProduct(Position - Capsule.Start, Segment) / SegmentLengthSquared, 0.0f, 1.0f)
				: 0.0f;
			const FVector3f ClosestPoint = Capsule.Start + Alpha * Segment;
			MaximumPenetration = FMath::Max(MaximumPenetration,
				static_cast<double>(Capsule.RadiusCm + Thickness - (Position - ClosestPoint).Length()));
		}
	}
	return FMath::Max(MaximumPenetration, 0.0);
}

class FContactClothGpuExplicitTopologyRealtimeState : public TSharedFromThis<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe>
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
	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	TArray<FVector4f> InitialPositions;
	TArray<float> InverseMasses;
	TArray<uint32> AdjacencyOffsets;
	TArray<uint32> NeighborIndices;
	TArray<float> NeighborRestLengthsCm;
	TArray<FVector4f> KinematicTargets;
	int64 KinematicTargetVersion = 0;
	TArray<FVector4f> CollisionSpheres;
	TArray<FVector4f> CollisionCapsuleStarts;
	TArray<FVector4f> CollisionCapsuleEnds;
	uint32 CollisionSphereCount = 0;
	uint32 CollisionCapsuleCount = 0;
	int64 CollisionPrimitiveVersion = 0;
	TArray<FVector3f> LatestPositions;
	TRefCountPtr<FRDGPooledBuffer> CurrentBuffer;
	TRefCountPtr<FRDGPooledBuffer> PreviousBuffer;
	TRefCountPtr<FRDGPooledBuffer> WorkBufferA;
	TRefCountPtr<FRDGPooledBuffer> WorkBufferB;
	TRefCountPtr<FRDGPooledBuffer> InverseMassBuffer;
	TRefCountPtr<FRDGPooledBuffer> AdjacencyOffsetBuffer;
	TRefCountPtr<FRDGPooledBuffer> NeighborIndexBuffer;
	TRefCountPtr<FRDGPooledBuffer> NeighborRestLengthBuffer;
	TRefCountPtr<FRDGPooledBuffer> CollisionActivationBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalBasisBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalInverseSystemBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalSystemBuffer;
	TRefCountPtr<FRDGPooledBuffer> ModalParticleOuterProductBuffer;
	TRefCountPtr<FRDGPooledBuffer> RankOneMatrixBuffer;
	TRefCountPtr<FRDGPooledBuffer> RankOneRhsBuffer;
	TRefCountPtr<FRDGPooledBuffer> RankOneSolutionBuffer;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadback;
	TUniquePtr<FRHIGPUBufferReadback> PendingActivationReadback;
	TArray<FGpuTimingSlot> GpuTimingSlots;
	TArray<int64> GpuTimingSteps;
	TArray<double> GpuStepMilliseconds;
	int64 PendingReadbackStep = 0;
	FContactClothGpuExplicitTopologyRealtimeStats Stats;
	bool bLatestPositionsAvailable = false;
	bool bShutdown = false;
};

namespace ContactClothGpuExplicitTopology
{
void ConsumeReadyGpuTimings_RenderThread(
	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe>& State)
{
	if (!State->Stats.bGpuTimingSupported) return;
	for (FContactClothGpuExplicitTopologyRealtimeState::FGpuTimingSlot& Slot : State->GpuTimingSlots)
	{
		if (!Slot.bPending) continue;
		uint64 StartMicroseconds = 0;
		uint64 EndMicroseconds = 0;
		if (!RHIGetRenderQueryResult(Slot.StartQuery.GetReference(), StartMicroseconds, false)
			|| !RHIGetRenderQueryResult(Slot.EndQuery.GetReference(), EndMicroseconds, false))
		{
			continue;
		}
		FScopeLock Lock(&State->DataLock);
		if (EndMicroseconds > StartMicroseconds)
		{
			const double Milliseconds = static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0;
			State->GpuTimingSteps.Add(Slot.Step);
			State->GpuStepMilliseconds.Add(Milliseconds);
			State->Stats.GpuTimingSamplesCompleted++;
			State->Stats.LatestGpuTimingStep = Slot.Step;
			State->Stats.LatestGpuStepMilliseconds = Milliseconds;
			State->Stats.MaximumGpuStepMilliseconds = FMath::Max(State->Stats.MaximumGpuStepMilliseconds, Milliseconds);
			double Total = 0.0;
			for (double Sample : State->GpuStepMilliseconds) Total += Sample;
			State->Stats.MeanGpuStepMilliseconds = Total / State->GpuStepMilliseconds.Num();
			TArray<double> SortedSamples = State->GpuStepMilliseconds;
			SortedSamples.Sort();
			const int32 P95Index = FMath::Clamp(FMath::CeilToInt(0.95 * SortedSamples.Num()) - 1, 0, SortedSamples.Num() - 1);
			const int32 P99Index = FMath::Clamp(FMath::CeilToInt(0.99 * SortedSamples.Num()) - 1, 0, SortedSamples.Num() - 1);
			State->Stats.P95GpuStepMilliseconds = SortedSamples[P95Index];
			State->Stats.P99GpuStepMilliseconds = SortedSamples[P99Index];
			TArray<double> SteadySamples;
			for (int32 Index = 0; Index < State->GpuStepMilliseconds.Num(); ++Index)
			{
				if (State->GpuTimingSteps[Index] > State->Settings.GpuTimingWarmupSteps)
				{
					SteadySamples.Add(State->GpuStepMilliseconds[Index]);
				}
			}
			State->Stats.SteadyStateGpuTimingSamples = SteadySamples.Num();
			if (!SteadySamples.IsEmpty())
			{
				double SteadyTotal = 0.0;
				for (double Sample : SteadySamples) SteadyTotal += Sample;
				State->Stats.MeanSteadyStateGpuStepMilliseconds = SteadyTotal / SteadySamples.Num();
				SteadySamples.Sort();
				const int32 SteadyP95Index = FMath::Clamp(FMath::CeilToInt(0.95 * SteadySamples.Num()) - 1, 0, SteadySamples.Num() - 1);
				const int32 SteadyP99Index = FMath::Clamp(FMath::CeilToInt(0.99 * SteadySamples.Num()) - 1, 0, SteadySamples.Num() - 1);
				State->Stats.P95SteadyStateGpuStepMilliseconds = SteadySamples[SteadyP95Index];
				State->Stats.P99SteadyStateGpuStepMilliseconds = SteadySamples[SteadyP99Index];
				State->Stats.MaximumSteadyStateGpuStepMilliseconds = SteadySamples.Last();
			}
		}
		else
		{
			State->Stats.GpuTimingSamplesSkipped++;
		}
		Slot.Step = 0;
		Slot.bPending = false;
	}
}

void ConsumeReadyRealtimeReadback_RenderThread(
	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe>& State)
{
	const bool bActivationRequired = State->Settings.bEnableNonDistanceBarrier;
	if (!State->PendingReadback || !State->PendingReadback->IsReady()
		|| (bActivationRequired && (!State->PendingActivationReadback || !State->PendingActivationReadback->IsReady())))
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
		for (int32 ParticleIndex = 0; ParticleIndex < State->LatestPositions.Num(); ++ParticleIndex)
		{
			State->LatestPositions[ParticleIndex] = FVector3f(
				Positions[ParticleIndex].X,
				Positions[ParticleIndex].Y,
				Positions[ParticleIndex].Z);
		}
		State->Stats.ReadbacksCompleted++;
		State->Stats.LatestReadbackStep = State->PendingReadbackStep;
		State->Stats.bReadbackPending = false;
		State->bLatestPositionsAvailable = true;
		if (bActivationRequired)
		{
			const uint32 ActivationCount = State->InitialPositions.Num() * State->Settings.CollisionPrimitiveCapacity;
			const uint32 ActivationBytes = ActivationCount * sizeof(uint32);
			const void* ActivationData = State->PendingActivationReadback->Lock(ActivationBytes);
			if (ActivationData)
			{
				const uint32* Values = static_cast<const uint32*>(ActivationData);
				int32 ActivePairs = 0;
				int32 MaximumActivation = 0;
				for (uint32 Index = 0; Index < ActivationCount; ++Index)
				{
					if (Values[Index] > 0)
					{
						ActivePairs++;
						MaximumActivation = FMath::Max(MaximumActivation, static_cast<int32>(Values[Index]));
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
	State->PendingActivationReadback.Reset();
	State->PendingReadbackStep = 0;
}
}

FContactClothGpuExplicitTopologySimulation::FContactClothGpuExplicitTopologySimulation() = default;

FContactClothGpuExplicitTopologySimulation::~FContactClothGpuExplicitTopologySimulation()
{
	Shutdown();
}

bool FContactClothGpuExplicitTopologySimulation::Initialize(
	TConstArrayView<FVector3f> InitialPositions,
	TConstArrayView<float> InverseMasses,
	TConstArrayView<FIntPoint> StructuralEdges,
	TConstArrayView<float> StructuralRestLengthsCm,
	const FContactClothGpuExplicitTopologyRealtimeSettings& InSettings)
{
	const bool bModalRequested = InSettings.ModalModeCount > 0 || InSettings.ModalCorrectionStrength > 0.0f
		|| !InSettings.ModalBasisValues.IsEmpty() || !InSettings.ModalInverseSystemValues.IsEmpty();
	const bool bModalValid = !bModalRequested || (InSettings.ModalModeCount > 0 && InSettings.ModalModeCount <= 16
		&& FMath::IsFinite(InSettings.ModalCorrectionStrength) && InSettings.ModalCorrectionStrength > 0.0f
		&& InSettings.ModalBasisValues.Num() == InitialPositions.Num() * InSettings.ModalModeCount
		&& InSettings.ModalInverseSystemValues.Num() == InSettings.ModalModeCount * InSettings.ModalModeCount);
	const bool bRankOneValid = !InSettings.bEnableRankOneCollisionHessian || (bModalValid && bModalRequested
		&& InSettings.bEnableNonDistanceBarrier
		&& InSettings.ModalSystemValues.Num() == InSettings.ModalModeCount * InSettings.ModalModeCount
		&& InSettings.ModalParticleOuterProductValues.Num()
			== InitialPositions.Num() * InSettings.ModalModeCount * InSettings.ModalModeCount
		&& FMath::IsFinite(InSettings.RankOneMaximumCorrectionCm) && InSettings.RankOneMaximumCorrectionCm > 0.0f);
	if (!IsInGameThread() || !GDynamicRHI || InitialPositions.IsEmpty()
		|| InverseMasses.Num() != InitialPositions.Num() || StructuralEdges.IsEmpty()
		|| StructuralRestLengthsCm.Num() != StructuralEdges.Num() || InSettings.Iterations < 1
		|| !FMath::IsFinite(InSettings.Relaxation) || InSettings.Relaxation <= 0.0f || InSettings.Relaxation > 1.0f
		|| !FMath::IsFinite(InSettings.FixedDeltaSeconds) || InSettings.FixedDeltaSeconds <= 0.0f
		|| !FMath::IsFinite(InSettings.VelocityDamping)
		|| !FMath::IsFinite(InSettings.CollisionThicknessCm) || InSettings.CollisionThicknessCm < 0.0f
		|| !FMath::IsFinite(InSettings.NdbBaseWeight) || InSettings.NdbBaseWeight <= 0.0f
		|| !FMath::IsFinite(InSettings.NdbActivationMultiplier) || InSettings.NdbActivationMultiplier < 1.0f
		|| !FMath::IsFinite(InSettings.NdbMaximumWeight) || InSettings.NdbMaximumWeight < InSettings.NdbBaseWeight
		|| InSettings.CollisionPrimitiveCapacity < 1
		|| !bModalValid || !bRankOneValid
		|| !FMath::IsFinite(InSettings.CcdSafetyOffsetCm) || InSettings.CcdSafetyOffsetCm < 0.0f)
	{
		return false;
	}

	TArray<uint32> Degrees;
	Degrees.Init(0, InitialPositions.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < StructuralEdges.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = StructuralEdges[EdgeIndex];
		const float RestLength = StructuralRestLengthsCm[EdgeIndex];
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= InitialPositions.Num() || Edge.Y >= InitialPositions.Num()
			|| Edge.X == Edge.Y || !FMath::IsFinite(RestLength) || RestLength <= 0.0f)
		{
			return false;
		}
		Degrees[Edge.X]++;
		Degrees[Edge.Y]++;
	}
	for (int32 ParticleIndex = 0; ParticleIndex < InitialPositions.Num(); ++ParticleIndex)
	{
		const FVector3f Position = InitialPositions[ParticleIndex];
		const float InverseMass = InverseMasses[ParticleIndex];
		if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z)
			|| !FMath::IsFinite(InverseMass) || InverseMass < 0.0f)
		{
			return false;
		}
	}

	Shutdown();
	State = MakeShared<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe>();
	State->Settings = InSettings;
	State->Settings.FixedDeltaSeconds = FMath::Max(InSettings.FixedDeltaSeconds, UE_SMALL_NUMBER);
	State->Settings.VelocityDamping = FMath::Clamp(InSettings.VelocityDamping, 0.0f, 1.0f);
	State->Settings.ReadbackIntervalSteps = FMath::Max(InSettings.ReadbackIntervalSteps, 1);
	State->Settings.GpuTimingWarmupSteps = FMath::Max(InSettings.GpuTimingWarmupSteps, 0);
	State->Settings.ModalCorrectionStrength = FMath::Max(InSettings.ModalCorrectionStrength, 0.0f);
	State->Settings.RankOneMaximumCorrectionCm = FMath::Max(InSettings.RankOneMaximumCorrectionCm, UE_SMALL_NUMBER);
	State->Settings.CollisionThicknessCm = FMath::Max(InSettings.CollisionThicknessCm, 0.0f);
	State->Settings.NdbBaseWeight = FMath::Max(InSettings.NdbBaseWeight, UE_SMALL_NUMBER);
	State->Settings.NdbActivationMultiplier = FMath::Max(InSettings.NdbActivationMultiplier, 1.0f);
	State->Settings.NdbMaximumWeight = FMath::Max(InSettings.NdbMaximumWeight, State->Settings.NdbBaseWeight);
	State->Settings.CollisionPrimitiveCapacity = FMath::Max(InSettings.CollisionPrimitiveCapacity, 1);
	State->Settings.CcdSafetyOffsetCm = FMath::Max(InSettings.CcdSafetyOffsetCm, 0.0f);
	State->InitialPositions.Reserve(InitialPositions.Num());
	State->KinematicTargets.Reserve(InitialPositions.Num());
	for (const FVector3f Position : InitialPositions)
	{
		State->InitialPositions.Emplace(Position, 0.0f);
		State->KinematicTargets.Emplace(Position, 1.0f);
	}
	State->InverseMasses.Append(InverseMasses);
	State->CollisionSpheres.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	State->CollisionCapsuleStarts.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	State->CollisionCapsuleEnds.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	State->AdjacencyOffsets.SetNumUninitialized(InitialPositions.Num() + 1);
	State->AdjacencyOffsets[0] = 0;
	for (int32 ParticleIndex = 0; ParticleIndex < InitialPositions.Num(); ++ParticleIndex)
	{
		State->AdjacencyOffsets[ParticleIndex + 1] = State->AdjacencyOffsets[ParticleIndex] + Degrees[ParticleIndex];
	}
	State->NeighborIndices.SetNumUninitialized(StructuralEdges.Num() * 2);
	State->NeighborRestLengthsCm.SetNumUninitialized(StructuralEdges.Num() * 2);
	TArray<uint32> WriteOffsets = State->AdjacencyOffsets;
	for (int32 EdgeIndex = 0; EdgeIndex < StructuralEdges.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = StructuralEdges[EdgeIndex];
		const uint32 AOffset = WriteOffsets[Edge.X]++;
		const uint32 BOffset = WriteOffsets[Edge.Y]++;
		State->NeighborIndices[AOffset] = Edge.Y;
		State->NeighborIndices[BOffset] = Edge.X;
		State->NeighborRestLengthsCm[AOffset] = StructuralRestLengthsCm[EdgeIndex];
		State->NeighborRestLengthsCm[BOffset] = StructuralRestLengthsCm[EdgeIndex];
	}

	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimeInitialize)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			if (StateRef->bShutdown) return;
			bool bGpuTimingSupported = GSupportsTimestampRenderQueries;
			if (bGpuTimingSupported)
			{
				StateRef->GpuTimingSlots.SetNum(8);
				for (FContactClothGpuExplicitTopologyRealtimeState::FGpuTimingSlot& Slot : StateRef->GpuTimingSlots)
				{
					Slot.StartQuery = RHICreateRenderQuery(RQT_AbsoluteTime);
					Slot.EndQuery = RHICreateRenderQuery(RQT_AbsoluteTime);
					bGpuTimingSupported = bGpuTimingSupported && Slot.StartQuery.IsValid() && Slot.EndQuery.IsValid();
				}
				if (!bGpuTimingSupported) StateRef->GpuTimingSlots.Reset();
			}
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferDesc PositionDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->InitialPositions.Num());
			PositionDesc.Usage = static_cast<EBufferUsageFlags>(PositionDesc.Usage | BUF_SourceCopy);
			FRDGBufferRef Current = GraphBuilder.CreateBuffer(PositionDesc, TEXT("ContactCloth.ExplicitRealtime.Current"));
			FRDGBufferRef Previous = GraphBuilder.CreateBuffer(PositionDesc, TEXT("ContactCloth.ExplicitRealtime.Previous"));
			FRDGBufferRef WorkA = GraphBuilder.CreateBuffer(PositionDesc, TEXT("ContactCloth.ExplicitRealtime.WorkA"));
			FRDGBufferRef WorkB = GraphBuilder.CreateBuffer(PositionDesc, TEXT("ContactCloth.ExplicitRealtime.WorkB"));
			const uint32 PositionBytes = StateRef->InitialPositions.Num() * sizeof(FVector4f);
			GraphBuilder.QueueBufferUpload(Current, StateRef->InitialPositions.GetData(), PositionBytes);
			GraphBuilder.QueueBufferUpload(Previous, StateRef->InitialPositions.GetData(), PositionBytes);
			FRDGBufferRef InverseMass = ContactClothGpuExplicitTopology::CreateUploadedBuffer(GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.InverseMass"), StateRef->InverseMasses);
			FRDGBufferRef Offsets = ContactClothGpuExplicitTopology::CreateUploadedBuffer(GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.Offsets"), StateRef->AdjacencyOffsets);
			FRDGBufferRef Neighbors = ContactClothGpuExplicitTopology::CreateUploadedBuffer(GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.Neighbors"), StateRef->NeighborIndices);
			FRDGBufferRef RestLengths = ContactClothGpuExplicitTopology::CreateUploadedBuffer(GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.RestLengths"), StateRef->NeighborRestLengthsCm);
			TArray<uint32> ZeroActivations;
			ZeroActivations.Init(0, StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity);
			FRDGBufferRef CollisionActivations = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.CollisionActivations"), ZeroActivations);
			FRDGBufferRef ModalBasis = nullptr;
			FRDGBufferRef ModalInverseSystem = nullptr;
			FRDGBufferRef ModalSystem = nullptr;
			FRDGBufferRef ModalOuterProducts = nullptr;
			FRDGBufferRef RankOneMatrix = nullptr;
			FRDGBufferRef RankOneRhs = nullptr;
			FRDGBufferRef RankOneSolution = nullptr;
			if (StateRef->Settings.ModalModeCount > 0)
			{
				ModalBasis = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
					GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.ModalBasis"), StateRef->Settings.ModalBasisValues);
				ModalInverseSystem = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
					GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.ModalInverseSystem"), StateRef->Settings.ModalInverseSystemValues);
			}
			if (StateRef->Settings.bEnableRankOneCollisionHessian)
			{
				ModalSystem = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
					GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.ModalSystem"), StateRef->Settings.ModalSystemValues);
				ModalOuterProducts = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
					GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.ModalOuterProducts"), StateRef->Settings.ModalParticleOuterProductValues);
				const int32 DofCount = StateRef->Settings.ModalModeCount * 3;
				RankOneMatrix = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount * DofCount),
					TEXT("ContactCloth.ExplicitRealtime.RankOneMatrix"));
				RankOneRhs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount),
					TEXT("ContactCloth.ExplicitRealtime.RankOneRhs"));
				RankOneSolution = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), DofCount),
					TEXT("ContactCloth.ExplicitRealtime.RankOneSolution"));
			}
			StateRef->CurrentBuffer = GraphBuilder.ConvertToExternalBuffer(Current);
			StateRef->PreviousBuffer = GraphBuilder.ConvertToExternalBuffer(Previous);
			StateRef->WorkBufferA = GraphBuilder.ConvertToExternalBuffer(WorkA);
			StateRef->WorkBufferB = GraphBuilder.ConvertToExternalBuffer(WorkB);
			StateRef->InverseMassBuffer = GraphBuilder.ConvertToExternalBuffer(InverseMass);
			StateRef->AdjacencyOffsetBuffer = GraphBuilder.ConvertToExternalBuffer(Offsets);
			StateRef->NeighborIndexBuffer = GraphBuilder.ConvertToExternalBuffer(Neighbors);
			StateRef->NeighborRestLengthBuffer = GraphBuilder.ConvertToExternalBuffer(RestLengths);
			StateRef->CollisionActivationBuffer = GraphBuilder.ConvertToExternalBuffer(CollisionActivations);
			if (ModalBasis)
			{
				StateRef->ModalBasisBuffer = GraphBuilder.ConvertToExternalBuffer(ModalBasis);
				StateRef->ModalInverseSystemBuffer = GraphBuilder.ConvertToExternalBuffer(ModalInverseSystem);
			}
			if (RankOneMatrix)
			{
				StateRef->ModalSystemBuffer = GraphBuilder.ConvertToExternalBuffer(ModalSystem);
				StateRef->ModalParticleOuterProductBuffer = GraphBuilder.ConvertToExternalBuffer(ModalOuterProducts);
				StateRef->RankOneMatrixBuffer = GraphBuilder.ConvertToExternalBuffer(RankOneMatrix);
				StateRef->RankOneRhsBuffer = GraphBuilder.ConvertToExternalBuffer(RankOneRhs);
				StateRef->RankOneSolutionBuffer = GraphBuilder.ConvertToExternalBuffer(RankOneSolution);
			}
			GraphBuilder.Execute();
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.bInitialized = true;
			StateRef->Stats.bGpuTimingSupported = bGpuTimingSupported;
		});
	return true;
}

bool FContactClothGpuExplicitTopologySimulation::UpdateKinematicTargets(TConstArrayView<FVector3f> TargetPositions)
{
	if (!IsInGameThread() || !State.IsValid() || TargetPositions.Num() != State->InitialPositions.Num()) return false;
	TArray<FVector4f> PackedTargets;
	PackedTargets.Reserve(TargetPositions.Num());
	for (const FVector3f Target : TargetPositions)
	{
		if (!FMath::IsFinite(Target.X) || !FMath::IsFinite(Target.Y) || !FMath::IsFinite(Target.Z)) return false;
		PackedTargets.Emplace(Target, 1.0f);
	}
	FScopeLock Lock(&State->DataLock);
	if (State->bShutdown) return false;
	State->KinematicTargets = MoveTemp(PackedTargets);
	State->KinematicTargetVersion++;
	State->Stats.KinematicTargetUpdatesAccepted++;
	return true;
}

bool FContactClothGpuExplicitTopologySimulation::UpdateCollisionPrimitives(
	TConstArrayView<FContactClothGpuSphere> Spheres,
	TConstArrayView<FContactClothGpuCapsule> Capsules)
{
	if (!IsInGameThread() || !State.IsValid()) return false;
	TArray<FVector4f> PackedSpheres;
	TArray<FVector4f> PackedCapsuleStarts;
	TArray<FVector4f> PackedCapsuleEnds;
	PackedSpheres.Reserve(FMath::Max(Spheres.Num(), 1));
	PackedCapsuleStarts.Reserve(FMath::Max(Capsules.Num(), 1));
	PackedCapsuleEnds.Reserve(FMath::Max(Capsules.Num(), 1));
	const float Thickness = State->Settings.CollisionThicknessCm;
	for (const FContactClothGpuSphere& Sphere : Spheres)
	{
		if (!FMath::IsFinite(Sphere.Center.X) || !FMath::IsFinite(Sphere.Center.Y) || !FMath::IsFinite(Sphere.Center.Z)
			|| !FMath::IsFinite(Sphere.RadiusCm) || Sphere.RadiusCm <= 0.0f)
		{
			return false;
		}
		PackedSpheres.Emplace(Sphere.Center, Sphere.RadiusCm + Thickness);
	}
	for (const FContactClothGpuCapsule& Capsule : Capsules)
	{
		if (!FMath::IsFinite(Capsule.Start.X) || !FMath::IsFinite(Capsule.Start.Y) || !FMath::IsFinite(Capsule.Start.Z)
			|| !FMath::IsFinite(Capsule.End.X) || !FMath::IsFinite(Capsule.End.Y) || !FMath::IsFinite(Capsule.End.Z)
			|| !FMath::IsFinite(Capsule.RadiusCm) || Capsule.RadiusCm <= 0.0f)
		{
			return false;
		}
		PackedCapsuleStarts.Emplace(Capsule.Start, Capsule.RadiusCm + Thickness);
		PackedCapsuleEnds.Emplace(Capsule.End, 0.0f);
	}
	const uint32 SphereCount = PackedSpheres.Num();
	const uint32 CapsuleCount = PackedCapsuleStarts.Num();
	if (State->Settings.bEnableNonDistanceBarrier
		&& SphereCount + CapsuleCount > static_cast<uint32>(State->Settings.CollisionPrimitiveCapacity))
	{
		return false;
	}
	if (PackedSpheres.IsEmpty()) PackedSpheres.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	if (PackedCapsuleStarts.IsEmpty())
	{
		PackedCapsuleStarts.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
		PackedCapsuleEnds.Emplace(0.0f, 0.0f, 0.0f, 0.0f);
	}
	FScopeLock Lock(&State->DataLock);
	if (State->bShutdown) return false;
	State->CollisionSpheres = MoveTemp(PackedSpheres);
	State->CollisionCapsuleStarts = MoveTemp(PackedCapsuleStarts);
	State->CollisionCapsuleEnds = MoveTemp(PackedCapsuleEnds);
	State->CollisionSphereCount = SphereCount;
	State->CollisionCapsuleCount = CapsuleCount;
	State->CollisionPrimitiveVersion++;
	State->Stats.CollisionPrimitiveUpdatesAccepted++;
	return true;
}

bool FContactClothGpuExplicitTopologySimulation::EnqueueStep()
{
	if (!IsInGameThread() || !State.IsValid()) return false;
	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	int64 StepToExecute = 0;
	int64 KinematicTargetVersion = 0;
	int64 CollisionPrimitiveVersion = 0;
	uint32 CollisionSphereCount = 0;
	uint32 CollisionCapsuleCount = 0;
	TArray<FVector4f> KinematicTargetSnapshot;
	TArray<FVector4f> CollisionSphereSnapshot;
	TArray<FVector4f> CollisionCapsuleStartSnapshot;
	TArray<FVector4f> CollisionCapsuleEndSnapshot;
	{
		FScopeLock Lock(&StateRef->DataLock);
		if (StateRef->bShutdown) return false;
		StepToExecute = ++StateRef->Stats.StepsEnqueued;
		KinematicTargetVersion = StateRef->KinematicTargetVersion;
		KinematicTargetSnapshot = StateRef->KinematicTargets;
		CollisionPrimitiveVersion = StateRef->CollisionPrimitiveVersion;
		CollisionSphereCount = StateRef->CollisionSphereCount;
		CollisionCapsuleCount = StateRef->CollisionCapsuleCount;
		CollisionSphereSnapshot = StateRef->CollisionSpheres;
		CollisionCapsuleStartSnapshot = StateRef->CollisionCapsuleStarts;
		CollisionCapsuleEndSnapshot = StateRef->CollisionCapsuleEnds;
	}
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimeStep)(
		[StateRef, StepToExecute, KinematicTargetVersion, CollisionPrimitiveVersion, CollisionSphereCount, CollisionCapsuleCount,
		 KinematicTargetSnapshot = MoveTemp(KinematicTargetSnapshot), CollisionSphereSnapshot = MoveTemp(CollisionSphereSnapshot),
		 CollisionCapsuleStartSnapshot = MoveTemp(CollisionCapsuleStartSnapshot), CollisionCapsuleEndSnapshot = MoveTemp(CollisionCapsuleEndSnapshot)](FRHICommandListImmediate& RHICmdList)
		{
			if (StateRef->bShutdown || !StateRef->CurrentBuffer || !StateRef->PreviousBuffer
				|| !StateRef->WorkBufferA || !StateRef->WorkBufferB || !StateRef->InverseMassBuffer
				|| !StateRef->AdjacencyOffsetBuffer || !StateRef->NeighborIndexBuffer || !StateRef->NeighborRestLengthBuffer
				|| !StateRef->CollisionActivationBuffer)
			{
				return;
			}
			if (StateRef->Settings.ModalModeCount > 0 && (!StateRef->ModalBasisBuffer || !StateRef->ModalInverseSystemBuffer)) return;
			if (StateRef->Settings.bEnableRankOneCollisionHessian
				&& (!StateRef->ModalSystemBuffer || !StateRef->ModalParticleOuterProductBuffer
					|| !StateRef->RankOneMatrixBuffer || !StateRef->RankOneRhsBuffer || !StateRef->RankOneSolutionBuffer)) return;
			ContactClothGpuExplicitTopology::ConsumeReadyRealtimeReadback_RenderThread(StateRef);
			ContactClothGpuExplicitTopology::ConsumeReadyGpuTimings_RenderThread(StateRef);
			FContactClothGpuExplicitTopologyRealtimeState::FGpuTimingSlot* GpuTimingSlot = nullptr;
			if (StateRef->Stats.bGpuTimingSupported)
			{
				for (FContactClothGpuExplicitTopologyRealtimeState::FGpuTimingSlot& Slot : StateRef->GpuTimingSlots)
				{
					if (!Slot.bPending)
					{
						GpuTimingSlot = &Slot;
						break;
					}
				}
				if (GpuTimingSlot)
				{
					RHICmdList.EndRenderQuery(GpuTimingSlot->StartQuery.GetReference());
				}
				else
				{
					FScopeLock Lock(&StateRef->DataLock);
					StateRef->Stats.GpuTimingSamplesSkipped++;
				}
			}
			const double SubmissionStartSeconds = FPlatformTime::Seconds();
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef Current = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentBuffer, TEXT("ContactCloth.ExplicitRealtime.Current"));
			FRDGBufferRef Previous = GraphBuilder.RegisterExternalBuffer(StateRef->PreviousBuffer, TEXT("ContactCloth.ExplicitRealtime.Previous"));
			FRDGBufferRef WorkA = GraphBuilder.RegisterExternalBuffer(StateRef->WorkBufferA, TEXT("ContactCloth.ExplicitRealtime.WorkA"));
			FRDGBufferRef WorkB = GraphBuilder.RegisterExternalBuffer(StateRef->WorkBufferB, TEXT("ContactCloth.ExplicitRealtime.WorkB"));
			FRDGBufferRef InverseMass = GraphBuilder.RegisterExternalBuffer(StateRef->InverseMassBuffer, TEXT("ContactCloth.ExplicitRealtime.InverseMass"));
			FRDGBufferRef Offsets = GraphBuilder.RegisterExternalBuffer(StateRef->AdjacencyOffsetBuffer, TEXT("ContactCloth.ExplicitRealtime.Offsets"));
			FRDGBufferRef Neighbors = GraphBuilder.RegisterExternalBuffer(StateRef->NeighborIndexBuffer, TEXT("ContactCloth.ExplicitRealtime.Neighbors"));
			FRDGBufferRef RestLengths = GraphBuilder.RegisterExternalBuffer(StateRef->NeighborRestLengthBuffer, TEXT("ContactCloth.ExplicitRealtime.RestLengths"));
			FRDGBufferRef CollisionActivations = GraphBuilder.RegisterExternalBuffer(
				StateRef->CollisionActivationBuffer, TEXT("ContactCloth.ExplicitRealtime.CollisionActivations"));
			FRDGBufferRef KinematicTargets = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.KinematicTargets"), KinematicTargetSnapshot);
			FRDGBufferRef CollisionSpheres = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.CollisionSpheres"), CollisionSphereSnapshot);
			FRDGBufferRef CollisionCapsuleStarts = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.CollisionCapsuleStarts"), CollisionCapsuleStartSnapshot);
			FRDGBufferRef CollisionCapsuleEnds = ContactClothGpuExplicitTopology::CreateUploadedBuffer(
				GraphBuilder, TEXT("ContactCloth.ExplicitRealtime.CollisionCapsuleEnds"), CollisionCapsuleEndSnapshot);

			TShaderMapRef<FContactClothExplicitTopologyIntegrateCS> IntegrateShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FContactClothExplicitTopologyIntegrateCS::FParameters* IntegrateParameters =
				GraphBuilder.AllocParameters<FContactClothExplicitTopologyIntegrateCS::FParameters>();
			IntegrateParameters->ParticleCount = StateRef->InitialPositions.Num();
			IntegrateParameters->FixedDeltaSecondsSquared = FMath::Square(StateRef->Settings.FixedDeltaSeconds);
			IntegrateParameters->VelocityDamping = StateRef->Settings.VelocityDamping;
			IntegrateParameters->GravityCmPerSecondSquared = StateRef->Settings.GravityCmPerSecondSquared;
			IntegrateParameters->InverseMasses = GraphBuilder.CreateSRV(InverseMass);
			IntegrateParameters->CurrentPositions = GraphBuilder.CreateSRV(Current);
			IntegrateParameters->PreviousPositions = GraphBuilder.CreateSRV(Previous);
			IntegrateParameters->KinematicTargets = GraphBuilder.CreateSRV(KinematicTargets);
			IntegrateParameters->PredictedPositions = GraphBuilder.CreateUAV(WorkA);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ContactCloth Explicit Integrate Step %lld", StepToExecute),
				IntegrateShader,
				IntegrateParameters,
				FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));

			FRDGBufferRef SolverCurrent = WorkA;
			FRDGBufferRef SolverNext = WorkB;
			FRDGBufferRef ModalBasis = nullptr;
			if (StateRef->Settings.ModalModeCount > 0)
			{
				ModalBasis = GraphBuilder.RegisterExternalBuffer(StateRef->ModalBasisBuffer, TEXT("ContactCloth.ExplicitRealtime.ModalBasis"));
				FRDGBufferRef ModalInverseSystem = GraphBuilder.RegisterExternalBuffer(
					StateRef->ModalInverseSystemBuffer, TEXT("ContactCloth.ExplicitRealtime.ModalInverseSystem"));
				FRDGBufferRef ModalRhs = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), StateRef->Settings.ModalModeCount),
					TEXT("ContactCloth.ExplicitRealtime.ModalRhs"));
				TShaderMapRef<FContactClothExplicitTopologyModalProjectCS> ProjectShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				auto* ProjectParameters = GraphBuilder.AllocParameters<FContactClothExplicitTopologyModalProjectCS::FParameters>();
				ProjectParameters->ExplicitModalParticleCount = StateRef->InitialPositions.Num();
				ProjectParameters->ExplicitModalModeCount = StateRef->Settings.ModalModeCount;
				ProjectParameters->ExplicitModalInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
				ProjectParameters->ExplicitModalTargetPositions = GraphBuilder.CreateSRV(KinematicTargets);
				ProjectParameters->ExplicitModalInverseMasses = GraphBuilder.CreateSRV(InverseMass);
				ProjectParameters->ExplicitModalBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				ProjectParameters->ExplicitModalOutputValues = GraphBuilder.CreateUAV(ModalRhs);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Explicit Modal Project Step %lld", StepToExecute),
					ProjectShader, ProjectParameters, FIntVector(FMath::DivideAndRoundUp(StateRef->Settings.ModalModeCount, 64), 1, 1));

				TShaderMapRef<FContactClothExplicitTopologyModalApplyCS> ApplyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				auto* ApplyParameters = GraphBuilder.AllocParameters<FContactClothExplicitTopologyModalApplyCS::FParameters>();
				ApplyParameters->ExplicitModalParticleCount = StateRef->InitialPositions.Num();
				ApplyParameters->ExplicitModalModeCount = StateRef->Settings.ModalModeCount;
				ApplyParameters->ExplicitModalCorrectionStrength = StateRef->Settings.ModalCorrectionStrength;
				ApplyParameters->ExplicitModalInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
				ApplyParameters->ExplicitModalInverseMasses = GraphBuilder.CreateSRV(InverseMass);
				ApplyParameters->ExplicitModalBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				ApplyParameters->ExplicitModalInverseSystemValues = GraphBuilder.CreateSRV(ModalInverseSystem);
				ApplyParameters->ExplicitModalRhsValues = GraphBuilder.CreateSRV(ModalRhs);
				ApplyParameters->ExplicitModalOutputValues = GraphBuilder.CreateUAV(SolverNext);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Explicit Modal Apply Step %lld", StepToExecute),
					ApplyShader, ApplyParameters, FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				Swap(SolverCurrent, SolverNext);
			}

			if (StateRef->Settings.bEnableNonDistanceBarrier)
			{
				TShaderMapRef<FContactClothExplicitTopologyNdbStateCS> NdbShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothExplicitTopologyNdbStateCS::FParameters* NdbParameters =
					GraphBuilder.AllocParameters<FContactClothExplicitTopologyNdbStateCS::FParameters>();
				NdbParameters->ExplicitNdbParticleCount = StateRef->InitialPositions.Num();
				NdbParameters->ExplicitNdbSphereCount = CollisionSphereCount;
				NdbParameters->ExplicitNdbCapsuleCount = CollisionCapsuleCount;
				NdbParameters->ExplicitNdbPrimitiveCapacity = StateRef->Settings.CollisionPrimitiveCapacity;
				NdbParameters->ExplicitNdbInverseMasses = GraphBuilder.CreateSRV(InverseMass);
				NdbParameters->ExplicitNdbPositions = GraphBuilder.CreateSRV(SolverCurrent);
				NdbParameters->ExplicitNdbCollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				NdbParameters->ExplicitNdbCollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				NdbParameters->ExplicitNdbCollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				NdbParameters->ExplicitNdbActivationCounts = GraphBuilder.CreateUAV(CollisionActivations);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Explicit NDB State Step %lld", StepToExecute),
					NdbShader,
					NdbParameters,
					FIntVector(FMath::DivideAndRoundUp(
						StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity, 64), 1, 1));
			}

			if (StateRef->Settings.bEnableRankOneCollisionHessian)
			{
				FRDGBufferRef ModalSystem = GraphBuilder.RegisterExternalBuffer(StateRef->ModalSystemBuffer,
					TEXT("ContactCloth.ExplicitRealtime.ModalSystem"));
				FRDGBufferRef OuterProducts = GraphBuilder.RegisterExternalBuffer(StateRef->ModalParticleOuterProductBuffer,
					TEXT("ContactCloth.ExplicitRealtime.ModalOuterProducts"));
				FRDGBufferRef RankOneMatrix = GraphBuilder.RegisterExternalBuffer(StateRef->RankOneMatrixBuffer,
					TEXT("ContactCloth.ExplicitRealtime.RankOneMatrix"));
				FRDGBufferRef RankOneRhs = GraphBuilder.RegisterExternalBuffer(StateRef->RankOneRhsBuffer,
					TEXT("ContactCloth.ExplicitRealtime.RankOneRhs"));
				FRDGBufferRef RankOneSolution = GraphBuilder.RegisterExternalBuffer(StateRef->RankOneSolutionBuffer,
					TEXT("ContactCloth.ExplicitRealtime.RankOneSolution"));
				const int32 DofCount = StateRef->Settings.ModalModeCount * 3;
				TShaderMapRef<FContactClothExplicitTopologyRankOneAssemblyCS> AssemblyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				auto* Assembly = GraphBuilder.AllocParameters<FContactClothExplicitTopologyRankOneAssemblyCS::FParameters>();
				Assembly->ExplicitRankOneParticleCount = StateRef->InitialPositions.Num();
				Assembly->ExplicitRankOneModeCount = StateRef->Settings.ModalModeCount;
				Assembly->ExplicitRankOneSphereCount = CollisionSphereCount;
				Assembly->ExplicitRankOneCapsuleCount = CollisionCapsuleCount;
				Assembly->ExplicitRankOnePrimitiveCapacity = StateRef->Settings.CollisionPrimitiveCapacity;
				Assembly->ExplicitRankOneBaseWeight = StateRef->Settings.NdbBaseWeight;
				Assembly->ExplicitRankOneActivationMultiplier = StateRef->Settings.NdbActivationMultiplier;
				Assembly->ExplicitRankOneMaximumWeight = StateRef->Settings.NdbMaximumWeight;
				Assembly->ExplicitRankOnePositions = GraphBuilder.CreateSRV(SolverCurrent);
				Assembly->ExplicitRankOneInverseMasses = GraphBuilder.CreateSRV(InverseMass);
				Assembly->ExplicitRankOneCollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				Assembly->ExplicitRankOneCollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				Assembly->ExplicitRankOneCollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				Assembly->ExplicitRankOneActivationCounts = GraphBuilder.CreateSRV(CollisionActivations);
				Assembly->ExplicitRankOneBaseSystem = GraphBuilder.CreateSRV(ModalSystem);
				Assembly->ExplicitRankOneBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				Assembly->ExplicitRankOneParticleOuterProducts = GraphBuilder.CreateSRV(OuterProducts);
				Assembly->ExplicitRankOneOutputMatrix = GraphBuilder.CreateUAV(RankOneMatrix);
				Assembly->ExplicitRankOneOutputRhs = GraphBuilder.CreateUAV(RankOneRhs);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Explicit RankOne Assemble Step %lld", StepToExecute),
					AssemblyShader, Assembly, FIntVector(FMath::DivideAndRoundUp(DofCount * DofCount, 64), 1, 1));

				TShaderMapRef<FContactClothExplicitTopologyRankOneSolveCS> SolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				auto* Solve = GraphBuilder.AllocParameters<FContactClothExplicitTopologyRankOneSolveCS::FParameters>();
				Solve->ExplicitRankOneSolveModeCount = StateRef->Settings.ModalModeCount;
				Solve->ExplicitRankOneSolveMatrix = GraphBuilder.CreateSRV(RankOneMatrix);
				Solve->ExplicitRankOneSolveRhs = GraphBuilder.CreateSRV(RankOneRhs);
				Solve->ExplicitRankOneSolution = GraphBuilder.CreateUAV(RankOneSolution);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Explicit RankOne Solve Step %lld", StepToExecute),
					SolveShader, Solve, FIntVector(1, 1, 1));

				TShaderMapRef<FContactClothExplicitTopologyRankOneApplyCS> RankApplyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				auto* RankApply = GraphBuilder.AllocParameters<FContactClothExplicitTopologyRankOneApplyCS::FParameters>();
				RankApply->ExplicitRankOneApplyParticleCount = StateRef->InitialPositions.Num();
				RankApply->ExplicitRankOneApplyModeCount = StateRef->Settings.ModalModeCount;
				RankApply->ExplicitRankOneMaximumCorrectionCm = StateRef->Settings.RankOneMaximumCorrectionCm;
				RankApply->ExplicitRankOneApplyInputPositions = GraphBuilder.CreateSRV(SolverCurrent);
				RankApply->ExplicitRankOneApplyInverseMasses = GraphBuilder.CreateSRV(InverseMass);
				RankApply->ExplicitRankOneApplyBasisValues = GraphBuilder.CreateSRV(ModalBasis);
				RankApply->ExplicitRankOneApplySolution = GraphBuilder.CreateSRV(RankOneSolution);
				RankApply->ExplicitRankOneApplyOutputPositions = GraphBuilder.CreateUAV(SolverNext);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ContactCloth Explicit RankOne Apply Step %lld", StepToExecute),
					RankApplyShader, RankApply, FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				Swap(SolverCurrent, SolverNext);
			}

			TShaderMapRef<FContactClothExplicitTopologyJacobiCS> JacobiShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRDGBufferRef JacobiCurrent = SolverCurrent;
			FRDGBufferRef JacobiNext = SolverNext;
			for (int32 Iteration = 0; Iteration < StateRef->Settings.Iterations; ++Iteration)
			{
				FContactClothExplicitTopologyJacobiCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FContactClothExplicitTopologyJacobiCS::FParameters>();
				Parameters->ParticleCount = StateRef->InitialPositions.Num();
				Parameters->CollisionSphereCount = CollisionSphereCount;
				Parameters->CollisionCapsuleCount = CollisionCapsuleCount;
				Parameters->CollisionPrimitiveCapacity = StateRef->Settings.CollisionPrimitiveCapacity;
				Parameters->NdbEnabled = StateRef->Settings.bEnableNonDistanceBarrier ? 1u : 0u;
				Parameters->Relaxation = StateRef->Settings.Relaxation;
				Parameters->NdbBaseWeight = StateRef->Settings.NdbBaseWeight;
				Parameters->NdbActivationMultiplier = StateRef->Settings.NdbActivationMultiplier;
				Parameters->NdbMaximumWeight = StateRef->Settings.NdbMaximumWeight;
				Parameters->InverseMasses = GraphBuilder.CreateSRV(InverseMass);
				Parameters->AdjacencyOffsets = GraphBuilder.CreateSRV(Offsets);
				Parameters->NeighborIndices = GraphBuilder.CreateSRV(Neighbors);
				Parameters->NeighborRestLengthsCm = GraphBuilder.CreateSRV(RestLengths);
				Parameters->InputPositions = GraphBuilder.CreateSRV(JacobiCurrent);
				Parameters->CollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				Parameters->CollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				Parameters->CollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				Parameters->CollisionActivationCounts = GraphBuilder.CreateSRV(CollisionActivations);
				Parameters->OutputPositions = GraphBuilder.CreateUAV(JacobiNext);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Explicit Realtime Jacobi %d", Iteration),
					JacobiShader,
					Parameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				Swap(JacobiCurrent, JacobiNext);
			}
			if (StateRef->Settings.bEnableCompleteCcd)
			{
				TShaderMapRef<FContactClothExplicitTopologyCompleteCcdCS> CcdShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FContactClothExplicitTopologyCompleteCcdCS::FParameters* CcdParameters =
					GraphBuilder.AllocParameters<FContactClothExplicitTopologyCompleteCcdCS::FParameters>();
				CcdParameters->ExplicitCcdParticleCount = StateRef->InitialPositions.Num();
				CcdParameters->ExplicitCcdSphereCount = CollisionSphereCount;
				CcdParameters->ExplicitCcdCapsuleCount = CollisionCapsuleCount;
				CcdParameters->ExplicitCcdSafetyOffsetCm = StateRef->Settings.CcdSafetyOffsetCm;
				CcdParameters->ExplicitCcdInverseMasses = GraphBuilder.CreateSRV(InverseMass);
				CcdParameters->ExplicitCcdStartPositions = GraphBuilder.CreateSRV(Current);
				CcdParameters->ExplicitCcdCandidatePositions = GraphBuilder.CreateSRV(JacobiCurrent);
				CcdParameters->ExplicitCcdCollisionSpheres = GraphBuilder.CreateSRV(CollisionSpheres);
				CcdParameters->ExplicitCcdCollisionCapsuleStarts = GraphBuilder.CreateSRV(CollisionCapsuleStarts);
				CcdParameters->ExplicitCcdCollisionCapsuleEnds = GraphBuilder.CreateSRV(CollisionCapsuleEnds);
				CcdParameters->ExplicitCcdOutputPositions = GraphBuilder.CreateUAV(JacobiNext);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ContactCloth Explicit Complete CCD Step %lld", StepToExecute),
					CcdShader,
					CcdParameters,
					FIntVector(FMath::DivideAndRoundUp(StateRef->InitialPositions.Num(), 64), 1, 1));
				JacobiCurrent = JacobiNext;
			}

			const bool bResultInWorkA = JacobiCurrent == WorkA;
			if (!StateRef->PendingReadback && StepToExecute % StateRef->Settings.ReadbackIntervalSteps == 0)
			{
				StateRef->PendingReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.ExplicitRealtime.AsyncReadback"));
				StateRef->PendingReadbackStep = StepToExecute;
				AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadback.Get(), JacobiCurrent,
					StateRef->InitialPositions.Num() * sizeof(FVector4f));
				if (StateRef->Settings.bEnableNonDistanceBarrier)
				{
					StateRef->PendingActivationReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.ExplicitRealtime.ActivationReadback"));
					AddEnqueueCopyPass(GraphBuilder, StateRef->PendingActivationReadback.Get(), CollisionActivations,
						StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity * sizeof(uint32));
				}
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
			const double SubmissionMilliseconds = (FPlatformTime::Seconds() - SubmissionStartSeconds) * 1000.0;
			TRefCountPtr<FRDGPooledBuffer> OldPrevious = StateRef->PreviousBuffer;
			TRefCountPtr<FRDGPooledBuffer> OldCurrent = StateRef->CurrentBuffer;
			TRefCountPtr<FRDGPooledBuffer> ResultBuffer = bResultInWorkA ? StateRef->WorkBufferA : StateRef->WorkBufferB;
			StateRef->PreviousBuffer = OldCurrent;
			StateRef->CurrentBuffer = ResultBuffer;
			if (bResultInWorkA) StateRef->WorkBufferA = OldPrevious; else StateRef->WorkBufferB = OldPrevious;
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.StepsExecuted = StepToExecute;
			StateRef->Stats.LatestRenderSubmissionMilliseconds = SubmissionMilliseconds;
			StateRef->Stats.TotalRenderSubmissionMilliseconds += SubmissionMilliseconds;
			StateRef->Stats.MaximumRenderSubmissionMilliseconds = FMath::Max(
				StateRef->Stats.MaximumRenderSubmissionMilliseconds, SubmissionMilliseconds);
			StateRef->Stats.KinematicTargetApplicationsExecuted++;
			StateRef->Stats.LatestKinematicTargetVersion = KinematicTargetVersion;
			StateRef->Stats.CollisionPrimitiveApplicationsExecuted++;
			StateRef->Stats.LatestCollisionPrimitiveVersion = CollisionPrimitiveVersion;
			StateRef->Stats.LatestCollisionSphereCount = CollisionSphereCount;
			StateRef->Stats.LatestCollisionCapsuleCount = CollisionCapsuleCount;
			if (StateRef->Settings.ModalModeCount > 0)
			{
				StateRef->Stats.ModalCorrectionsExecuted++;
			}
			if (StateRef->Settings.bEnableRankOneCollisionHessian)
			{
				StateRef->Stats.RankOneCollisionSystemsExecuted++;
			}
			if (StateRef->Settings.bEnableCompleteCcd)
			{
				StateRef->Stats.CompleteCcdPassesExecuted++;
			}
			if (StateRef->Settings.bEnableNonDistanceBarrier)
			{
				StateRef->Stats.NdbStateUpdatesExecuted++;
			}
		});
	return true;
}

void FContactClothGpuExplicitTopologySimulation::RequestReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimeRequestReadback)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			if (StateRef->bShutdown || !StateRef->CurrentBuffer) return;
			ContactClothGpuExplicitTopology::ConsumeReadyRealtimeReadback_RenderThread(StateRef);
			ContactClothGpuExplicitTopology::ConsumeReadyGpuTimings_RenderThread(StateRef);
			if (StateRef->PendingReadback || StateRef->PendingActivationReadback) return;
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef Current = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentBuffer, TEXT("ContactCloth.ExplicitRealtime.ReadbackCurrent"));
			FRDGBufferRef CollisionActivations = GraphBuilder.RegisterExternalBuffer(
				StateRef->CollisionActivationBuffer, TEXT("ContactCloth.ExplicitRealtime.ReadbackActivations"));
			StateRef->PendingReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.ExplicitRealtime.RequestedReadback"));
			{
				FScopeLock Lock(&StateRef->DataLock);
				StateRef->PendingReadbackStep = StateRef->Stats.StepsExecuted;
				StateRef->Stats.bReadbackPending = true;
			}
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadback.Get(), Current,
				StateRef->InitialPositions.Num() * sizeof(FVector4f));
			if (StateRef->Settings.bEnableNonDistanceBarrier)
			{
				StateRef->PendingActivationReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactCloth.ExplicitRealtime.RequestedActivationReadback"));
				AddEnqueueCopyPass(GraphBuilder, StateRef->PendingActivationReadback.Get(), CollisionActivations,
					StateRef->InitialPositions.Num() * StateRef->Settings.CollisionPrimitiveCapacity * sizeof(uint32));
			}
			GraphBuilder.Execute();
		});
}

void FContactClothGpuExplicitTopologySimulation::PollReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimePollReadback)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			if (!StateRef->bShutdown)
			{
				ContactClothGpuExplicitTopology::ConsumeReadyRealtimeReadback_RenderThread(StateRef);
				ContactClothGpuExplicitTopology::ConsumeReadyGpuTimings_RenderThread(StateRef);
			}
		});
}

bool FContactClothGpuExplicitTopologySimulation::ConsumeLatestPositions(TArray<FVector3f>& OutPositions, int64& OutStepIndex)
{
	if (!State.IsValid()) return false;
	FScopeLock Lock(&State->DataLock);
	if (!State->bLatestPositionsAvailable) return false;
	OutPositions = State->LatestPositions;
	OutStepIndex = State->Stats.LatestReadbackStep;
	State->bLatestPositionsAvailable = false;
	return true;
}

FContactClothGpuExplicitTopologyRealtimeStats FContactClothGpuExplicitTopologySimulation::GetStats() const
{
	if (!State.IsValid()) return FContactClothGpuExplicitTopologyRealtimeStats();
	FScopeLock Lock(&State->DataLock);
	return State->Stats;
}

void FContactClothGpuExplicitTopologySimulation::GetGpuTimingSamples(
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

void FContactClothGpuExplicitTopologySimulation::FlushAndWaitForReadbackForValidation()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimeValidationWait)(
		[](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	RequestReadback();
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimeReadbackWait)(
		[](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	PollReadback();
	FlushRenderingCommands();
}

void FContactClothGpuExplicitTopologySimulation::Shutdown()
{
	if (!State.IsValid()) return;
	const TSharedRef<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	{
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->bShutdown = true;
	}
	ENQUEUE_RENDER_COMMAND(ContactClothGpuExplicitTopologyRealtimeShutdown)(
		[StateRef](FRHICommandListImmediate& RHICmdList)
		{
			StateRef->PendingReadback.Reset();
			StateRef->PendingActivationReadback.Reset();
			StateRef->GpuTimingSlots.Reset();
			StateRef->GpuTimingSteps.Reset();
			StateRef->GpuStepMilliseconds.Reset();
			StateRef->CurrentBuffer.SafeRelease();
			StateRef->PreviousBuffer.SafeRelease();
			StateRef->WorkBufferA.SafeRelease();
			StateRef->WorkBufferB.SafeRelease();
			StateRef->InverseMassBuffer.SafeRelease();
			StateRef->AdjacencyOffsetBuffer.SafeRelease();
			StateRef->NeighborIndexBuffer.SafeRelease();
			StateRef->NeighborRestLengthBuffer.SafeRelease();
			StateRef->CollisionActivationBuffer.SafeRelease();
			StateRef->ModalBasisBuffer.SafeRelease();
			StateRef->ModalInverseSystemBuffer.SafeRelease();
			StateRef->ModalSystemBuffer.SafeRelease();
			StateRef->ModalParticleOuterProductBuffer.SafeRelease();
			StateRef->RankOneMatrixBuffer.SafeRelease();
			StateRef->RankOneRhsBuffer.SafeRelease();
			StateRef->RankOneSolutionBuffer.SafeRelease();
		});
	State.Reset();
}
