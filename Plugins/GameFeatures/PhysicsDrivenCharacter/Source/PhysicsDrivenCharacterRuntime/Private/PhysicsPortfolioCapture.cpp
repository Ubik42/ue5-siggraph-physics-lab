#include "PhysicsPortfolioCapture.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "FrameGrabber.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SViewport.h"

namespace
{
struct FPhysicsCapturePayload : IFramePayload
{
    double Time = 0;
    int32 Index = 0;
};

struct FPhysicsCaptureSession
{
    TUniquePtr<FFrameGrabber> Grabber;
    FString Directory;
    FString IndexCsv = TEXT("index,simulation_seconds,width,height,file\n");
    int32 Requested = 0;
    int32 Saved = 0;
    int32 Tick = 0;
    bool bFinished = false;

    void Drain()
    {
        IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        for (FCapturedFrameData& Frame : Grabber->GetCapturedFrames())
        {
            const FPhysicsCapturePayload* Payload = Frame.GetPayload<FPhysicsCapturePayload>();
            TSharedPtr<IImageWrapper> Wrapper = Images.CreateImageWrapper(EImageFormat::JPEG);
            if (!Wrapper->SetRaw(Frame.ColorBuffer.GetData(), Frame.ColorBuffer.Num() * sizeof(FColor),
                Frame.BufferSize.X, Frame.BufferSize.Y, ERGBFormat::BGRA, 8)) continue;
            const FString Name = FString::Printf(TEXT("frame_%05d.jpg"), Payload->Index);
            if (FFileHelper::SaveArrayToFile(Wrapper->GetCompressed(90), *FPaths::Combine(Directory, Name)))
            {
                IndexCsv += FString::Printf(TEXT("%d,%.9f,%d,%d,%s\n"), Payload->Index, Payload->Time,
                    Frame.BufferSize.X, Frame.BufferSize.Y, *Name);
                ++Saved;
            }
        }
    }
};
TUniquePtr<FPhysicsCaptureSession> Session;
}

void CapturePhysicsPortfolioFrame(const double SimulationSeconds)
{
    static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterRecord"));
    if (!bEnabled || (Session && Session->bFinished)) return;
    if (!Session)
    {
        if (!GEngine || !GEngine->GameViewport) return;
        FSceneViewport* Viewport = GEngine->GameViewport->GetGameViewport();
        if (!Viewport || Viewport->GetSizeXY().GetMin() < 64) return;
        TSharedPtr<SViewport> Widget = GEngine->GameViewport->GetGameViewportWidget();
        TSharedPtr<ISlateViewport> Interface = Widget ? Widget->GetViewportInterface().Pin() : nullptr;
        if (!Interface || Interface.Get() != static_cast<ISlateViewport*>(Viewport)) return;
        Session = MakeUnique<FPhysicsCaptureSession>();
        Session->Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhysicsResearch/Captures"),
            FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
        IFileManager::Get().MakeDirectory(*Session->Directory, true);
        Session->Grabber = MakeUnique<FFrameGrabber>(StaticCastSharedRef<FSceneViewport>(Interface.ToSharedRef()), Viewport->GetSizeXY());
        Session->Grabber->StartCapturingFrames();
        UE_LOG(LogTemp, Display, TEXT("Physics portfolio capture: %s"), *Session->Directory);
    }
    Session->Drain();
    double Duration = 6.0;
    FParse::Value(FCommandLine::Get(), TEXT("PhysicsCaptureDuration="), Duration);
    Duration = FMath::Clamp(Duration, 1.0, 60.0);
    if (SimulationSeconds <= Duration && (++Session->Tick % 2) == 0)
    {
        TSharedPtr<FPhysicsCapturePayload, ESPMode::ThreadSafe> Payload = MakeShared<FPhysicsCapturePayload, ESPMode::ThreadSafe>();
        Payload->Index = Session->Requested++;
        Payload->Time = SimulationSeconds;
        Session->Grabber->CaptureThisFrame(Payload);
    }
    if (SimulationSeconds >= Duration + 0.2)
    {
        Session->Grabber->StopCapturingFrames();
        Session->Grabber->Shutdown();
        Session->Drain();
        Session->Grabber.Reset();
        Session->bFinished = true;
        FFileHelper::SaveStringToFile(Session->IndexCsv, *FPaths::Combine(Session->Directory, TEXT("frames.csv")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("Physics portfolio capture complete: saved=%d requested=%d"), Session->Saved, Session->Requested);
    }
}
