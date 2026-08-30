#include "FlowMapGpuPressure.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FFlowMapDivergenceCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapDivergenceCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX)
		SHADER_PARAMETER(uint32, CellCountY)
		SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputFaceU)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputFaceV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputDivergence)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapDivergenceCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "DivergenceCS", SF_Compute);

class FFlowMapPressureJacobiCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapPressureJacobiCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapPressureJacobiCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX)
		SHADER_PARAMETER(uint32, CellCountY)
		SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputDivergence)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputPressure)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputPressure)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapPressureJacobiCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "JacobiCS", SF_Compute);

class FFlowMapPressureApplyUCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapPressureApplyUCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapPressureApplyUCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX)
		SHADER_PARAMETER(uint32, CellCountY)
		SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ApplyInputPressure)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ApplyInputFaceU)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ApplyOutputFaceU)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapPressureApplyUCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "ApplyUCS", SF_Compute);

class FFlowMapPressureApplyVCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapPressureApplyVCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapPressureApplyVCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX)
		SHADER_PARAMETER(uint32, CellCountY)
		SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ApplyInputPressure)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ApplyInputFaceV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ApplyOutputFaceV)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapPressureApplyVCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "ApplyVCS", SF_Compute);

namespace
{
	struct FDispatchState
	{
		FFlowMapGpuPressureInput Input;
		FFlowMapGpuPressureResult Result;
	};
}

FFlowMapGpuPressureResult RunFlowMapGpuPressureForValidation(const FFlowMapGpuPressureInput& Input)
{
	FFlowMapGpuPressureResult Invalid;
	const int32 Nx = Input.CellCounts.X;
	const int32 Ny = Input.CellCounts.Y;
	if (Nx < 2 || Ny < 2 || Input.CellSizeCm <= 0.0f || Input.JacobiIterations < 1
		|| Input.FaceU.Num() != (Nx + 1) * Ny || Input.FaceV.Num() != Nx * (Ny + 1))
	{
		Invalid.FailureReason = TEXT("Invalid Flow Map GPU pressure input contract");
		return Invalid;
	}
	const TSharedRef<FDispatchState, ESPMode::ThreadSafe> State = MakeShared<FDispatchState, ESPMode::ThreadSafe>();
	State->Input = Input;
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureValidation)([State](FRHICommandListImmediate& RHICmdList)
	{
		const int32 LocalNx = State->Input.CellCounts.X;
		const int32 LocalNy = State->Input.CellCounts.Y;
		const int32 CellCount = LocalNx * LocalNy;
		FRDGBuilder GraphBuilder(RHICmdList);
		auto Upload = [&GraphBuilder](const TCHAR* Name, TConstArrayView<float> Values)
		{
			FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), Values.Num()), Name);
			GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(float));
			return Buffer;
		};
		FRDGBufferRef FaceU = Upload(TEXT("FlowMap.Pressure.InputU"), State->Input.FaceU);
		FRDGBufferRef FaceV = Upload(TEXT("FlowMap.Pressure.InputV"), State->Input.FaceV);
		FRDGBufferRef Divergence = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), CellCount), TEXT("FlowMap.Pressure.Divergence"));
		FRDGBufferRef PressureA = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), CellCount), TEXT("FlowMap.Pressure.A"));
		FRDGBufferRef PressureB = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), CellCount), TEXT("FlowMap.Pressure.B"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureA), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureB), 0u);
		TShaderMapRef<FFlowMapDivergenceCS> DivergenceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FFlowMapDivergenceCS::FParameters* DivergenceParameters = GraphBuilder.AllocParameters<FFlowMapDivergenceCS::FParameters>();
		DivergenceParameters->CellCountX = LocalNx;
		DivergenceParameters->CellCountY = LocalNy;
		DivergenceParameters->CellSizeCm = State->Input.CellSizeCm;
		DivergenceParameters->InputFaceU = GraphBuilder.CreateSRV(FaceU);
		DivergenceParameters->InputFaceV = GraphBuilder.CreateSRV(FaceV);
		DivergenceParameters->OutputDivergence = GraphBuilder.CreateUAV(Divergence);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Divergence"), DivergenceShader, DivergenceParameters, FIntVector(FMath::DivideAndRoundUp(CellCount, 64), 1, 1));
		State->Result.DivergencePasses = 1;
		FRDGBufferRef Current = PressureA;
		FRDGBufferRef Next = PressureB;
		TShaderMapRef<FFlowMapPressureJacobiCS> JacobiShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		for (int32 Iteration = 0; Iteration < State->Input.JacobiIterations; ++Iteration)
		{
			FFlowMapPressureJacobiCS::FParameters* Parameters = GraphBuilder.AllocParameters<FFlowMapPressureJacobiCS::FParameters>();
			Parameters->CellCountX = LocalNx;
			Parameters->CellCountY = LocalNy;
			Parameters->CellSizeCm = State->Input.CellSizeCm;
			Parameters->InputDivergence = GraphBuilder.CreateSRV(Divergence);
			Parameters->InputPressure = GraphBuilder.CreateSRV(Current);
			Parameters->OutputPressure = GraphBuilder.CreateUAV(Next);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Jacobi %d", Iteration), JacobiShader, Parameters, FIntVector(FMath::DivideAndRoundUp(CellCount, 64), 1, 1));
			Swap(Current, Next);
		}
		State->Result.JacobiPasses = State->Input.JacobiIterations;
		FRDGBufferDesc OutputUDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), State->Input.FaceU.Num());
		OutputUDesc.Usage = static_cast<EBufferUsageFlags>(OutputUDesc.Usage | BUF_SourceCopy);
		FRDGBufferDesc OutputVDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), State->Input.FaceV.Num());
		OutputVDesc.Usage = static_cast<EBufferUsageFlags>(OutputVDesc.Usage | BUF_SourceCopy);
		FRDGBufferRef OutputU = GraphBuilder.CreateBuffer(OutputUDesc, TEXT("FlowMap.Pressure.OutputU"));
		FRDGBufferRef OutputV = GraphBuilder.CreateBuffer(OutputVDesc, TEXT("FlowMap.Pressure.OutputV"));
		TShaderMapRef<FFlowMapPressureApplyUCS> ApplyUShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FFlowMapPressureApplyUCS::FParameters* ApplyU = GraphBuilder.AllocParameters<FFlowMapPressureApplyUCS::FParameters>();
		ApplyU->CellCountX = LocalNx; ApplyU->CellCountY = LocalNy; ApplyU->CellSizeCm = State->Input.CellSizeCm;
		ApplyU->ApplyInputPressure = GraphBuilder.CreateSRV(Current); ApplyU->ApplyInputFaceU = GraphBuilder.CreateSRV(FaceU); ApplyU->ApplyOutputFaceU = GraphBuilder.CreateUAV(OutputU);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Apply U"), ApplyUShader, ApplyU, FIntVector(FMath::DivideAndRoundUp(State->Input.FaceU.Num(), 64), 1, 1));
		TShaderMapRef<FFlowMapPressureApplyVCS> ApplyVShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FFlowMapPressureApplyVCS::FParameters* ApplyV = GraphBuilder.AllocParameters<FFlowMapPressureApplyVCS::FParameters>();
		ApplyV->CellCountX = LocalNx; ApplyV->CellCountY = LocalNy; ApplyV->CellSizeCm = State->Input.CellSizeCm;
		ApplyV->ApplyInputPressure = GraphBuilder.CreateSRV(Current); ApplyV->ApplyInputFaceV = GraphBuilder.CreateSRV(FaceV); ApplyV->ApplyOutputFaceV = GraphBuilder.CreateUAV(OutputV);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Apply V"), ApplyVShader, ApplyV, FIntVector(FMath::DivideAndRoundUp(State->Input.FaceV.Num(), 64), 1, 1));
		State->Result.ApplyPasses = 2;
		FRHIGPUBufferReadback ReadbackU(TEXT("FlowMap.Pressure.ReadbackU"));
		FRHIGPUBufferReadback ReadbackV(TEXT("FlowMap.Pressure.ReadbackV"));
		const uint32 BytesU = State->Input.FaceU.Num() * sizeof(float);
		const uint32 BytesV = State->Input.FaceV.Num() * sizeof(float);
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
				State->Result.FaceU.SetNumUninitialized(State->Input.FaceU.Num());
				State->Result.FaceV.SetNumUninitialized(State->Input.FaceV.Num());
				FMemory::Memcpy(State->Result.FaceU.GetData(), DataU, BytesU);
				FMemory::Memcpy(State->Result.FaceV.GetData(), DataV, BytesV);
				State->Result.bReadbackSucceeded = true;
			}
			ReadbackU.Unlock(); ReadbackV.Unlock();
		}
		else { State->Result.FailureReason = TEXT("Flow Map GPU pressure readback was not ready"); }
	});
	FlushRenderingCommands();
	return MoveTemp(State->Result);
}

class FFlowMapGpuPressureSessionState
{
public:
	mutable FCriticalSection DataLock;
	FFlowMapGpuPressureInput Input;
	FFlowMapGpuPressureSessionSettings Settings;
	FFlowMapGpuPressureSessionStats Stats;
	TRefCountPtr<FRDGPooledBuffer> CurrentFaceU;
	TRefCountPtr<FRDGPooledBuffer> CurrentFaceV;
	TRefCountPtr<FRDGPooledBuffer> WorkFaceU;
	TRefCountPtr<FRDGPooledBuffer> WorkFaceV;
	TRefCountPtr<FRDGPooledBuffer> Divergence;
	TRefCountPtr<FRDGPooledBuffer> PressureA;
	TRefCountPtr<FRDGPooledBuffer> PressureB;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadbackU;
	TUniquePtr<FRHIGPUBufferReadback> PendingReadbackV;
	TArray<float> LatestFaceU;
	TArray<float> LatestFaceV;
	int64 PendingReadbackStep = 0;
	bool bLatestVelocityAvailable = false;
	bool bShutdown = false;
};

namespace FlowMapGpuPressureSession
{
	bool ValidateInput(const FFlowMapGpuPressureInput& Input, const FFlowMapGpuPressureSessionSettings& Settings)
	{
		const int32 Nx = Input.CellCounts.X;
		const int32 Ny = Input.CellCounts.Y;
		return Nx >= 2 && Ny >= 2 && FMath::IsFinite(Input.CellSizeCm) && Input.CellSizeCm > 0.0f
			&& Settings.JacobiIterationsPerStep >= 1 && Settings.ReadbackIntervalSteps >= 1
			&& Input.FaceU.Num() == (Nx + 1) * Ny && Input.FaceV.Num() == Nx * (Ny + 1);
	}

	void ConsumeReadyReadback_RenderThread(const TSharedRef<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe>& State)
	{
		if (!State->PendingReadbackU || !State->PendingReadbackV
			|| !State->PendingReadbackU->IsReady() || !State->PendingReadbackV->IsReady())
		{
			return;
		}
		const uint32 BytesU = State->Input.FaceU.Num() * sizeof(float);
		const uint32 BytesV = State->Input.FaceV.Num() * sizeof(float);
		const float* ValuesU = static_cast<const float*>(State->PendingReadbackU->Lock(BytesU));
		const float* ValuesV = static_cast<const float*>(State->PendingReadbackV->Lock(BytesV));
		if (ValuesU && ValuesV)
		{
			FScopeLock Lock(&State->DataLock);
			State->LatestFaceU.SetNumUninitialized(State->Input.FaceU.Num());
			State->LatestFaceV.SetNumUninitialized(State->Input.FaceV.Num());
			FMemory::Memcpy(State->LatestFaceU.GetData(), ValuesU, BytesU);
			FMemory::Memcpy(State->LatestFaceV.GetData(), ValuesV, BytesV);
			State->Stats.ReadbacksCompleted++;
			State->Stats.LatestReadbackStep = State->PendingReadbackStep;
			State->Stats.bReadbackPending = false;
			State->bLatestVelocityAvailable = true;
		}
		State->PendingReadbackU->Unlock();
		State->PendingReadbackV->Unlock();
		State->PendingReadbackU.Reset();
		State->PendingReadbackV.Reset();
		State->PendingReadbackStep = 0;
	}
}

FFlowMapGpuPressureSession::FFlowMapGpuPressureSession() = default;
FFlowMapGpuPressureSession::~FFlowMapGpuPressureSession() { Shutdown(); }

bool FFlowMapGpuPressureSession::Initialize(
	const FFlowMapGpuPressureInput& Input,
	const FFlowMapGpuPressureSessionSettings& Settings)
{
	if (!IsInGameThread() || !GDynamicRHI || !FlowMapGpuPressureSession::ValidateInput(Input, Settings)) return false;
	Shutdown();
	State = MakeShared<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe>();
	State->Input = Input;
	State->Settings = Settings;
	const TSharedRef<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionInitialize)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		auto CreateFaceBuffer = [&GraphBuilder](const TCHAR* Name, TConstArrayView<float> Values)
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), Values.Num());
			Desc.Usage = static_cast<EBufferUsageFlags>(Desc.Usage | BUF_SourceCopy);
			FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, Name);
			GraphBuilder.QueueBufferUpload(Buffer, Values.GetData(), Values.Num() * sizeof(float));
			return Buffer;
		};
		auto CreateFaceWorkBuffer = [&GraphBuilder](const TCHAR* Name, int32 Count)
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), Count);
			Desc.Usage = static_cast<EBufferUsageFlags>(Desc.Usage | BUF_SourceCopy);
			return GraphBuilder.CreateBuffer(Desc, Name);
		};
		FRDGBufferRef CurrentU = CreateFaceBuffer(TEXT("FlowMap.PressureSession.CurrentU"), StateRef->Input.FaceU);
		FRDGBufferRef CurrentV = CreateFaceBuffer(TEXT("FlowMap.PressureSession.CurrentV"), StateRef->Input.FaceV);
		FRDGBufferRef WorkU = CreateFaceWorkBuffer(TEXT("FlowMap.PressureSession.WorkU"), StateRef->Input.FaceU.Num());
		FRDGBufferRef WorkV = CreateFaceWorkBuffer(TEXT("FlowMap.PressureSession.WorkV"), StateRef->Input.FaceV.Num());
		const int32 CellCount = StateRef->Input.CellCounts.X * StateRef->Input.CellCounts.Y;
		FRDGBufferRef Divergence = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), CellCount), TEXT("FlowMap.PressureSession.Divergence"));
		FRDGBufferRef PressureA = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), CellCount), TEXT("FlowMap.PressureSession.PressureA"));
		FRDGBufferRef PressureB = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), CellCount), TEXT("FlowMap.PressureSession.PressureB"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureA), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureB), 0u);
		StateRef->CurrentFaceU = GraphBuilder.ConvertToExternalBuffer(CurrentU);
		StateRef->CurrentFaceV = GraphBuilder.ConvertToExternalBuffer(CurrentV);
		StateRef->WorkFaceU = GraphBuilder.ConvertToExternalBuffer(WorkU);
		StateRef->WorkFaceV = GraphBuilder.ConvertToExternalBuffer(WorkV);
		StateRef->Divergence = GraphBuilder.ConvertToExternalBuffer(Divergence);
		StateRef->PressureA = GraphBuilder.ConvertToExternalBuffer(PressureA);
		StateRef->PressureB = GraphBuilder.ConvertToExternalBuffer(PressureB);
		GraphBuilder.Execute();
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->Stats.bInitialized = true;
	});
	return true;
}

bool FFlowMapGpuPressureSession::EnqueueProjectionStep()
{
	if (!IsInGameThread() || !State.IsValid()) return false;
	const TSharedRef<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	int64 StepToExecute = 0;
	{
		FScopeLock Lock(&StateRef->DataLock);
		if (StateRef->bShutdown) return false;
		StepToExecute = ++StateRef->Stats.StepsEnqueued;
	}
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionStep)([StateRef, StepToExecute](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown || !StateRef->CurrentFaceU || !StateRef->CurrentFaceV) return;
		FlowMapGpuPressureSession::ConsumeReadyReadback_RenderThread(StateRef);
		FRDGBuilder GraphBuilder(RHICmdList);
		auto Register = [&GraphBuilder](const TRefCountPtr<FRDGPooledBuffer>& Buffer, const TCHAR* Name)
		{
			return GraphBuilder.RegisterExternalBuffer(Buffer, Name);
		};
		FRDGBufferRef CurrentU = Register(StateRef->CurrentFaceU, TEXT("FlowMap.PressureSession.CurrentU"));
		FRDGBufferRef CurrentV = Register(StateRef->CurrentFaceV, TEXT("FlowMap.PressureSession.CurrentV"));
		FRDGBufferRef WorkU = Register(StateRef->WorkFaceU, TEXT("FlowMap.PressureSession.WorkU"));
		FRDGBufferRef WorkV = Register(StateRef->WorkFaceV, TEXT("FlowMap.PressureSession.WorkV"));
		FRDGBufferRef Divergence = Register(StateRef->Divergence, TEXT("FlowMap.PressureSession.Divergence"));
		FRDGBufferRef PressureA = Register(StateRef->PressureA, TEXT("FlowMap.PressureSession.PressureA"));
		FRDGBufferRef PressureB = Register(StateRef->PressureB, TEXT("FlowMap.PressureSession.PressureB"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureA), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureB), 0u);
		const int32 Nx = StateRef->Input.CellCounts.X;
		const int32 Ny = StateRef->Input.CellCounts.Y;
		const int32 CellCount = Nx * Ny;
		TShaderMapRef<FFlowMapDivergenceCS> DivergenceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* DivergenceParameters = GraphBuilder.AllocParameters<FFlowMapDivergenceCS::FParameters>();
		DivergenceParameters->CellCountX = Nx;
		DivergenceParameters->CellCountY = Ny;
		DivergenceParameters->CellSizeCm = StateRef->Input.CellSizeCm;
		DivergenceParameters->InputFaceU = GraphBuilder.CreateSRV(CurrentU);
		DivergenceParameters->InputFaceV = GraphBuilder.CreateSRV(CurrentV);
		DivergenceParameters->OutputDivergence = GraphBuilder.CreateUAV(Divergence);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Session Divergence %lld", StepToExecute),
			DivergenceShader, DivergenceParameters, FIntVector(FMath::DivideAndRoundUp(CellCount, 64), 1, 1));
		FRDGBufferRef PressureCurrent = PressureA;
		FRDGBufferRef PressureNext = PressureB;
		TShaderMapRef<FFlowMapPressureJacobiCS> JacobiShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		for (int32 Iteration = 0; Iteration < StateRef->Settings.JacobiIterationsPerStep; ++Iteration)
		{
			auto* Parameters = GraphBuilder.AllocParameters<FFlowMapPressureJacobiCS::FParameters>();
			Parameters->CellCountX = Nx;
			Parameters->CellCountY = Ny;
			Parameters->CellSizeCm = StateRef->Input.CellSizeCm;
			Parameters->InputDivergence = GraphBuilder.CreateSRV(Divergence);
			Parameters->InputPressure = GraphBuilder.CreateSRV(PressureCurrent);
			Parameters->OutputPressure = GraphBuilder.CreateUAV(PressureNext);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Session Jacobi %d", Iteration),
				JacobiShader, Parameters, FIntVector(FMath::DivideAndRoundUp(CellCount, 64), 1, 1));
			Swap(PressureCurrent, PressureNext);
		}
		TShaderMapRef<FFlowMapPressureApplyUCS> ApplyUShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* ApplyU = GraphBuilder.AllocParameters<FFlowMapPressureApplyUCS::FParameters>();
		ApplyU->CellCountX = Nx; ApplyU->CellCountY = Ny; ApplyU->CellSizeCm = StateRef->Input.CellSizeCm;
		ApplyU->ApplyInputPressure = GraphBuilder.CreateSRV(PressureCurrent);
		ApplyU->ApplyInputFaceU = GraphBuilder.CreateSRV(CurrentU);
		ApplyU->ApplyOutputFaceU = GraphBuilder.CreateUAV(WorkU);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Session Apply U"),
			ApplyUShader, ApplyU, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.FaceU.Num(), 64), 1, 1));
		TShaderMapRef<FFlowMapPressureApplyVCS> ApplyVShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* ApplyV = GraphBuilder.AllocParameters<FFlowMapPressureApplyVCS::FParameters>();
		ApplyV->CellCountX = Nx; ApplyV->CellCountY = Ny; ApplyV->CellSizeCm = StateRef->Input.CellSizeCm;
		ApplyV->ApplyInputPressure = GraphBuilder.CreateSRV(PressureCurrent);
		ApplyV->ApplyInputFaceV = GraphBuilder.CreateSRV(CurrentV);
		ApplyV->ApplyOutputFaceV = GraphBuilder.CreateUAV(WorkV);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FlowMap Pressure Session Apply V"),
			ApplyVShader, ApplyV, FIntVector(FMath::DivideAndRoundUp(StateRef->Input.FaceV.Num(), 64), 1, 1));
		if (!StateRef->PendingReadbackU && !StateRef->PendingReadbackV
			&& StepToExecute % StateRef->Settings.ReadbackIntervalSteps == 0)
		{
			StateRef->PendingReadbackU = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.PressureSession.AsyncReadbackU"));
			StateRef->PendingReadbackV = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.PressureSession.AsyncReadbackV"));
			StateRef->PendingReadbackStep = StepToExecute;
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackU.Get(), WorkU, StateRef->Input.FaceU.Num() * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackV.Get(), WorkV, StateRef->Input.FaceV.Num() * sizeof(float));
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->Stats.bReadbackPending = true;
		}
		GraphBuilder.Execute();
		Swap(StateRef->CurrentFaceU, StateRef->WorkFaceU);
		Swap(StateRef->CurrentFaceV, StateRef->WorkFaceV);
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->Stats.StepsExecuted = StepToExecute;
		StateRef->Stats.DivergencePassesExecuted++;
		StateRef->Stats.JacobiPassesExecuted += StateRef->Settings.JacobiIterationsPerStep;
		StateRef->Stats.ApplyPassesExecuted += 2;
	});
	return true;
}

void FFlowMapGpuPressureSession::RequestReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionRequestReadback)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (StateRef->bShutdown || !StateRef->CurrentFaceU || !StateRef->CurrentFaceV) return;
		FlowMapGpuPressureSession::ConsumeReadyReadback_RenderThread(StateRef);
		if (StateRef->PendingReadbackU || StateRef->PendingReadbackV) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGBufferRef CurrentU = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentFaceU, TEXT("FlowMap.PressureSession.ReadbackU"));
		FRDGBufferRef CurrentV = GraphBuilder.RegisterExternalBuffer(StateRef->CurrentFaceV, TEXT("FlowMap.PressureSession.ReadbackV"));
		StateRef->PendingReadbackU = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.PressureSession.RequestedReadbackU"));
		StateRef->PendingReadbackV = MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.PressureSession.RequestedReadbackV"));
		{
			FScopeLock Lock(&StateRef->DataLock);
			StateRef->PendingReadbackStep = StateRef->Stats.StepsExecuted;
			StateRef->Stats.bReadbackPending = true;
		}
		AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackU.Get(), CurrentU, StateRef->Input.FaceU.Num() * sizeof(float));
		AddEnqueueCopyPass(GraphBuilder, StateRef->PendingReadbackV.Get(), CurrentV, StateRef->Input.FaceV.Num() * sizeof(float));
		GraphBuilder.Execute();
	});
}

void FFlowMapGpuPressureSession::PollReadback()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	const TSharedRef<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionPollReadback)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		if (!StateRef->bShutdown) FlowMapGpuPressureSession::ConsumeReadyReadback_RenderThread(StateRef);
	});
}

bool FFlowMapGpuPressureSession::ConsumeLatestVelocity(
	TArray<float>& OutFaceU,
	TArray<float>& OutFaceV,
	int64& OutStepIndex)
{
	if (!State.IsValid()) return false;
	FScopeLock Lock(&State->DataLock);
	if (!State->bLatestVelocityAvailable) return false;
	OutFaceU = State->LatestFaceU;
	OutFaceV = State->LatestFaceV;
	OutStepIndex = State->Stats.LatestReadbackStep;
	State->bLatestVelocityAvailable = false;
	return true;
}

FFlowMapGpuPressureSessionStats FFlowMapGpuPressureSession::GetStats() const
{
	if (!State.IsValid()) return FFlowMapGpuPressureSessionStats();
	FScopeLock Lock(&State->DataLock);
	return State->Stats;
}

void FFlowMapGpuPressureSession::FlushAndWaitForReadbackForValidation()
{
	if (!IsInGameThread() || !State.IsValid()) return;
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionValidationIdle)([](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	RequestReadback();
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionValidationReadbackIdle)([](FRHICommandListImmediate& RHICmdList) { RHICmdList.BlockUntilGPUIdle(); });
	FlushRenderingCommands();
	PollReadback();
	FlushRenderingCommands();
}

void FFlowMapGpuPressureSession::Shutdown()
{
	if (!State.IsValid()) return;
	const TSharedRef<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
	{
		FScopeLock Lock(&StateRef->DataLock);
		StateRef->bShutdown = true;
	}
	ENQUEUE_RENDER_COMMAND(FlowMapGpuPressureSessionShutdown)([StateRef](FRHICommandListImmediate& RHICmdList)
	{
		StateRef->PendingReadbackU.Reset(); StateRef->PendingReadbackV.Reset();
		StateRef->CurrentFaceU.SafeRelease(); StateRef->CurrentFaceV.SafeRelease();
		StateRef->WorkFaceU.SafeRelease(); StateRef->WorkFaceV.SafeRelease();
		StateRef->Divergence.SafeRelease(); StateRef->PressureA.SafeRelease(); StateRef->PressureB.SafeRelease();
	});
	State.Reset();
}
