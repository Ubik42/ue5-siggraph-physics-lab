#include "ContactClothGpuValidationGameMode.h"

#include "ContactClothGpuValidationActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"

void AContactClothGpuValidationHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const AContactClothGpuValidationActor* Actor = FindValidationActor();
	const FContactClothGpuJacobiResult Result = Actor ? Actor->GetGpuResult() : FContactClothGpuJacobiResult();
	const double ReductionPercent = Result.InputMaximumEdgeErrorCm > UE_DOUBLE_SMALL_NUMBER
		? (1.0 - Result.OutputMaximumEdgeErrorCm / Result.InputMaximumEdgeErrorCm) * 100.0
		: 0.0;

	DrawRect(FLinearColor(0.008f, 0.018f, 0.032f, 0.92f), 28.0f, 26.0f, 585.0f, 278.0f);
	DrawRect(FLinearColor(0.0f, 0.72f, 0.95f, 1.0f), 28.0f, 26.0f, 7.0f, 278.0f);
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(TEXT("CASE 01 · GPU 聚合 Jacobi 首个计算门"), FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到 GPU 验证 Actor"), Actor && Actor->DidValidationPass() ? FLinearColor(0.25f, 1.0f, 0.55f) : FLinearColor(1.0f, 0.35f, 0.25f), 50.0f, 90.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("粒子：%d    Jacobi 迭代：%d    D3D GPU 回读：%s"), Actor ? Actor->GetParticleCount() : 0, Actor ? Actor->GetIterations() : 0, Result.bReadbackSucceeded ? TEXT("成功") : TEXT("失败")), FLinearColor::White, 50.0f, 126.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("结构边最大误差：%.3f cm  →  %.3f cm"), Result.InputMaximumEdgeErrorCm, Result.OutputMaximumEdgeErrorCm), FLinearColor(0.82f, 0.88f, 0.96f), 50.0f, 160.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("误差降低：%.2f%%    阻塞验证耗时：%.3f ms"), ReductionPercent, Result.BlockingWallMilliseconds), FLinearColor(0.82f, 0.88f, 0.96f), 50.0f, 194.0f, MediumFont, 1.0f, false);
	DrawText(TEXT("橙：扰动输入    青：GPU 输出    红：固定顶边"), FLinearColor(1.0f, 0.72f, 0.2f), 50.0f, 240.0f, MediumFont, 0.95f, false);
	DrawText(TEXT("当前为正确性门：逐帧版将使用持久 Buffer 与异步回读"), FLinearColor(0.55f, 0.68f, 0.82f), 50.0f, 268.0f, MediumFont, 0.82f, false);
}

AContactClothGpuValidationActor* AContactClothGpuValidationHUD::FindValidationActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AContactClothGpuValidationActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AContactClothGpuValidationGameMode::AContactClothGpuValidationGameMode()
{
	HUDClass = AContactClothGpuValidationHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
