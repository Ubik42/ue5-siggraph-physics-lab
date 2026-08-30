#include "FlowMapGpuClosedLoop.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FFlowMapClosedLoopAdvectCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopAdvectCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopAdvectCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX) SHADER_PARAMETER(uint32, CellCountY) SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(uint32, ParticleSolidCircleCount)
		SHADER_PARAMETER(float, CellSizeCm) SHADER_PARAMETER(FVector2f, OriginCm) SHADER_PARAMETER(float, FixedDeltaSeconds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ParticleFaceU)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ParticleFaceV)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InputParticleStateA)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InputParticleStateB)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ParticleSolidCircles)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutputParticleStateA)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutputParticleStateB)
	END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopAdvectCS, "/Plugin/PhysicsResearch/Private/FlowMapParticleAdvection.usf", "AdvectParticlesWithSolidsCS", SF_Compute);

class FFlowMapClosedLoopResetCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopResetCS);
	SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopResetCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX) SHADER_PARAMETER(uint32, CellCountY) SHADER_PARAMETER(uint32, ParticleCount)
		SHADER_PARAMETER(float, CellSizeCm) SHADER_PARAMETER(FVector2f, OriginCm) SHADER_PARAMETER(float, FixedDeltaSeconds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ParticleFaceU)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ParticleFaceV)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ResetInputParticleStateA)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ResetOutputParticleStateA)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ResetOutputParticleStateB)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ResetReferenceImpulseAndGradientRow0)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ResetReferenceGradientRow1)
	END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopResetCS, "/Plugin/PhysicsResearch/Private/FlowMapParticleAdvection.usf", "ResetParticleMapsCS", SF_Compute);

#define CLOSED_LOOP_IMPULSE_COMMON \
	SHADER_PARAMETER(uint32, ImpulseCellCountX) SHADER_PARAMETER(uint32, ImpulseCellCountY) SHADER_PARAMETER(uint32, ImpulseParticleCount) \
	SHADER_PARAMETER(float, ImpulseCellSizeCm) SHADER_PARAMETER(FVector2f, ImpulseOriginCm) SHADER_PARAMETER(uint32, ImpulseUseGradient) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ImpulseParticleStateA) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ImpulseParticleStateB) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceImpulseAndGradientRow0) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceGradientRow1)

class FFlowMapClosedLoopGatherUCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopGatherUCS); SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopGatherUCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) CLOSED_LOOP_IMPULSE_COMMON SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputImpulseFaceU) END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopGatherUCS, "/Plugin/PhysicsResearch/Private/FlowMapImpulseTransfer.usf", "GatherImpulseUCS", SF_Compute);

class FFlowMapClosedLoopGatherVCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopGatherVCS); SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopGatherVCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) CLOSED_LOOP_IMPULSE_COMMON SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputImpulseFaceV) END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopGatherVCS, "/Plugin/PhysicsResearch/Private/FlowMapImpulseTransfer.usf", "GatherImpulseVCS", SF_Compute);
#undef CLOSED_LOOP_IMPULSE_COMMON

class FFlowMapClosedLoopDivergenceCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopDivergenceCS); SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopDivergenceCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX) SHADER_PARAMETER(uint32, CellCountY) SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SolidCellMask) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SolidFaceUMask) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SolidFaceVMask)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidFaceVelocityU) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidFaceVelocityV)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidInputFaceU) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidInputFaceV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SolidOutputDivergence)
	END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopDivergenceCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "SolidDivergenceCS", SF_Compute);

class FFlowMapClosedLoopJacobiCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopJacobiCS); SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopJacobiCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX) SHADER_PARAMETER(uint32, CellCountY) SHADER_PARAMETER(float, CellSizeCm)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SolidCellMask)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidInputDivergence) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidInputPressure)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SolidOutputPressure)
	END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopJacobiCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "SolidJacobiCS", SF_Compute);

class FFlowMapClosedLoopApplyUCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopApplyUCS); SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopApplyUCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX) SHADER_PARAMETER(uint32, CellCountY) SHADER_PARAMETER(float, CellSizeCm) SHADER_PARAMETER(float, MaxFaceVelocityCmPerSecond)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SolidFaceUMask) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidFaceVelocityU)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidApplyInputPressure) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidApplyInputFaceU)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SolidApplyOutputFaceU)
	END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopApplyUCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "SolidApplyUCS", SF_Compute);

class FFlowMapClosedLoopApplyVCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlowMapClosedLoopApplyVCS); SHADER_USE_PARAMETER_STRUCT(FFlowMapClosedLoopApplyVCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CellCountX) SHADER_PARAMETER(uint32, CellCountY) SHADER_PARAMETER(float, CellSizeCm) SHADER_PARAMETER(float, MaxFaceVelocityCmPerSecond)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SolidFaceVMask) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidFaceVelocityV)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidApplyInputPressure) SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SolidApplyInputFaceV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SolidApplyOutputFaceV)
	END_SHADER_PARAMETER_STRUCT()
public: static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FFlowMapClosedLoopApplyVCS, "/Plugin/PhysicsResearch/Private/FlowMapPressureProjection.usf", "SolidApplyVCS", SF_Compute);

class FFlowMapGpuClosedLoopState
{
public:
	struct FGpuTimingSlot
	{
		FRenderQueryRHIRef StartQuery;
		FRenderQueryRHIRef EndQuery;
		int64 Step = 0;
		bool bPending = false;
	};
	mutable FCriticalSection Lock;
	FFlowMapGpuClosedLoopInput Input;
	FFlowMapGpuClosedLoopStats Stats;
	TArray<FVector4f> InitialParticleA, InitialParticleB, Payload0, Payload1;
	FFlowMapGpuSolidBoundaryUpdate SolidBoundary;
	int64 SolidBoundaryVersion = 0;
	TRefCountPtr<FRDGPooledBuffer> FaceCurrentU, FaceCurrentV, FaceWorkU, FaceWorkV;
	TRefCountPtr<FRDGPooledBuffer> ParticleCurrentA, ParticleCurrentB, ParticleWorkA, ParticleWorkB;
	TRefCountPtr<FRDGPooledBuffer> GpuPayload0, GpuPayload1, Divergence, PressureA, PressureB;
	TRefCountPtr<FRDGPooledBuffer> SolidCellMask, SolidFaceUMask, SolidFaceVMask, SolidFaceVelocityU, SolidFaceVelocityV;
	TRefCountPtr<FRDGPooledBuffer> SolidCircles;
	TUniquePtr<FRHIGPUBufferReadback> ReadbackU, ReadbackV, ReadbackPressure, ReadbackParticleA, ReadbackParticleB;
	TArray<FGpuTimingSlot> GpuTimingSlots;
	TArray<int64> GpuTimingSteps;
	TArray<double> GpuStepMilliseconds;
	FFlowMapGpuClosedLoopSnapshot LatestSnapshot;
	int64 PendingReadbackStep = 0;
	bool bSnapshotAvailable = false;
	bool bShutdown = false;
};

namespace FlowMapGpuClosedLoop
{
	bool Validate(const FFlowMapGpuClosedLoopInput& I)
	{
		const int32 Nx = I.CellCounts.X, Ny = I.CellCounts.Y;
		return Nx >= 8 && Ny >= 8 && Nx == Ny && I.CellSizeCm > 0.0f && I.FixedDeltaSeconds > 0.0f
			&& I.PressureIterations > 0 && I.LongMapResetSteps > 0 && I.ReadbackIntervalSteps > 0 && I.GpuTimingWarmupSteps >= 0
			&& FMath::IsFinite(I.MaximumFaceVelocityCmPerSecond) && I.MaximumFaceVelocityCmPerSecond >= 0.0f && !I.Particles.IsEmpty()
			&& I.InitialFaceU.Num() == (Nx + 1) * Ny && I.InitialFaceV.Num() == Nx * (Ny + 1);
	}
	bool ValidateBoundary(const FFlowMapGpuClosedLoopInput& I, const FFlowMapGpuSolidBoundaryUpdate& B)
	{
		const int32 CellCount=I.CellCounts.X*I.CellCounts.Y, FaceUCount=I.InitialFaceU.Num(), FaceVCount=I.InitialFaceV.Num();
		if(B.SolidCellMask.Num()!=CellCount||B.SolidFaceUMask.Num()!=FaceUCount||B.SolidFaceVMask.Num()!=FaceVCount||B.SolidFaceVelocityU.Num()!=FaceUCount||B.SolidFaceVelocityV.Num()!=FaceVCount)return false;
		for(uint32 Value:B.SolidCellMask)if(Value>1u)return false;for(uint32 Value:B.SolidFaceUMask)if(Value>1u)return false;for(uint32 Value:B.SolidFaceVMask)if(Value>1u)return false;
		for(float Value:B.SolidFaceVelocityU)if(!FMath::IsFinite(Value))return false;for(float Value:B.SolidFaceVelocityV)if(!FMath::IsFinite(Value))return false;if(B.ParticleCollisionCircles.Num()>8)return false;for(const FFlowMapGpuSolidBoundaryUpdate::FSolidCircle& Circle:B.ParticleCollisionCircles)if(!FMath::IsFinite(Circle.CenterCm.X)||!FMath::IsFinite(Circle.CenterCm.Y)||!FMath::IsFinite(Circle.RadiusCm)||!FMath::IsFinite(Circle.SafetyOffsetCm)||Circle.RadiusCm<=0.0f||Circle.SafetyOffsetCm<0.0f)return false;return true;
	}
	template <typename T> FRDGBufferRef Upload(FRDGBuilder& G, const TCHAR* Name, const TArray<T>& V, bool Copy = false)
	{
		FRDGBufferDesc D = FRDGBufferDesc::CreateStructuredDesc(sizeof(T), V.Num()); if (Copy) D.Usage = static_cast<EBufferUsageFlags>(D.Usage | BUF_SourceCopy);
		FRDGBufferRef B = G.CreateBuffer(D, Name); G.QueueBufferUpload(B, V.GetData(), V.Num() * sizeof(T)); return B;
	}
	FRDGBufferRef Work(FRDGBuilder& G, const TCHAR* Name, uint32 Bytes, int32 Count, bool Copy = false)
	{
		FRDGBufferDesc D = FRDGBufferDesc::CreateStructuredDesc(Bytes, Count); if (Copy) D.Usage = static_cast<EBufferUsageFlags>(D.Usage | BUF_SourceCopy); return G.CreateBuffer(D, Name);
	}
	void ConsumeReadback(const TSharedRef<FFlowMapGpuClosedLoopState, ESPMode::ThreadSafe>& S)
	{
		if (!S->ReadbackU || !S->ReadbackV || !S->ReadbackPressure || !S->ReadbackParticleA || !S->ReadbackParticleB
			|| !S->ReadbackU->IsReady() || !S->ReadbackV->IsReady() || !S->ReadbackPressure->IsReady() || !S->ReadbackParticleA->IsReady() || !S->ReadbackParticleB->IsReady()) return;
		const uint32 BU = S->Input.InitialFaceU.Num() * sizeof(float), BV = S->Input.InitialFaceV.Num() * sizeof(float), BPressure = S->Input.CellCounts.X*S->Input.CellCounts.Y*sizeof(float), BP = S->Input.Particles.Num() * sizeof(FVector4f);
		const float* U = static_cast<const float*>(S->ReadbackU->Lock(BU)); const float* V = static_cast<const float*>(S->ReadbackV->Lock(BV));
		const float* Pressure = static_cast<const float*>(S->ReadbackPressure->Lock(BPressure));
		const FVector4f* A = static_cast<const FVector4f*>(S->ReadbackParticleA->Lock(BP)); const FVector4f* B = static_cast<const FVector4f*>(S->ReadbackParticleB->Lock(BP));
		if (U && V && Pressure && A && B)
		{
			FScopeLock Guard(&S->Lock); FFlowMapGpuClosedLoopSnapshot& O = S->LatestSnapshot; O.StepIndex = S->PendingReadbackStep;
			O.FaceU.SetNumUninitialized(S->Input.InitialFaceU.Num()); O.FaceV.SetNumUninitialized(S->Input.InitialFaceV.Num());
			FMemory::Memcpy(O.FaceU.GetData(), U, BU); FMemory::Memcpy(O.FaceV.GetData(), V, BV);
			O.PressurePotential.SetNumUninitialized(S->Input.CellCounts.X*S->Input.CellCounts.Y);FMemory::Memcpy(O.PressurePotential.GetData(),Pressure,BPressure);
			O.ParticlePositionsCm.SetNumUninitialized(S->Input.Particles.Num()); O.BackwardMapJacobians.SetNumUninitialized(S->Input.Particles.Num());
			for (int32 I = 0; I < S->Input.Particles.Num(); ++I) { O.ParticlePositionsCm[I] = FVector2f(A[I].X, A[I].Y); O.BackwardMapJacobians[I] = FVector4f(A[I].Z, A[I].W, B[I].X, B[I].Y); }
			S->Stats.ReadbacksCompleted++; S->Stats.LatestReadbackStep = O.StepIndex; S->Stats.bReadbackPending = false; S->bSnapshotAvailable = true;
		}
		S->ReadbackU->Unlock(); S->ReadbackV->Unlock(); S->ReadbackPressure->Unlock(); S->ReadbackParticleA->Unlock(); S->ReadbackParticleB->Unlock();
		S->ReadbackU.Reset(); S->ReadbackV.Reset(); S->ReadbackPressure.Reset(); S->ReadbackParticleA.Reset(); S->ReadbackParticleB.Reset(); S->PendingReadbackStep = 0;
	}
	void ConsumeGpuTimings(const TSharedRef<FFlowMapGpuClosedLoopState, ESPMode::ThreadSafe>& S)
	{
		if (!S->Stats.bGpuTimingSupported) return;
		for (FFlowMapGpuClosedLoopState::FGpuTimingSlot& Slot : S->GpuTimingSlots)
		{
			if (!Slot.bPending) continue;
			uint64 StartMicroseconds = 0, EndMicroseconds = 0;
			if (!RHIGetRenderQueryResult(Slot.StartQuery.GetReference(), StartMicroseconds, false)
				|| !RHIGetRenderQueryResult(Slot.EndQuery.GetReference(), EndMicroseconds, false)) continue;
			FScopeLock Guard(&S->Lock);
			if (EndMicroseconds > StartMicroseconds)
			{
				const double Milliseconds = static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0;
				S->GpuTimingSteps.Add(Slot.Step); S->GpuStepMilliseconds.Add(Milliseconds);
				S->Stats.GpuTimingSamplesCompleted++; S->Stats.LatestGpuTimingStep=Slot.Step; S->Stats.LatestGpuStepMilliseconds=Milliseconds;
				S->Stats.MaximumGpuStepMilliseconds=FMath::Max(S->Stats.MaximumGpuStepMilliseconds,Milliseconds);
				double Total=0.0;for(double Sample:S->GpuStepMilliseconds)Total+=Sample;S->Stats.MeanGpuStepMilliseconds=Total/S->GpuStepMilliseconds.Num();
				TArray<double> Sorted=S->GpuStepMilliseconds;Sorted.Sort();
				S->Stats.P95GpuStepMilliseconds=Sorted[FMath::Clamp(FMath::CeilToInt(0.95*Sorted.Num())-1,0,Sorted.Num()-1)];
				S->Stats.P99GpuStepMilliseconds=Sorted[FMath::Clamp(FMath::CeilToInt(0.99*Sorted.Num())-1,0,Sorted.Num()-1)];
				TArray<double> Steady;for(int32 I=0;I<S->GpuStepMilliseconds.Num();++I)if(S->GpuTimingSteps[I]>S->Input.GpuTimingWarmupSteps)Steady.Add(S->GpuStepMilliseconds[I]);
				S->Stats.SteadyStateGpuTimingSamples=Steady.Num();
				if(!Steady.IsEmpty()){double SteadyTotal=0.0;for(double Sample:Steady)SteadyTotal+=Sample;S->Stats.MeanSteadyStateGpuStepMilliseconds=SteadyTotal/Steady.Num();Steady.Sort();S->Stats.P95SteadyStateGpuStepMilliseconds=Steady[FMath::Clamp(FMath::CeilToInt(0.95*Steady.Num())-1,0,Steady.Num()-1)];S->Stats.P99SteadyStateGpuStepMilliseconds=Steady[FMath::Clamp(FMath::CeilToInt(0.99*Steady.Num())-1,0,Steady.Num()-1)];S->Stats.MaximumSteadyStateGpuStepMilliseconds=Steady.Last();}
			}
			else S->Stats.GpuTimingSamplesSkipped++;
			Slot.Step=0;Slot.bPending=false;
		}
	}
}

FFlowMapGpuClosedLoopSession::FFlowMapGpuClosedLoopSession() = default;
FFlowMapGpuClosedLoopSession::~FFlowMapGpuClosedLoopSession() { Shutdown(); }

bool FFlowMapGpuClosedLoopSession::Initialize(const FFlowMapGpuClosedLoopInput& Input)
{
	if (!IsInGameThread() || !GDynamicRHI || !FlowMapGpuClosedLoop::Validate(Input)) return false;
	Shutdown(); State = MakeShared<FFlowMapGpuClosedLoopState, ESPMode::ThreadSafe>(); State->Input = Input;
	for (const FFlowMapGpuImpulseParticle& P : Input.Particles)
	{
		const FVector4f B = P.BackwardMapJacobian, G = P.ReferenceImpulseGradientPerSecond;
		State->InitialParticleA.Emplace(P.PositionCm.X, P.PositionCm.Y, B.X, B.Y); State->InitialParticleB.Emplace(B.Z, B.W, 0, 0);
		State->Payload0.Emplace(P.ReferenceImpulseCmPerSecond.X, P.ReferenceImpulseCmPerSecond.Y, G.X, G.Y); State->Payload1.Emplace(G.Z, G.W, 0, 0);
	}
	State->SolidBoundary.SolidCellMask.Init(0u,Input.CellCounts.X*Input.CellCounts.Y);State->SolidBoundary.SolidFaceUMask.Init(0u,Input.InitialFaceU.Num());State->SolidBoundary.SolidFaceVMask.Init(0u,Input.InitialFaceV.Num());State->SolidBoundary.SolidFaceVelocityU.Init(0.0f,Input.InitialFaceU.Num());State->SolidBoundary.SolidFaceVelocityV.Init(0.0f,Input.InitialFaceV.Num());
	const auto S = State.ToSharedRef();
	ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopInitialize)([S](FRHICommandListImmediate& R)
	{
		if (S->bShutdown) return; bool bGpuTimingSupported=GSupportsTimestampRenderQueries;if(bGpuTimingSupported){S->GpuTimingSlots.SetNum(8);for(FFlowMapGpuClosedLoopState::FGpuTimingSlot& Slot:S->GpuTimingSlots){Slot.StartQuery=RHICreateRenderQuery(RQT_AbsoluteTime);Slot.EndQuery=RHICreateRenderQuery(RQT_AbsoluteTime);bGpuTimingSupported=bGpuTimingSupported&&Slot.StartQuery.IsValid()&&Slot.EndQuery.IsValid();}if(!bGpuTimingSupported)S->GpuTimingSlots.Reset();} FRDGBuilder G(R); const int32 C = S->Input.CellCounts.X * S->Input.CellCounts.Y;
		auto CU = FlowMapGpuClosedLoop::Upload(G, TEXT("FlowMap.Closed.CurrentU"), S->Input.InitialFaceU, true); auto CV = FlowMapGpuClosedLoop::Upload(G, TEXT("FlowMap.Closed.CurrentV"), S->Input.InitialFaceV, true);
		auto WU = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.WorkU"), sizeof(float), S->Input.InitialFaceU.Num(), true); auto WV = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.WorkV"), sizeof(float), S->Input.InitialFaceV.Num(), true);
		auto PA = FlowMapGpuClosedLoop::Upload(G, TEXT("FlowMap.Closed.ParticleA"), S->InitialParticleA, true); auto PB = FlowMapGpuClosedLoop::Upload(G, TEXT("FlowMap.Closed.ParticleB"), S->InitialParticleB, true);
		auto PWA = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.ParticleWorkA"), sizeof(FVector4f), S->Input.Particles.Num(), true); auto PWB = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.ParticleWorkB"), sizeof(FVector4f), S->Input.Particles.Num(), true);
		auto P0 = FlowMapGpuClosedLoop::Upload(G, TEXT("FlowMap.Closed.Payload0"), S->Payload0); auto P1 = FlowMapGpuClosedLoop::Upload(G, TEXT("FlowMap.Closed.Payload1"), S->Payload1);
		auto SCM=FlowMapGpuClosedLoop::Upload(G,TEXT("FlowMap.Closed.SolidCellMask"),S->SolidBoundary.SolidCellMask);auto SUM=FlowMapGpuClosedLoop::Upload(G,TEXT("FlowMap.Closed.SolidFaceUMask"),S->SolidBoundary.SolidFaceUMask);auto SVM=FlowMapGpuClosedLoop::Upload(G,TEXT("FlowMap.Closed.SolidFaceVMask"),S->SolidBoundary.SolidFaceVMask);auto SUV=FlowMapGpuClosedLoop::Upload(G,TEXT("FlowMap.Closed.SolidFaceVelocityU"),S->SolidBoundary.SolidFaceVelocityU);auto SVV=FlowMapGpuClosedLoop::Upload(G,TEXT("FlowMap.Closed.SolidFaceVelocityV"),S->SolidBoundary.SolidFaceVelocityV);
		TArray<FVector4f> ZeroCircles;ZeroCircles.Init(FVector4f(0,0,0,0),8);auto SC=FlowMapGpuClosedLoop::Upload(G,TEXT("FlowMap.Closed.SolidCircles"),ZeroCircles);
		auto D = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.Divergence"), sizeof(float), C); auto PrA = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.PressureA"), sizeof(float), C); auto PrB = FlowMapGpuClosedLoop::Work(G, TEXT("FlowMap.Closed.PressureB"), sizeof(float), C);
		AddClearUAVPass(G, G.CreateUAV(PrA), 0u); AddClearUAVPass(G, G.CreateUAV(PrB), 0u);
		S->FaceCurrentU=G.ConvertToExternalBuffer(CU); S->FaceCurrentV=G.ConvertToExternalBuffer(CV); S->FaceWorkU=G.ConvertToExternalBuffer(WU); S->FaceWorkV=G.ConvertToExternalBuffer(WV);
		S->ParticleCurrentA=G.ConvertToExternalBuffer(PA); S->ParticleCurrentB=G.ConvertToExternalBuffer(PB); S->ParticleWorkA=G.ConvertToExternalBuffer(PWA); S->ParticleWorkB=G.ConvertToExternalBuffer(PWB);
		S->GpuPayload0=G.ConvertToExternalBuffer(P0); S->GpuPayload1=G.ConvertToExternalBuffer(P1); S->Divergence=G.ConvertToExternalBuffer(D); S->PressureA=G.ConvertToExternalBuffer(PrA); S->PressureB=G.ConvertToExternalBuffer(PrB);
		S->SolidCellMask=G.ConvertToExternalBuffer(SCM);S->SolidFaceUMask=G.ConvertToExternalBuffer(SUM);S->SolidFaceVMask=G.ConvertToExternalBuffer(SVM);S->SolidFaceVelocityU=G.ConvertToExternalBuffer(SUV);S->SolidFaceVelocityV=G.ConvertToExternalBuffer(SVV);
		S->SolidCircles=G.ConvertToExternalBuffer(SC);
		G.Execute(); FScopeLock Guard(&S->Lock); S->Stats.bInitialized = true; S->Stats.bGpuTimingSupported=bGpuTimingSupported;
	}); return true;
}

bool FFlowMapGpuClosedLoopSession::UpdateSolidBoundary(const FFlowMapGpuSolidBoundaryUpdate& Boundary)
{
	if(!IsInGameThread()||!State.IsValid()||!FlowMapGpuClosedLoop::ValidateBoundary(State->Input,Boundary))return false;
	FScopeLock Guard(&State->Lock);if(State->bShutdown)return false;State->SolidBoundary=Boundary;State->SolidBoundaryVersion++;State->Stats.SolidBoundaryUpdatesAccepted++;
	State->Stats.LatestSolidCellCount=0;State->Stats.LatestSolidFaceUCount=0;State->Stats.LatestSolidFaceVCount=0;State->Stats.LatestSolidCircleCount=Boundary.ParticleCollisionCircles.Num();
	for(uint32 Value:Boundary.SolidCellMask)State->Stats.LatestSolidCellCount+=Value!=0;for(uint32 Value:Boundary.SolidFaceUMask)State->Stats.LatestSolidFaceUCount+=Value!=0;for(uint32 Value:Boundary.SolidFaceVMask)State->Stats.LatestSolidFaceVCount+=Value!=0;return true;
}

bool FFlowMapGpuClosedLoopSession::EnqueueStep()
{
	if (!IsInGameThread() || !State.IsValid()) return false; const auto S = State.ToSharedRef(); int64 Step,BoundaryVersion;FFlowMapGpuSolidBoundaryUpdate Boundary;
	{ FScopeLock G(&S->Lock); if (S->bShutdown) return false; Step = ++S->Stats.StepsEnqueued;BoundaryVersion=S->SolidBoundaryVersion;Boundary=S->SolidBoundary; }
	ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopStep)([S, Step, BoundaryVersion, Boundary=MoveTemp(Boundary)](FRHICommandListImmediate& R)
	{
		if (S->bShutdown || !S->FaceCurrentU || !S->SolidCellMask || !S->SolidFaceUMask || !S->SolidFaceVMask || !S->SolidFaceVelocityU || !S->SolidFaceVelocityV || !S->SolidCircles) return; FlowMapGpuClosedLoop::ConsumeReadback(S); FlowMapGpuClosedLoop::ConsumeGpuTimings(S); FFlowMapGpuClosedLoopState::FGpuTimingSlot* TimingSlot=nullptr;if(S->Stats.bGpuTimingSupported){for(FFlowMapGpuClosedLoopState::FGpuTimingSlot& Slot:S->GpuTimingSlots)if(!Slot.bPending){TimingSlot=&Slot;break;}if(TimingSlot)R.EndRenderQuery(TimingSlot->StartQuery.GetReference());else{FScopeLock Guard(&S->Lock);S->Stats.GpuTimingSamplesSkipped++;}} FRDGBuilder G(R);
		auto Reg=[&G](const TRefCountPtr<FRDGPooledBuffer>& B,const TCHAR* N){return G.RegisterExternalBuffer(B,N);};
		auto CU=Reg(S->FaceCurrentU,TEXT("FlowMap.Closed.CurrentU")), CV=Reg(S->FaceCurrentV,TEXT("FlowMap.Closed.CurrentV")), WU=Reg(S->FaceWorkU,TEXT("FlowMap.Closed.WorkU")), WV=Reg(S->FaceWorkV,TEXT("FlowMap.Closed.WorkV"));
		auto PA=Reg(S->ParticleCurrentA,TEXT("FlowMap.Closed.ParticleA")), PB=Reg(S->ParticleCurrentB,TEXT("FlowMap.Closed.ParticleB")), PWA=Reg(S->ParticleWorkA,TEXT("FlowMap.Closed.ParticleWorkA")), PWB=Reg(S->ParticleWorkB,TEXT("FlowMap.Closed.ParticleWorkB"));
		auto P0=Reg(S->GpuPayload0,TEXT("FlowMap.Closed.Payload0")), P1=Reg(S->GpuPayload1,TEXT("FlowMap.Closed.Payload1")), D=Reg(S->Divergence,TEXT("FlowMap.Closed.Divergence")), PrA=Reg(S->PressureA,TEXT("FlowMap.Closed.PressureA")), PrB=Reg(S->PressureB,TEXT("FlowMap.Closed.PressureB"));
		auto SCM=Reg(S->SolidCellMask,TEXT("FlowMap.Closed.SolidCellMask")),SUM=Reg(S->SolidFaceUMask,TEXT("FlowMap.Closed.SolidFaceUMask")),SVM=Reg(S->SolidFaceVMask,TEXT("FlowMap.Closed.SolidFaceVMask")),SUV=Reg(S->SolidFaceVelocityU,TEXT("FlowMap.Closed.SolidFaceVelocityU")),SVV=Reg(S->SolidFaceVelocityV,TEXT("FlowMap.Closed.SolidFaceVelocityV")),SC=Reg(S->SolidCircles,TEXT("FlowMap.Closed.SolidCircles"));
		G.QueueBufferUpload(SCM,Boundary.SolidCellMask.GetData(),Boundary.SolidCellMask.Num()*sizeof(uint32));G.QueueBufferUpload(SUM,Boundary.SolidFaceUMask.GetData(),Boundary.SolidFaceUMask.Num()*sizeof(uint32));G.QueueBufferUpload(SVM,Boundary.SolidFaceVMask.GetData(),Boundary.SolidFaceVMask.Num()*sizeof(uint32));G.QueueBufferUpload(SUV,Boundary.SolidFaceVelocityU.GetData(),Boundary.SolidFaceVelocityU.Num()*sizeof(float));G.QueueBufferUpload(SVV,Boundary.SolidFaceVelocityV.GetData(),Boundary.SolidFaceVelocityV.Num()*sizeof(float));TArray<FVector4f> PackedCircles;PackedCircles.Init(FVector4f(0,0,0,0),8);for(int32 I=0;I<Boundary.ParticleCollisionCircles.Num();++I){const auto& C=Boundary.ParticleCollisionCircles[I];PackedCircles[I]=FVector4f(C.CenterCm.X,C.CenterCm.Y,C.RadiusCm,C.SafetyOffsetCm);}G.QueueBufferUpload(SC,PackedCircles.GetData(),PackedCircles.Num()*sizeof(FVector4f));
		AddClearUAVPass(G,G.CreateUAV(PrA),0u); AddClearUAVPass(G,G.CreateUAV(PrB),0u); const int32 Nx=S->Input.CellCounts.X, Ny=S->Input.CellCounts.Y, PC=S->Input.Particles.Num(), CC=Nx*Ny;
		TShaderMapRef<FFlowMapClosedLoopAdvectCS> AS(GetGlobalShaderMap(GMaxRHIFeatureLevel)); auto* A=G.AllocParameters<FFlowMapClosedLoopAdvectCS::FParameters>();
		A->CellCountX=Nx;A->CellCountY=Ny;A->ParticleCount=PC;A->ParticleSolidCircleCount=Boundary.ParticleCollisionCircles.Num();A->CellSizeCm=S->Input.CellSizeCm;A->OriginCm=S->Input.OriginCm;A->FixedDeltaSeconds=S->Input.FixedDeltaSeconds;A->ParticleFaceU=G.CreateSRV(CU);A->ParticleFaceV=G.CreateSRV(CV);A->InputParticleStateA=G.CreateSRV(PA);A->InputParticleStateB=G.CreateSRV(PB);A->ParticleSolidCircles=G.CreateSRV(SC);A->OutputParticleStateA=G.CreateUAV(PWA);A->OutputParticleStateB=G.CreateUAV(PWB);
		FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Advect %lld",Step),AS,A,FIntVector(FMath::DivideAndRoundUp(PC,64),1,1));
		auto SetImpulse=[&](auto* X){X->ImpulseCellCountX=Nx;X->ImpulseCellCountY=Ny;X->ImpulseParticleCount=PC;X->ImpulseCellSizeCm=S->Input.CellSizeCm;X->ImpulseOriginCm=S->Input.OriginCm;X->ImpulseUseGradient=S->Input.bUseGradient?1u:0u;X->ImpulseParticleStateA=G.CreateSRV(PWA);X->ImpulseParticleStateB=G.CreateSRV(PWB);X->ReferenceImpulseAndGradientRow0=G.CreateSRV(P0);X->ReferenceGradientRow1=G.CreateSRV(P1);};
		TShaderMapRef<FFlowMapClosedLoopGatherUCS> GUS(GetGlobalShaderMap(GMaxRHIFeatureLevel));auto* GU=G.AllocParameters<FFlowMapClosedLoopGatherUCS::FParameters>();SetImpulse(GU);GU->OutputImpulseFaceU=G.CreateUAV(WU);FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Gather U"),GUS,GU,FIntVector(FMath::DivideAndRoundUp(S->Input.InitialFaceU.Num(),64),1,1));
		TShaderMapRef<FFlowMapClosedLoopGatherVCS> GVS(GetGlobalShaderMap(GMaxRHIFeatureLevel));auto* GV=G.AllocParameters<FFlowMapClosedLoopGatherVCS::FParameters>();SetImpulse(GV);GV->OutputImpulseFaceV=G.CreateUAV(WV);FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Gather V"),GVS,GV,FIntVector(FMath::DivideAndRoundUp(S->Input.InitialFaceV.Num(),64),1,1));
		TShaderMapRef<FFlowMapClosedLoopDivergenceCS> DS(GetGlobalShaderMap(GMaxRHIFeatureLevel));auto* DP=G.AllocParameters<FFlowMapClosedLoopDivergenceCS::FParameters>();DP->CellCountX=Nx;DP->CellCountY=Ny;DP->CellSizeCm=S->Input.CellSizeCm;DP->SolidCellMask=G.CreateSRV(SCM);DP->SolidFaceUMask=G.CreateSRV(SUM);DP->SolidFaceVMask=G.CreateSRV(SVM);DP->SolidFaceVelocityU=G.CreateSRV(SUV);DP->SolidFaceVelocityV=G.CreateSRV(SVV);DP->SolidInputFaceU=G.CreateSRV(WU);DP->SolidInputFaceV=G.CreateSRV(WV);DP->SolidOutputDivergence=G.CreateUAV(D);FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Divergence SolidAware"),DS,DP,FIntVector(FMath::DivideAndRoundUp(CC,64),1,1));
		auto PrC=PrA,PrN=PrB;TShaderMapRef<FFlowMapClosedLoopJacobiCS> JS(GetGlobalShaderMap(GMaxRHIFeatureLevel));for(int32 I=0;I<S->Input.PressureIterations;++I){auto* J=G.AllocParameters<FFlowMapClosedLoopJacobiCS::FParameters>();J->CellCountX=Nx;J->CellCountY=Ny;J->CellSizeCm=S->Input.CellSizeCm;J->SolidCellMask=G.CreateSRV(SCM);J->SolidInputDivergence=G.CreateSRV(D);J->SolidInputPressure=G.CreateSRV(PrC);J->SolidOutputPressure=G.CreateUAV(PrN);FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Jacobi SolidAware %d",I),JS,J,FIntVector(FMath::DivideAndRoundUp(CC,64),1,1));Swap(PrC,PrN);}
		const float MaxFaceVelocityCmPerSecond=S->Input.MaximumFaceVelocityCmPerSecond>0.0f?S->Input.MaximumFaceVelocityCmPerSecond:0.9f*S->Input.CellSizeCm/S->Input.FixedDeltaSeconds;
		TShaderMapRef<FFlowMapClosedLoopApplyUCS> AUS(GetGlobalShaderMap(GMaxRHIFeatureLevel));auto* AU=G.AllocParameters<FFlowMapClosedLoopApplyUCS::FParameters>();AU->CellCountX=Nx;AU->CellCountY=Ny;AU->CellSizeCm=S->Input.CellSizeCm;AU->MaxFaceVelocityCmPerSecond=MaxFaceVelocityCmPerSecond;AU->SolidFaceUMask=G.CreateSRV(SUM);AU->SolidFaceVelocityU=G.CreateSRV(SUV);AU->SolidApplyInputPressure=G.CreateSRV(PrC);AU->SolidApplyInputFaceU=G.CreateSRV(WU);AU->SolidApplyOutputFaceU=G.CreateUAV(CU);FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Apply U SolidAware"),AUS,AU,FIntVector(FMath::DivideAndRoundUp(S->Input.InitialFaceU.Num(),64),1,1));
		TShaderMapRef<FFlowMapClosedLoopApplyVCS> AVS(GetGlobalShaderMap(GMaxRHIFeatureLevel));auto* AV=G.AllocParameters<FFlowMapClosedLoopApplyVCS::FParameters>();AV->CellCountX=Nx;AV->CellCountY=Ny;AV->CellSizeCm=S->Input.CellSizeCm;AV->MaxFaceVelocityCmPerSecond=MaxFaceVelocityCmPerSecond;AV->SolidFaceVMask=G.CreateSRV(SVM);AV->SolidFaceVelocityV=G.CreateSRV(SVV);AV->SolidApplyInputPressure=G.CreateSRV(PrC);AV->SolidApplyInputFaceV=G.CreateSRV(WV);AV->SolidApplyOutputFaceV=G.CreateUAV(CV);FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Apply V SolidAware"),AVS,AV,FIntVector(FMath::DivideAndRoundUp(S->Input.InitialFaceV.Num(),64),1,1));
		const bool bResetMap = Step % S->Input.LongMapResetSteps == 0;
		auto FinalPA = PWA, FinalPB = PWB;
		if (bResetMap)
		{
			TShaderMapRef<FFlowMapClosedLoopResetCS> RS(GetGlobalShaderMap(GMaxRHIFeatureLevel)); auto* RP = G.AllocParameters<FFlowMapClosedLoopResetCS::FParameters>();
			RP->CellCountX=Nx;RP->CellCountY=Ny;RP->ParticleCount=PC;RP->CellSizeCm=S->Input.CellSizeCm;RP->OriginCm=S->Input.OriginCm;RP->FixedDeltaSeconds=S->Input.FixedDeltaSeconds;RP->ParticleFaceU=G.CreateSRV(CU);RP->ParticleFaceV=G.CreateSRV(CV);RP->ResetInputParticleStateA=G.CreateSRV(PWA);RP->ResetOutputParticleStateA=G.CreateUAV(PA);RP->ResetOutputParticleStateB=G.CreateUAV(PB);RP->ResetReferenceImpulseAndGradientRow0=G.CreateUAV(P0);RP->ResetReferenceGradientRow1=G.CreateUAV(P1);
			FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("FlowMap Closed Map Reset %lld",Step),RS,RP,FIntVector(FMath::DivideAndRoundUp(PC,64),1,1)); FinalPA=PA;FinalPB=PB;
		}
		if(!S->ReadbackU && Step%S->Input.ReadbackIntervalSteps==0){S->ReadbackU=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.ReadbackU"));S->ReadbackV=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.ReadbackV"));S->ReadbackPressure=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.ReadbackPressure"));S->ReadbackParticleA=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.ReadbackPA"));S->ReadbackParticleB=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.ReadbackPB"));S->PendingReadbackStep=Step;AddEnqueueCopyPass(G,S->ReadbackU.Get(),CU,S->Input.InitialFaceU.Num()*sizeof(float));AddEnqueueCopyPass(G,S->ReadbackV.Get(),CV,S->Input.InitialFaceV.Num()*sizeof(float));AddEnqueueCopyPass(G,S->ReadbackPressure.Get(),PrC,CC*sizeof(float));AddEnqueueCopyPass(G,S->ReadbackParticleA.Get(),FinalPA,PC*sizeof(FVector4f));AddEnqueueCopyPass(G,S->ReadbackParticleB.Get(),FinalPB,PC*sizeof(FVector4f));FScopeLock Guard(&S->Lock);S->Stats.bReadbackPending=true;}
		G.Execute();if(TimingSlot){R.EndRenderQuery(TimingSlot->EndQuery.GetReference());R.SubmitCommandsHint();TimingSlot->Step=Step;TimingSlot->bPending=true;}if(!bResetMap){Swap(S->ParticleCurrentA,S->ParticleWorkA);Swap(S->ParticleCurrentB,S->ParticleWorkB);}FScopeLock Guard(&S->Lock);S->Stats.StepsExecuted=Step;S->Stats.ParticleAdvectionPasses++;S->Stats.ImpulseGatherPasses+=2;S->Stats.DivergencePasses++;S->Stats.PressureJacobiPasses+=S->Input.PressureIterations;S->Stats.PressureApplyPasses+=2;S->Stats.SolidBoundaryApplicationsExecuted++;S->Stats.LatestSolidBoundaryVersion=BoundaryVersion;if(!Boundary.ParticleCollisionCircles.IsEmpty())S->Stats.ParticleSolidProjectionPasses++;if(bResetMap){S->Stats.MapResetPasses++;S->Stats.MapResets++;}
	}); return true;
}

void FFlowMapGpuClosedLoopSession::RequestReadback()
{
	if(!IsInGameThread()||!State.IsValid())return;const auto S=State.ToSharedRef();ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopRequest)([S](FRHICommandListImmediate&R){if(S->bShutdown||!S->FaceCurrentU)return;FlowMapGpuClosedLoop::ConsumeReadback(S);FlowMapGpuClosedLoop::ConsumeGpuTimings(S);if(S->ReadbackU)return;FRDGBuilder G(R);auto U=G.RegisterExternalBuffer(S->FaceCurrentU,TEXT("FlowMap.Closed.RequestU")),V=G.RegisterExternalBuffer(S->FaceCurrentV,TEXT("FlowMap.Closed.RequestV")),Pressure=G.RegisterExternalBuffer((S->Input.PressureIterations%2)==0?S->PressureA:S->PressureB,TEXT("FlowMap.Closed.RequestPressure")),A=G.RegisterExternalBuffer(S->ParticleCurrentA,TEXT("FlowMap.Closed.RequestPA")),B=G.RegisterExternalBuffer(S->ParticleCurrentB,TEXT("FlowMap.Closed.RequestPB"));S->ReadbackU=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.RequestU"));S->ReadbackV=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.RequestV"));S->ReadbackPressure=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.RequestPressure"));S->ReadbackParticleA=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.RequestPA"));S->ReadbackParticleB=MakeUnique<FRHIGPUBufferReadback>(TEXT("FlowMap.Closed.RequestPB"));{FScopeLock Guard(&S->Lock);S->PendingReadbackStep=S->Stats.StepsExecuted;S->Stats.bReadbackPending=true;}AddEnqueueCopyPass(G,S->ReadbackU.Get(),U,S->Input.InitialFaceU.Num()*sizeof(float));AddEnqueueCopyPass(G,S->ReadbackV.Get(),V,S->Input.InitialFaceV.Num()*sizeof(float));AddEnqueueCopyPass(G,S->ReadbackPressure.Get(),Pressure,S->Input.CellCounts.X*S->Input.CellCounts.Y*sizeof(float));AddEnqueueCopyPass(G,S->ReadbackParticleA.Get(),A,S->Input.Particles.Num()*sizeof(FVector4f));AddEnqueueCopyPass(G,S->ReadbackParticleB.Get(),B,S->Input.Particles.Num()*sizeof(FVector4f));G.Execute();});
}
void FFlowMapGpuClosedLoopSession::PollReadback(){if(!IsInGameThread()||!State.IsValid())return;const auto S=State.ToSharedRef();ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopPoll)([S](FRHICommandListImmediate&R){if(!S->bShutdown){FlowMapGpuClosedLoop::ConsumeReadback(S);FlowMapGpuClosedLoop::ConsumeGpuTimings(S);}});}
bool FFlowMapGpuClosedLoopSession::ConsumeLatestSnapshot(FFlowMapGpuClosedLoopSnapshot& O){if(!State.IsValid())return false;FScopeLock G(&State->Lock);if(!State->bSnapshotAvailable)return false;O=State->LatestSnapshot;State->bSnapshotAvailable=false;return true;}
FFlowMapGpuClosedLoopStats FFlowMapGpuClosedLoopSession::GetStats()const{if(!State.IsValid())return {};FScopeLock G(&State->Lock);return State->Stats;}
void FFlowMapGpuClosedLoopSession::GetGpuTimingSamples(TArray<int64>& OutSteps,TArray<double>& OutMilliseconds)const{OutSteps.Reset();OutMilliseconds.Reset();if(!State.IsValid())return;FScopeLock G(&State->Lock);OutSteps=State->GpuTimingSteps;OutMilliseconds=State->GpuStepMilliseconds;}
void FFlowMapGpuClosedLoopSession::FlushAndWaitForReadbackForValidation(){if(!IsInGameThread()||!State.IsValid())return;FlushRenderingCommands();ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopIdle)([](FRHICommandListImmediate&R){R.BlockUntilGPUIdle();});FlushRenderingCommands();RequestReadback();FlushRenderingCommands();ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopReadbackIdle)([](FRHICommandListImmediate&R){R.BlockUntilGPUIdle();});FlushRenderingCommands();PollReadback();FlushRenderingCommands();}
void FFlowMapGpuClosedLoopSession::Shutdown(){if(!State.IsValid())return;const auto S=State.ToSharedRef();{FScopeLock G(&S->Lock);S->bShutdown=true;}ENQUEUE_RENDER_COMMAND(FlowMapClosedLoopShutdown)([S](FRHICommandListImmediate&R){S->ReadbackU.Reset();S->ReadbackV.Reset();S->ReadbackPressure.Reset();S->ReadbackParticleA.Reset();S->ReadbackParticleB.Reset();S->GpuTimingSlots.Reset();S->GpuTimingSteps.Reset();S->GpuStepMilliseconds.Reset();S->FaceCurrentU.SafeRelease();S->FaceCurrentV.SafeRelease();S->FaceWorkU.SafeRelease();S->FaceWorkV.SafeRelease();S->ParticleCurrentA.SafeRelease();S->ParticleCurrentB.SafeRelease();S->ParticleWorkA.SafeRelease();S->ParticleWorkB.SafeRelease();S->GpuPayload0.SafeRelease();S->GpuPayload1.SafeRelease();S->Divergence.SafeRelease();S->PressureA.SafeRelease();S->PressureB.SafeRelease();S->SolidCellMask.SafeRelease();S->SolidFaceUMask.SafeRelease();S->SolidFaceVMask.SafeRelease();S->SolidFaceVelocityU.SafeRelease();S->SolidFaceVelocityV.SafeRelease();S->SolidCircles.SafeRelease();});State.Reset();}
