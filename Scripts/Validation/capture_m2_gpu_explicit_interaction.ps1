param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$UnrealEditor = "C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe"
)
$ErrorActionPreference = "Stop"
$uproject = Join-Path $ProjectRoot "SIGGRAPHPhysicsLab.uproject"
$evidence = Join-Path $ProjectRoot "Docs\Validation\M2"
function Invoke-OwnedCase([string]$ModeArgument, [string]$RuntimeName) {
    $runtimePath = Join-Path $evidence $RuntimeName
    $startedAt = Get-Date
    $arguments = ('"{0}" /PhysicsContactCloth/Maps/L_GpuExplicitAssassin -game -windowed -ResX=1600 -ResY=900 -UseFixedTimeStep -FPS=60 {1} -PhysicsClothExplicitCapture -PhysicsClothExplicitAutoQuit -NoSplash -NoP4 -d3d12 -sm6' -f $uproject, $ModeArgument)
    $owned = Start-Process -FilePath $UnrealEditor -ArgumentList $arguments -PassThru
    Wait-Process -Id $owned.Id -Timeout 55 -ErrorAction SilentlyContinue
    if (Get-Process -Id $owned.Id -ErrorAction SilentlyContinue) {
        if ($owned.Path -eq $UnrealEditor) { Stop-Process -Id $owned.Id -Force }
        throw "UE 交互 Case 在 55 秒内未退出：PID=$($owned.Id)"
    }
    $ownedMonitors = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq "CrashReportClientEditor.exe" -and
        $_.ExecutablePath -like "C:\Program Files\Epic Games\UE_5.4*" -and
        $_.CommandLine -match ("-MONITOR=" + $owned.Id)
    })
    foreach ($monitor in $ownedMonitors) { Stop-Process -Id $monitor.ProcessId -Force }
    if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) { throw "缺少本轮运行报告：$runtimePath" }
    if ((Get-Item -LiteralPath $runtimePath).LastWriteTime -lt $startedAt) { throw "运行报告不是本轮生成：$runtimePath" }
}
Invoke-OwnedCase "-PhysicsClothExplicitInteractionScript" "gpu_explicit_assassin_interactive_runtime.json"
& (Join-Path $PSScriptRoot "validate_m2_gpu_explicit_interactive.ps1") -ProjectRoot $ProjectRoot
Invoke-OwnedCase "-PhysicsClothExplicitFailureWeakBarrier" "gpu_explicit_assassin_failure_weak_barrier_runtime.json"
& (Join-Path $PSScriptRoot "validate_m2_gpu_explicit_failure.ps1") -ProjectRoot $ProjectRoot
Write-Output "M2_GPU_EXPLICIT_INTERACTION_CAPTURE_PASSED"
