#include "PhysicsControlCharacterHUD.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "PhysicsControlCharacterRealtimeActor.h"

void APhysicsControlCharacterHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    const APhysicsControlCharacterRealtimeActor* Experiment = nullptr;
    for (TActorIterator<APhysicsControlCharacterRealtimeActor> It(GetWorld()); It; ++It) { Experiment = *It; break; }
    if (!Experiment) return;

    if (Experiment->IsPortfolioMode())
    {
        DrawRect(FLinearColor(0.008f, 0.014f, 0.03f, 0.88f), 22.0f, 18.0f, 675.0f, 205.0f);
        DrawRect(FLinearColor(0.85f, 0.12f, 0.035f, 0.92f), 22.0f, 18.0f, 6.0f, 205.0f);
        DrawRect(FLinearColor(0.008f, 0.014f, 0.03f, 0.88f), 230.0f, Canvas->ClipY - 96.0f, 360.0f, 62.0f);
        DrawRect(FLinearColor(0.90f, 0.16f, 0.045f, 0.95f), 230.0f, Canvas->ClipY - 96.0f, 6.0f, 62.0f);
        DrawRect(FLinearColor(0.008f, 0.014f, 0.03f, 0.88f), Canvas->ClipX - 430.0f, Canvas->ClipY - 96.0f, 390.0f, 62.0f);
        DrawRect(FLinearColor(0.04f, 0.55f, 1.0f, 0.95f), Canvas->ClipX - 430.0f, Canvas->ClipY - 96.0f, 6.0f, 62.0f);
        DrawText(TEXT("A  被动 Chaos 基线"), FLinearColor(1.0f, 0.62f, 0.42f), 252.0f, Canvas->ClipY - 72.0f, nullptr, 1.05f, false);
        DrawText(TEXT("B  Chaos + Physics Control"), FLinearColor(0.55f, 0.86f, 1.0f), Canvas->ClipX - 405.0f, Canvas->ClipY - 72.0f, nullptr, 1.05f, false);
    }

    const float X = 36.0f;
    float Y = 30.0f;
    auto Line = [&](const FString& Text, const FLinearColor Color = FLinearColor::White, const float Scale = 1.0f)
    {
        DrawText(Text, Color, X, Y, nullptr, Scale, false);
        Y += 25.0f * Scale;
    };
    Line(TEXT("Case 05 · 真实 Chaos 物理角色控制"), FLinearColor(0.2f, 0.85f, 1.0f), 1.35f);
    Line(TEXT("左：ChaosPassiveRagdoll   右：ChaosPhysicsControlActive"));
    Line(Experiment->GetStatusLine(), Experiment->IsRunComplete() ? FLinearColor::Green : FLinearColor::Yellow);
    Line(FString::Printf(TEXT("Manny Physics Asset：%d Bodies · Research Controls：%d"), Experiment->GetPhysicsBodyCount(), Experiment->GetResearchControlCount()));
    Line(FString::Printf(TEXT("姿态误差：Baseline %.2f° · Research %.2f°"), Experiment->GetBaselinePoseErrorDegrees(), Experiment->GetResearchPoseErrorDegrees()));
    Line(FString::Printf(TEXT("Pelvis 位移：Baseline %.1f cm · Research %.1f cm"), Experiment->GetBaselinePelvisDisplacementCentimeters(), Experiment->GetResearchPelvisDisplacementCentimeters()));
    Y += 10.0f;
    Line(TEXT("边界：两路都是真实 Chaos；研究路额外启用 Epic Physics Control + 个人相位/指标编排"), FLinearColor(0.8f, 0.85f, 0.9f), 0.85f);
    if (Experiment->IsPortfolioMode())
    {
        Line(TEXT("作品证据：同资产 · 同冲量 · 固定 60 Hz · 360 帧 · CSV/JSON 可复算"), FLinearColor(0.35f, 0.9f, 1.0f), 0.85f);
    }
}
