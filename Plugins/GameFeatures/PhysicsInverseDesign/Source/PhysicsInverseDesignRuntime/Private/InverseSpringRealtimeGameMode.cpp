#include "InverseSpringRealtimeGameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "InverseSpringRealtimeActor.h"

void AInverseSpringRealtimeHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
    {
        return;
    }
    const AInverseSpringRealtimeActor* Actor = FindExperimentActor();
    UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
    UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
    DrawRect(FLinearColor(0.006f, 0.015f, 0.035f, 0.95f), 28.0f, 26.0f, 860.0f, 480.0f);
    DrawRect(Actor && Actor->IsIdentifiabilityFailure() ? FLinearColor(1.0f, 0.18f, 0.04f, 1.0f) : FLinearColor(0.0f, 0.78f, 1.0f, 1.0f), 28.0f, 26.0f, 7.0f, 480.0f);
    DrawText(TEXT("CASE 04 · 可微物理反向设计 · Chaos Constraint 重放"), FLinearColor::White, 50.0f, 44.0f, LargeFont, 1.0f, false);
    DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到 Case04 运行 Actor"), Actor && Actor->IsIdentifiabilityFailure() ? FLinearColor(1.0f, 0.35f, 0.16f) : (Actor && Actor->IsRunComplete() ? FLinearColor(0.25f, 1.0f, 0.55f) : FLinearColor(0.35f, 0.85f, 1.0f)), 50.0f, 84.0f, MediumFont, 0.9f, false);
    DrawText(FString::Printf(TEXT("固定步：%d / %d    观测终点：%d    Chaos 样本：%d"), Actor ? Actor->GetCompletedSteps() : 0, Actor ? Actor->GetTrajectorySteps() : 0, Actor ? Actor->GetObservationStep() : 0, Actor ? Actor->GetChaosSamples() : 0), FLinearColor::White, 50.0f, 124.0f, MediumFont, 0.95f, false);
    DrawText(FString::Printf(TEXT("损失 初始 → 拟合：%.6g → %.6g"), Actor ? Actor->GetInitialLoss() : 0.0, Actor ? Actor->GetFittedLoss() : 0.0), FLinearColor(0.60f, 0.86f, 1.0f), 50.0f, 162.0f, MediumFont, 0.95f, false);
    DrawText(FString::Printf(TEXT("恢复参数 驱动力：%.5f    阻尼：%.5f    参数 L1 误差：%.4f    梯度误差：%.3e"), Actor ? Actor->GetRecoveredDrive() : 0.0, Actor ? Actor->GetRecoveredDamping() : 0.0, Actor ? Actor->GetParameterL1Error() : 0.0, Actor ? Actor->GetGradientRelativeError() : 0.0), FLinearColor(0.35f, 1.0f, 0.74f), 50.0f, 200.0f, MediumFont, 0.85f, false);
    DrawText(FString::Printf(TEXT("研究轨迹末端误差：%.6g cm"), Actor ? Actor->GetResearchFinalTargetErrorCm() : 0.0), FLinearColor(0.0f, 0.82f, 1.0f), 50.0f, 238.0f, MediumFont, 0.95f, false);
    DrawText(FString::Printf(TEXT("Chaos ↔ 研究轨迹 RMS：%.4f cm    Chaos 末端目标误差：%.4f cm"), Actor ? Actor->GetChaosTrajectoryRmsCm() : 0.0, Actor ? Actor->GetChaosFinalTargetErrorCm() : 0.0), FLinearColor(1.0f, 0.65f, 0.16f), 50.0f, 276.0f, MediumFont, 0.95f, false);
    DrawText(TEXT("绿：目标轨迹    红：错误初值    青：优化研究模型    黄：真实 Chaos Constraint"), FLinearColor(0.86f, 0.9f, 1.0f), 50.0f, 318.0f, MediumFont, 0.87f, false);
    DrawText(TEXT("同名义质量 / 刚度 / 阻尼；模型偏差被报告，不要求 Chaos 数值等同"), FLinearColor(0.72f, 0.76f, 0.9f), 50.0f, 354.0f, MediumFont, 0.82f, false);
    DrawText(TEXT("空格 暂停  N 自然帧单步  R 重置  O 重新优化  F 失败配置  D 调试  E 导出"), FLinearColor(0.38f, 1.0f, 0.68f), 50.0f, 392.0f, MediumFont, 0.82f, false);
    DrawText(FString::Printf(TEXT("交互计数 暂停:%d 单步:%d 重置:%d 优化:%d 失败:%d 调试:%d 导出:%d 脚本:%d"), Actor ? Actor->GetPauseToggleCount() : 0, Actor ? Actor->GetSingleStepCount() : 0, Actor ? Actor->GetResetCount() : 0, Actor ? Actor->GetReoptimizeCount() : 0, Actor ? Actor->GetFailureToggleCount() : 0, Actor ? Actor->GetDebugToggleCount() : 0, Actor ? Actor->GetManualExportCount() : 0, Actor ? Actor->GetInteractionScriptPhase() : 0), FLinearColor(0.66f, 0.78f, 0.92f), 50.0f, 430.0f, MediumFont, 0.76f, false);
}

AInverseSpringRealtimeActor* AInverseSpringRealtimeHUD::FindExperimentActor() const
{
    if (GetWorld())
    {
        for (TActorIterator<AInverseSpringRealtimeActor> It(GetWorld()); It; ++It)
        {
            return *It;
        }
    }
    return nullptr;
}

AInverseSpringRealtimeGameMode::AInverseSpringRealtimeGameMode()
{
    HUDClass = AInverseSpringRealtimeHUD::StaticClass();
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    bStartPlayersAsSpectators = true;
}
