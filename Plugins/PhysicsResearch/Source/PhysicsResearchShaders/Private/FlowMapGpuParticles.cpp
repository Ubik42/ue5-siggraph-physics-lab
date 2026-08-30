#include "FlowMapGpuParticles.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FFlowMapParticleAdvectCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapParticleAdvectCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapParticleAdvectCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX)
		SHADER_PARAMETER(uint32, CellCountY)
		SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER(FVector2f, OriginCm)
		SHADER_PARAMETER(float, FixedDeltaSeconds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ParticleFaceU)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ParticleFaceV)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InputParticleStateA)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InputParticleStateB)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutputParticleStateA)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutputParticleStateB)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapParticleAdvectCS, "/Plugin/PhysicsResearch/Private/FlowMapParticleAdvection.usf", "AdvectParticlesCS", SF_Compute);

class FFlowMapGpuParticleStateInternal
{
public:
	mutable FCriticalSection DataLock;
	FFlowMapGpuParticleInput Input;
	FFlowMapGpuParticleSettings Settings;
	FFlowMapGpuParticleStats Stats;
	TArray<FVector4f> InitialStateA;
	TArray<FVector4f> InitialStateB;
	TRefCountPtr<FRDGPooledBuffer> FaceU;
	TRefCountPtr<FRDGPooledBuffer> FaceV;
	TRefCountPtr<FRDGPooledBuffer> CurrentStateA;
	TRefCountPtr<FRDGPooledBuffer> CurrentStateB;
	TRefCountPtr<FRDGPooledBuffer> WorkStateA;
	TRefCountPtr<FRDGPooledBuffer> WorkStateB;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadbackA;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadbackB;
	TArray<FFlowMapGpuParticleState> LatestParticles;
	int64 PendingReadbackStep = 0;
	bool bLatestParticlesAvailable = false;
	bool bShutdown = false;
};

namespace FlowMapGpuParticles
{
	bool IsFiniteParticle(const FFlowMapGpuParticleState& Particle)
	{
		return FMath::IsFinite(Particle.PositionCm.X) && FMath::IsFinite(Particle.PositionCm.Y)
			&& FMath::IsFinite(Particle.BackwardMapJacobian.X) && FMath::IsFinite(Particle.BackwardMapJacobian.Y)
			&& FMath::IsFinite(Particle.BackwardMapJacobian.Z) && FMath::IsFinite(Particle.BackwardMapJacobian.W);
	}

	bool ValidateInput(const FFlowMapGpuParticleInput& Input, const FFlowMapGpuParticleSettings& Settings)
	{
		const int32 Nx = Input.CellCounts.X;
		const int32 Ny = Input.CellCounts.Y;
		if (Nx < 2 || Ny < 2 || !FMath::IsFinite(Input.CellSizeCm) || Input.CellSizeCm <= 0.0f
			|| !FMath::IsFinite(Input.FixedDeltaSeconds) || Input.FixedDeltaSeconds <= 0.0f
			|| !FMath::IsFinite(Input.OriginCm.X) || !FMath::IsFinite(Input.OriginCm.Y)
			|| Settings.ReadbackIntervalSteps < 1 || Input.Particles.IsEmpty()
			|| Input.FaceU.Num() != (Nx + 1) * Ny || Input.FaceV.Num() != Nx * (Ny + 1))
		{
			return false;
		}
		for (float Value : Input.FaceU) if (!FMath::IsFinite(Value)) return false;
		for (float Value : Input.FaceV) if (!FMath::IsFinite(Value)) return false;
		for (const FFlowMapGpuParticleState& Particle : Input.Particles) if (!IsFiniteParticle(Particle)) return false;
		return true;
	}

	template <typename ElementType>
	FRDGBufferRef CreateUploadedBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, const TArray<ElementType>& Values, bool bSourceCopy = false)
	{
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(ElementType), Values.Num());
		if (bSourceCopy) Desc.Usage = static_cast<EBufferUsageFlags>(Desc.Usage | BUF_SourceCopy);
		FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, Name);
		GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(ElementType));
		return Buffer;
	}

	FRDGBufferRef CreateStateWorkBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, int32 Count)
	{
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), Count);
		Desc.Usage = static_cast<EBufferUsageFlags>(Desc.Usage | BUF_SourceCopy);
		return GraphBuilder.CreateBuffer(Desc, Name);
	}

	void ConsumeReadyReadback_RenderThread(const TSharedRef<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe>& State)
	{
		if (!State->PendingReadbackA || !State->PendingReadbackB
			|| !State->PendingReadbackA->IsReady() || !State->PendingReadbackB->IsReady()) return;
		const uint32 Bytes = State->Input.Particles.Num() * sizeof(FVector4f);
		const FVector4f* ValuesA = static_cast<const FVector4f*>(State->PendingReadbackA->Lock(Bytes));
		const FVector4f* ValuesB = static_cast<const FVector4f*>(State->PendingReadbackB->Lock(Bytes));
		if (ValuesA && ValuesB)
		{
			FScopeLock Lock(&State->DataLock);
			State->LatestParticles.SetNumUninitialized(State->Input.Particles.Num());
			for (int32 Index = 0; Index < State->LatestParticles.Num(); ++Index)
			{
				State->LatestParticles[Index].PositionCm = FVector2f(ValuesA[Index].X, ValuesA[Index].Y);
				State->LatestParticles[Index].BackwardMapJacobian = FVector4f(ValuesA[Index].Z, ValuesA[Index].W, ValuesB[Index].X, ValuesB[Index].Y);
			}
			State->Stats.ReadbacksCompleted++;
			State->Stats.LatestReadbackStep = State->PendingReadbackStep;
			State->Stats.bReadbackPending = false;
			State->bLatestParticlesAvailable = true;
		}
		State->PendingReadbackA->Unlock(); State->PendingReadbackB->Unlock();
		State->PendingReadbackA.Reset(); State->PendingReadbackB.Reset();
		State->PendingReadbackStep = 0;
	}
}

FFlowMapGpuParticleSession::FFlowMapGpuParticleSession() = default;
FFlowMapGpuParticleSession::~FFlowMapGpuParticleSession() { Shutdown(); }

bool FFlowMapGpuParticleSession::Initialize(const FFlowMapGpuParticleInput& Input, const FFlowMapGpuParticleSettings& Settings)
{
	if (!IsInGameThread() || !GDynamicRHI || !FlowMapGpuParticles::ValidateInput(Input, Settings)) return false;
	Shutdown();
	State = MakeShared<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe>();
	State->Input = Input;
	State->Settings = Settings;
	State->InitialStateA.Reserve(Input.Particles.Num());
	State->InitialStateB.Reserve(Input.Particles.Num());
	for (const FFlowMapGpuParticleState& Particle : Input.Particles)
	{
		State->InitialStateA.Emplace(Particle.PositionCm.X, Particle.PositionCm.Y, Particle.BackwardMapJacobian.X, Particle.BackwardMapJacobian.Y);
		State->InitialStateB.Emplace(Particle.BackwardMapJacobian.Z, Particle.BackwardMapJacobian.W, 0.0f, 0.0f);
	}
	const TSharedRef<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticleInitialize)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGBufferRef FaceU = FlowMapGpuParticles::CreateUploadedBuffer(GraphBuilder, TEXT("FlowMap.Particles.FaceU"), StateRef->Input.FaceU);
		FRDGBufferRef FaceV = FlowMapGpuParticles::CreateUploadedBuffer(GraphBuilder, TEXT("FlowMap.Particles.FaceV"), StateRef->Input.FaceV);
		FRDGBufferRef CurrentA = FlowMapGpuParticles::CreateUploadedBuffer(GraphBuilder, TEXT("FlowMap.Particles.CurrentA"), StateRef->InitialStateA, true);
		FRDGBufferRef CurrentB = FlowMapGpuParticles::CreateUploadedBuffer(GraphBuilder, TEXT("FlowMap.Particles.CurrentB"), StateRef->InitialStateB, true);
		FRDGBufferRef WorkA = FlowMapGpuParticles::CreateStateWorkBuffer(GraphBuilder, TEXT("FlowMap.Particles.WorkA"), StateRef->Input.Particles.Num());
		FRDGBufferRef WorkB = FlowMapGpuParticles::CreateStateWorkBuffer(GraphBuilder, TEXT("FlowMap.Particles.WorkB"), StateRef->Input.Particles.Num());
		StateRef->FaceU = GraphBuilder.ConvertToExternalBuffer(FaceU);
		StateRef->FaceV = GraphBuilder.ConvertToExternalBuffer(FaceV);
		StateRef->CurrentStateA = GraphBuilder.ConvertToExternalBuffer(CurrentA);
		StateRef->CurrentStateB = GraphBuilder.ConvertToExternalBuffer(CurrentB);
		StateRef->WorkStateA = GraphBuilder.ConvertToExternalBuffer(WorkA);
		StateRef->WorkStateB = GraphBuilder.ConvertToExternalBuffer(WorkB);
		GraphBuilder.Execute();
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->Stats.bInitialized = true;
	});
	return true;
}

bool FFlowMapGpuParticleSession::EnqueueAdvectionStep()
{
	if (!IsInGameThread() || !State.IsValid()) return false;
	const TSharedRef<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	int64 StepToExecute = 0;
	{
		FScopeLock Lock(&StateRef->DataLock);
		if (StateRef->bShutdown) return false;
		StepToExecute = ++StateRef->Stats.StepsEnqueued;
	}
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticleStep)([StateRef, StepToExecute](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown || !StateRef->CurrentStateA) return;
		FlowMapGpuParticles::ConsumeReadyReadback_RenderThread(StateRef);
		FRDGBuilder GraphBuilder(RHICmdList);
		auto Register = [&GraphBuilder](const TRefCountPtr<FRDGPooledBuffer>& Buffer, const TCHAR* Name) { return GraphBuilder.RegisterExternalBuffer(Buffer, Name); };
		FRDGBufferRef FaceU = Register(StateRef->FaceU, TEXT("FlowMap.Particles.FaceU"));
		FRDGBufferRef FaceV = Register(StateRef->FaceV, TEXT("FlowMap.Particles.FaceV"));
		FRDGBufferRef CurrentA = Register(StateRef->CurrentStateA, TEXT("FlowMap.Particles.CurrentA"));
		FRDGBufferRef CurrentB = Register(StateRef->CurrentStateB, TEXT("FlowMap.Particles.CurrentB"));
		FRDGBufferRef WorkA = Register(StateRef->WorkStateA, TEXT("FlowMap.Particles.WorkA"));
		FRDGBufferRef WorkB = Register(StateRef->WorkStateB, TEXT("FlowMap.Particles.WorkB"));
		TShaderMapRef<FFlowMapParticleAdvectCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* Parameters = GraphBuilder.AllocParameters<FFlowMapParticleAdvectCS::FParameters>();
		Parameters->CellCountX = StateRef->Input.CellCounts.X;
		Parameters->CellCountY = StateRef->Input.CellCounts.Y;
		Parameters->ParticleCount = StateRef->Input.Particles.Num();
		Parameters->CellSizeCm = StateRef->Input.CellSizeCm;
		Parameters->OriginCm = StateRef->Input.OriginCm;
		Parameters->FixedDeltaSeconds = StateRef->Input.FixedDeltaSeconds;
		Parameters->ParticleFaceU = GraphBuilder.CreateSRV(FaceU);
		Parameters->ParticleFaceV = GraphBuilder.CreateSRV(FaceV);
		Parameters->InputParticleStateA = GraphBuilder.CreateSRV(CurrentA);
		Parameters->InputParticleStateB = GraphBuilder.CreateSRV(CurrentB);
		Parameters->OutputParticleStateA = GraphBuilder.CreateUAV(WorkA);
		Parameters->OutputParticleStateB = GraphBuilder.CreateUAV(WorkB);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Particle Advect %lld", StepToExecute),
			Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.Particles.Num(), 64), 1, 1));
		if (!StateRef->PendingReadbackA && !StateRef->PendingReadbackB && StepToExecute % StateRef->Settings.ReadbackIntervalSteps == 0)
		{
			StateRef->PendingReadbackA = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Particles.AsyncReadbackA"));
			StateRef->PendingReadbackB = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Particles.AsyncReadbackB"));
			StateRef->PendingReadbackStep = StepToExecute;
			const uint32 Bytes = StateRef->Input.Particles.Num() * sizeof(FVector4f);
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackA.Get(), WorkA, Bytes);
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackB.Get(), WorkB, Bytes);
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.bReadbackPending = true;
		}
		GraphBuilder.Execute();
		Swap(StateRef->CurrentStateA, StateRef->WorkStateA);
		Swap(StateRef->CurrentStateB, StateRef->WorkStateB);
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->Stats.StepsExecuted = StepToExecute;
		StateRef->Stats.AdvectionPassesExecuted++;
	});
	return true;
}

void FFlowMapGpuParticleSession::RequestReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticleRequestReadback)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown || !StateRef->CurrentStateA) return;
		FlowMapGpuParticles::ConsumeReadyReadback_RenderThread(StateRef);
		if (StateRef->PendingReadbackA || StateRef->PendingReadbackB) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGBufferRef CurrentA = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentStateA, TEXT("FlowMap.Particles.ReadbackA"));
		FRDGBufferRef CurrentB = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentStateB, TEXT("FlowMap.Particles.ReadbackB"));
		StateRef->PendingReadbackA = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Particles.RequestedReadbackA"));
		StateRef->PendingReadbackB = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Particles.RequestedReadbackB"));
		{
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->PendingReadbackStep = StateRef->Stats.StepsExecuted;
			StateRef->Stats.bReadbackPending = true;
		}
		const uint32 Bytes = StateRef->Input.Particles.Num() * sizeof(FVector4f);
		AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackA.Get(), CurrentA, Bytes);
		AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackB.Get(), CurrentB, Bytes);
		GraphBuilder.Execute();
	});
}

void FFlowMapGpuParticleSession::PollReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticlePollReadback)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (!StateRef->bShutdown) FlowMapGpuParticles::ConsumeReadyReadback_RenderThread(StateRef);
	});
}

bool FFlowMapGpuParticleSession::ConsumeLatestParticles(TArray<FFlowMapGpuParticleState>& OutParticles, int64& OutStepIndex)
{
	if (!State.IsValid()) return false;
	FScopeLock Lock(&State->DataLock);
	if (!State->bLatestParticlesAvailable) return false;
	OutParticles = State->LatestParticles;
	OutStepIndex = State->Stats.LatestReadbackStep;
	State->bLatestParticlesAvailable = false;
	return true;
}

FFlowMapGpuParticleStats FFlowMapGpuParticleSession::GetStats() const
{
	if (!State.IsValid()) return FFlowMapGpuParticleStats();
	FScopeLock Lock(&State->DataLock);
	return State->Stats;
}

void FFlowMapGpuParticleSession::FlushAndWaitForReadbackForValidation()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticleValidationIdle)([](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	RequestReadback();
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticleValidationReadbackIdle)([](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	PollReadback();
	FlushRenderingCommands();
}

void FFlowMapGpuParticleSession::Shutdown()
{
	if (!State.IsValid()) return;
	const TSharedRef<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	{
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->bShutdown = true;
	}
	ENQUEUE_RENDER_COMMAND(FlowMapGpuParticleShutdown)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		StateRef->PendingReadbackA.Reset(); StateRef->PendingReadbackB.Reset();
		StateRef->FaceU.SafeRelease(); StateRef->FaceV.SafeRelease();
		StateRef->CurrentStateA.SafeRelease(); StateRef->CurrentStateB.SafeRelease();
		StateRef->WorkStateA.SafeRelease(); StateRef->WorkStateB.SafeRelease();
	});
	State.Reset();
}
