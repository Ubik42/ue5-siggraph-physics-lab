#include "FlowMapGpuImpulseTransfer.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

#define FLOW_MAP_IMPULSE_COMMON_PARAMETERS \
	SHADER_PARAMETER(uint32, ImpulseCellCountX) \
	SHADER_PARAMETER(uint32, ImpulseCellCountY) \
	SHADER_PARAMETER(uint32, ImpulseParticleCount) \
	SHADER_PARAMETER(float, ImpulseCellSizeCm) \
	SHADER_PARAMETER(FVector2f, ImpulseOriginCm) \
	SHADER_PARAMETER(uint32, ImpulseUseGradient) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ImpulseParticleStateA) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ImpulseParticleStateB) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceImpulseAndGradientRow0) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceGradientRow1)

class FFlowMapImpulseGatherUCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapImpulseGatherUCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapImpulseGatherUCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		FLOW_MAP_IMPULSE_COMMON_PARAMETERS
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputImpulseFaceU)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapImpulseGatherUCS, "/Plugin/PhysicsResearch/Private/FlowMapImpulseTransfer.usf", "GatherImpulseUCS", SF_Compute);

class FFlowMapImpulseGatherVCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapImpulseGatherVCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapImpulseGatherVCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		FLOW_MAP_IMPULSE_COMMON_PARAMETERS
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputImpulseFaceV)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapImpulseGatherVCS, "/Plugin/PhysicsResearch/Private/FlowMapImpulseTransfer.usf", "GatherImpulseVCS", SF_Compute);

#undef FLOW_MAP_IMPULSE_COMMON_PARAMETERS

namespace
{
	struct FFlowMapImpulseDispatchState
	{
		FFlowMapGpuImpulseTransferInput Input;
		FFlowMapGpuImpulseTransferResult Result;
	};

	bool IsFiniteParticle(const FFlowMapGpuImpulseParticle& Particle)
	{
		const FVector4f G = Particle.ReferenceImpulseGradientPerSecond;
		const FVector4f B = Particle.BackwardMapJacobian;
		return FMath::IsFinite(Particle.PositionCm.X) && FMath::IsFinite(Particle.PositionCm.Y)
			&& FMath::IsFinite(Particle.ReferenceImpulseCmPerSecond.X) && FMath::IsFinite(Particle.ReferenceImpulseCmPerSecond.Y)
			&& FMath::IsFinite(G.X) && FMath::IsFinite(G.Y) && FMath::IsFinite(G.Z) && FMath::IsFinite(G.W)
			&& FMath::IsFinite(B.X) && FMath::IsFinite(B.Y) && FMath::IsFinite(B.Z) && FMath::IsFinite(B.W);
	}

	template <typename ShaderParameters>
	void SetCommonParameters(ShaderParameters* Parameters, FRDGBuilder& GraphBuilder,
		const FFlowMapImpulseDispatchState& State, FRDGBufferRef ParticleA, FRDGBufferRef ParticleB,
		FRDGBufferRef Payload0, FRDGBufferRef Payload1)
	{
		Parameters->ImpulseCellCountX = State.Input.CellCounts.X;
		Parameters->ImpulseCellCountY = State.Input.CellCounts.Y;
		Parameters->ImpulseParticleCount = State.Input.Particles.Num();
		Parameters->ImpulseCellSizeCm = State.Input.CellSizeCm;
		Parameters->ImpulseOriginCm = State.Input.OriginCm;
		Parameters->ImpulseUseGradient = State.Input.bUseGradient ? 1u : 0u;
		Parameters->ImpulseParticleStateA = GraphBuilder.CreateSRV(ParticleA);
		Parameters->ImpulseParticleStateB = GraphBuilder.CreateSRV(ParticleB);
		Parameters->ReferenceImpulseAndGradientRow0 = GraphBuilder.CreateSRV(Payload0);
		Parameters->ReferenceGradientRow1 = GraphBuilder.CreateSRV(Payload1);
	}
}

FFlowMapGpuImpulseTransferResult RunFlowMapGpuImpulseTransferForValidation(const FFlowMapGpuImpulseTransferInput& Input)
{
	FFlowMapGpuImpulseTransferResult Invalid;
	if (!GDynamicRHI || Input.CellCounts.X < 2 || Input.CellCounts.Y < 2 || !FMath::IsFinite(Input.CellSizeCm)
		|| Input.CellSizeCm <= 0.0f || !FMath::IsFinite(Input.OriginCm.X) || !FMath::IsFinite(Input.OriginCm.Y)
		|| Input.Particles.IsEmpty())
	{
		Invalid.FailureReason = TEXT("Invalid Flow Map GPU impulse transfer input contract");
		return Invalid;
	}
	for (const FFlowMapGpuImpulseParticle& Particle : Input.Particles)
	{
		if (!IsFiniteParticle(Particle))
		{
			Invalid.FailureReason = TEXT("Flow Map GPU impulse particle contains non-finite data");
			return Invalid;
		}
	}
	const TSharedRef<FFlowMapImpulseDispatchState, ESPMode::ThreadSafe> State = MakeShared<FFlowMapImpulseDispatchState, ESPMode::ThreadSafe>();
	State->Input = Input;
	ENQUEUE_RENDER_COMMAND(FlowMapGpuImpulseTransferValidation)([State](FRHICommandListImmediate& RHICmdList)
	{
		TArray<FVector4f> ParticleA;
		TArray<FVector4f> ParticleB;
		TArray<FVector4f> Payload0;
		TArray<FVector4f> Payload1;
		ParticleA.Reserve(State->Input.Particles.Num()); ParticleB.Reserve(State->Input.Particles.Num());
		Payload0.Reserve(State->Input.Particles.Num()); Payload1.Reserve(State->Input.Particles.Num());
		for (const FFlowMapGpuImpulseParticle& Particle : State->Input.Particles)
		{
			const FVector4f B = Particle.BackwardMapJacobian;
			const FVector4f G = Particle.ReferenceImpulseGradientPerSecond;
			ParticleA.Emplace(Particle.PositionCm.X, Particle.PositionCm.Y, B.X, B.Y);
			ParticleB.Emplace(B.Z, B.W, 0.0f, 0.0f);
			Payload0.Emplace(Particle.ReferenceImpulseCmPerSecond.X, Particle.ReferenceImpulseCmPerSecond.Y, G.X, G.Y);
			Payload1.Emplace(G.Z, G.W, 0.0f, 0.0f);
		}
		FRDGBuilder GraphBuilder(RHICmdList);
		auto Upload = [&GraphBuilder](const TCHAR* Name, const TArray<FVector4f>& Values)
		{
			FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), Values.Num()), Name);
			GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(FVector4f));
			return Buffer;
		};
		FRDGBufferRef GpuParticleA = Upload(TEXT("FlowMap.Impulse.ParticleA"), ParticleA);
		FRDGBufferRef GpuParticleB = Upload(TEXT("FlowMap.Impulse.ParticleB"), ParticleB);
		FRDGBufferRef GpuPayload0 = Upload(TEXT("FlowMap.Impulse.Payload0"), Payload0);
		FRDGBufferRef GpuPayload1 = Upload(TEXT("FlowMap.Impulse.Payload1"), Payload1);
		const int32 UCount = (State->Input.CellCounts.X + 1) * State->Input.CellCounts.Y;
		const int32 VCount = State->Input.CellCounts.X * (State->Input.CellCounts.Y + 1);
		FRDGBufferDesc UDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), UCount);
		FRDGBufferDesc VDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VCount);
		UDesc.Usage = static_cast<EBufferUsageFlags>(UDesc.Usage | BUF_SourceCopy);
		VDesc.Usage = static_cast<EBufferUsageFlags>(VDesc.Usage | BUF_SourceCopy);
		FRDGBufferRef OutputU = GraphBuilder.CreateBuffer(UDesc, TEXT("FlowMap.Impulse.OutputU"));
		FRDGBufferRef OutputV = GraphBuilder.CreateBuffer(VDesc, TEXT("FlowMap.Impulse.OutputV"));
		TShaderMapRef<FFlowMapImpulseGatherUCS> UShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* UParameters = GraphBuilder.AllocParameters<FFlowMapImpulseGatherUCS::FParameters>();
		SetCommonParameters(UParameters, GraphBuilder, *State, GpuParticleA, GpuParticleB, GpuPayload0, GpuPayload1);
		UParameters->OutputImpulseFaceU = GraphBuilder.CreateUAV(OutputU);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Impulse Gather U"), UShader, UParameters,
			FIntVector(FMath::DivideAndRoundUp(UCount, 64), 1, 1));
		TShaderMapRef<FFlowMapImpulseGatherVCS> VShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* VParameters = GraphBuilder.AllocParameters<FFlowMapImpulseGatherVCS::FParameters>();
		SetCommonParameters(VParameters, GraphBuilder, *State, GpuParticleA, GpuParticleB, GpuPayload0, GpuPayload1);
		VParameters->OutputImpulseFaceV = GraphBuilder.CreateUAV(OutputV);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Impulse Gather V"), VShader, VParameters,
			FIntVector(FMath::DivideAndRoundUp(VCount, 64), 1, 1));
		State->Result.GatherPasses = 2;
		FRHIGPUBufferReadback ReadbackU(TEXT("FlowMap.Impulse.ReadbackU"));
		FRHIGPUBufferReadback ReadbackV(TEXT("FlowMap.Impulse.ReadbackV"));
		const uint32 BytesU = UCount * sizeof(float);
		const uint32 BytesV = VCount * sizeof(float);
		AddEnqueueCopyPass(GraphBuilder, &ReadbackU, OutputU, BytesU);
		AddEnqueueCopyPass(GraphBuilder, &ReadbackV, OutputV, BytesV);
		GraphBuilder.Execute();
		State->Result.bDispatched = true;
		RHICmdList.BlockUntilGPUIdle();
		if (ReadbackU.IsReady() && ReadbackV.IsReady())
		{
			const void* DataU = ReadbackU.Lock(BytesU);
			const void* DataV = ReadbackV.Lock(BytesV);
			if (DataU && DataV)
			{
				State->Result.FaceU.SetNumUninitialized(UCount); State->Result.FaceV.SetNumUninitialized(VCount);
				FMemory::Memcpy(State->Result.FaceU.GetData(), DataU, BytesU);
				FMemory::Memcpy(State->Result.FaceV.GetData(), DataV, BytesV);
				State->Result.bReadbackSucceeded = true;
			}
			ReadbackU.Unlock(); ReadbackV.Unlock();
		}
		else State->Result.FailureReason = TEXT("Flow Map GPU impulse readback was not ready");
	});
	FlushRenderingCommands();
	return MoveTemp(State->Result);
}
