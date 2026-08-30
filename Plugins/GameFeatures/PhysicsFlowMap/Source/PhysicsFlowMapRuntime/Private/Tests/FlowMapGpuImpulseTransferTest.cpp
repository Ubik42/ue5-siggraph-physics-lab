#if WITH_DEV_AUTOMATION_TESTS

#include "FlowMapGpuImpulseTransfer.h"

#include "Misc/AutomationTest.h"
#include "RHI.h"

namespace FlowMapGpuImpulseTransferTest
{
	int32 WrapIndex(int32 Value, int32 Count) { return (Value % Count + Count) % Count; }
	FVector4f Multiply(const FVector4f& A, const FVector4f& B)
	{
		return FVector4f(A.X * B.X + A.Y * B.Z, A.X * B.Y + A.Y * B.W,
			A.Z * B.X + A.W * B.Z, A.Z * B.Y + A.W * B.W);
	}
	void TransferReference(const FFlowMapGpuImpulseTransferInput& Input, TArray<float>& OutU, TArray<float>& OutV)
	{
		const int32 Nx = Input.CellCounts.X;
		const int32 Ny = Input.CellCounts.Y;
		const float H = Input.CellSizeCm;
		TArray<float> CompactU; CompactU.Init(0.0f, Nx * Ny);
		TArray<float> CompactV; CompactV.Init(0.0f, Nx * Ny);
		TArray<float> WeightU; WeightU.Init(0.0f, Nx * Ny);
		TArray<float> WeightV; WeightV.Init(0.0f, Nx * Ny);
		for (const FFlowMapGpuImpulseParticle& Particle : Input.Particles)
		{
			const FVector4f B = Particle.BackwardMapJacobian;
			const FVector2f M0 = Particle.ReferenceImpulseCmPerSecond;
			const FVector2f Impulse(B.X * M0.X + B.Z * M0.Y, B.Y * M0.X + B.W * M0.Y);
			const FVector4f G0B = Multiply(Particle.ReferenceImpulseGradientPerSecond, B);
			const FVector4f G(B.X * G0B.X + B.Z * G0B.Z, B.X * G0B.Y + B.Z * G0B.W,
				B.Y * G0B.X + B.W * G0B.Z, B.Y * G0B.Y + B.W * G0B.W);
			const FVector2f Grid = (Particle.PositionCm - Input.OriginCm) / H;
			auto Deposit = [Nx, Ny, H, &Input, &Particle](TArray<float>& Values, TArray<float>& Weights,
				float X, float Y, float Value, const FVector2f& Gradient, bool bUFace)
			{
				const int32 X0 = FMath::FloorToInt(X);
				const int32 Y0 = FMath::FloorToInt(Y);
				const float Tx = X - X0;
				const float Ty = Y - Y0;
				for (int32 Oy = 0; Oy <= 1; ++Oy) for (int32 Ox = 0; Ox <= 1; ++Ox)
				{
					const int32 LogicalX = X0 + Ox;
					const int32 LogicalY = Y0 + Oy;
					const float Weight = (Ox ? Tx : 1.0f - Tx) * (Oy ? Ty : 1.0f - Ty);
					const FVector2f FacePosition = Input.OriginCm + (bUFace
						? FVector2f(LogicalX * H, (LogicalY + 0.5f) * H)
						: FVector2f((LogicalX + 0.5f) * H, LogicalY * H));
					const float Predicted = Input.bUseGradient ? Value + FVector2f::DotProduct(Gradient, FacePosition - Particle.PositionCm) : Value;
					const int32 Index = WrapIndex(LogicalY, Ny) * Nx + WrapIndex(LogicalX, Nx);
					Values[Index] += Weight * Predicted;
					Weights[Index] += Weight;
				}
			};
			Deposit(CompactU, WeightU, Grid.X, Grid.Y - 0.5f, Impulse.X, FVector2f(G.X, G.Y), true);
			Deposit(CompactV, WeightV, Grid.X - 0.5f, Grid.Y, Impulse.Y, FVector2f(G.Z, G.W), false);
		}
		OutU.Init(0.0f, (Nx + 1) * Ny);
		OutV.Init(0.0f, Nx * (Ny + 1));
		for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
			OutU[Y * (Nx + 1) + X] = WeightU[Y * Nx + X] > UE_SMALL_NUMBER ? CompactU[Y * Nx + X] / WeightU[Y * Nx + X] : 0.0f;
		for (int32 Y = 0; Y < Ny; ++Y) OutU[Y * (Nx + 1) + Nx] = OutU[Y * (Nx + 1)];
		for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
			OutV[Y * Nx + X] = WeightV[Y * Nx + X] > UE_SMALL_NUMBER ? CompactV[Y * Nx + X] / WeightV[Y * Nx + X] : 0.0f;
		for (int32 X = 0; X < Nx; ++X) OutV[Ny * Nx + X] = OutV[X];
	}
	float MaximumError(const TArray<float>& A, const TArray<float>& B)
	{
		float Result = 0.0f;
		for (int32 Index = 0; Index < A.Num() && Index < B.Num(); ++Index) Result = FMath::Max(Result, FMath::Abs(A[Index] - B[Index]));
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuImpulseTransferTest,
	"PhysicsResearch.FlowMap.GpuImpulseGradientTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuImpulseTransferTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU impulse/梯度 P2G；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapGpuImpulseTransferInput Input;
	Input.CellCounts = FIntPoint(16, 16);
	Input.CellSizeCm = 5.0f;
	Input.OriginCm = FVector2f(-40.0f, -40.0f);
	const float L = Input.CellCounts.X * Input.CellSizeCm;
	const float K = 2.0f * PI / L;
	for (int32 Y = 0; Y < Input.CellCounts.Y; ++Y) for (int32 X = 0; X < Input.CellCounts.X; ++X)
	{
		const float JitterX = 0.18f * FMath::Sin(0.73f * X + 1.17f * Y);
		const float JitterY = 0.16f * FMath::Cos(1.31f * X - 0.67f * Y);
		FFlowMapGpuImpulseParticle Particle;
		Particle.PositionCm = Input.OriginCm + FVector2f((X + 0.5f + JitterX) * Input.CellSizeCm, (Y + 0.5f + JitterY) * Input.CellSizeCm);
		const FVector2f Local = Particle.PositionCm - Input.OriginCm;
		Particle.ReferenceImpulseCmPerSecond = FVector2f(20.0f * FMath::Sin(K * Local.X) * FMath::Cos(K * Local.Y), -20.0f * FMath::Cos(K * Local.X) * FMath::Sin(K * Local.Y));
		Particle.ReferenceImpulseGradientPerSecond = FVector4f(
			20.0f * K * FMath::Cos(K * Local.X) * FMath::Cos(K * Local.Y), -20.0f * K * FMath::Sin(K * Local.X) * FMath::Sin(K * Local.Y),
			20.0f * K * FMath::Sin(K * Local.X) * FMath::Sin(K * Local.Y), -20.0f * K * FMath::Cos(K * Local.X) * FMath::Cos(K * Local.Y));
		Particle.BackwardMapJacobian = FVector4f(1.0f + 0.03f * FMath::Sin(0.2f * X), 0.02f * FMath::Cos(0.3f * Y),
			-0.015f * FMath::Sin(0.4f * Y), 1.0f - 0.025f * FMath::Cos(0.25f * X));
		Input.Particles.Add(Particle);
	}
	float PicMaximumError = 0.0f;
	float GradientMaximumError = 0.0f;
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		Input.bUseGradient = Mode == 1;
		TArray<float> CpuU, CpuV;
		FlowMapGpuImpulseTransferTest::TransferReference(Input, CpuU, CpuV);
		const FFlowMapGpuImpulseTransferResult Gpu = RunFlowMapGpuImpulseTransferForValidation(Input);
		TestTrue(TEXT("GPU impulse Gather 已派发"), Gpu.bDispatched);
		TestTrue(TEXT("GPU impulse U/V 已读回"), Gpu.bReadbackSucceeded);
		TestEqual(TEXT("GPU impulse 精确两个面 Gather Pass"), Gpu.GatherPasses, 2);
		const float Error = FMath::Max(FlowMapGpuImpulseTransferTest::MaximumError(CpuU, Gpu.FaceU), FlowMapGpuImpulseTransferTest::MaximumError(CpuV, Gpu.FaceV));
		if (Mode == 0) PicMaximumError = Error; else GradientMaximumError = Error;
	}
	TestTrue(TEXT("GPU/CPU 普通 impulse P2G 最大误差低于 1e-4"), PicMaximumError < 1.e-4f);
	TestTrue(TEXT("GPU/CPU 梯度 impulse P2G 最大误差低于 1e-4"), GradientMaximumError < 1.e-4f);
	AddInfo(FString::Printf(TEXT("FlowMap GPU impulse transfer rhi=%s cells=16x16 particles=%d modes=2 gather_passes_per_mode=2 pic_max_face_error=%.9g gradient_max_face_error=%.9g"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Input.Particles.Num(), PicMaximumError, GradientMaximumError));
	return true;
}

#endif
