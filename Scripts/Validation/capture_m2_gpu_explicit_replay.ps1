param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$UnrealEditor = "C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe"
)
$ErrorActionPreference = "Stop"
$uproject = Join-Path $ProjectRoot "SIGGRAPHPhysicsLab.uproject"
$evidenceDirectory = Join-Path $ProjectRoot "Docs\Validation\M2"
$runtimePath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_runtime.json"
$firstRuntimePath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_first_runtime.json"
$verifiedRuntimePath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_verified_runtime.json"
$mismatchRuntimePath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_mismatch_runtime.json"
$comparisonCsvPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_comparison.csv"
$comparisonJsonPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_comparison.json"
$timingCsvPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_gpu_timing.csv"
$verifiedComparisonCsvPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_verified_comparison.csv"
$verifiedComparisonJsonPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_verified_comparison.json"
$verifiedTimingCsvPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_verified_gpu_timing.csv"
$screenshotPath = Join-Path $evidenceDirectory "Screenshots\M2_ExplicitAssassinReplay.png"
$validationPath = Join-Path $evidenceDirectory "gpu_explicit_assassin_replay_validation.json"
if (-not (Test-Path -LiteralPath $UnrealEditor -PathType Leaf)) { throw "缺少 UE 5.4 Editor：$UnrealEditor" }
if (-not (Test-Path -LiteralPath $uproject -PathType Leaf)) { throw "缺少工程：$uproject" }

function Invoke-ReplayRun([string]$ExpectedHash, [bool]$Capture) {
    $arguments = @(
        ('"{0}"' -f $uproject),
        "/PhysicsContactCloth/Maps/L_GpuExplicitAssassin",
        "-game", "-windowed", "-ResX=1600", "-ResY=900",
        "-UseFixedTimeStep", "-FPS=60", "-PhysicsClothExplicitReplay",
        "-PhysicsClothExplicitAutoQuit", "-NoSplash", "-NoP4", "-d3d12", "-sm6"
    )
    if ($ExpectedHash) { $arguments += "-PhysicsClothExplicitExpectedInputHash=$ExpectedHash" }
    if ($Capture) { $arguments += "-PhysicsClothExplicitCapture" }
    $startedAt = Get-Date
    $owned = Start-Process -FilePath $UnrealEditor -ArgumentList ($arguments -join " ") -PassThru
    Wait-Process -Id $owned.Id -Timeout 55 -ErrorAction SilentlyContinue
    if (Get-Process -Id $owned.Id -ErrorAction SilentlyContinue) {
        if ($owned.Path -eq $UnrealEditor) { Stop-Process -Id $owned.Id -Force }
        throw "重放 UE 进程在 55 秒内未退出：PID=$($owned.Id)"
    }
    if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) { throw "重放未生成运行报告：$runtimePath" }
    $runtimeFile = Get-Item -LiteralPath $runtimePath
    if ($runtimeFile.LastWriteTime -lt $startedAt) { throw "重放运行报告不是本轮生成：$runtimePath" }
    return Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
}

$first = Invoke-ReplayRun "" $false
Copy-Item -LiteralPath $runtimePath -Destination $firstRuntimePath -Force
if ($first.result -ne "passed" -or $first.replay_mode -ne $true -or $first.replay_input_hash -notmatch "^[0-9a-f]{16}$") {
    throw "第一次确定性输入重放未通过：$runtimePath"
}
$second = Invoke-ReplayRun ([string]$first.replay_input_hash) $true
Copy-Item -LiteralPath $runtimePath -Destination $verifiedRuntimePath -Force
Copy-Item -LiteralPath $comparisonCsvPath -Destination $verifiedComparisonCsvPath -Force
Copy-Item -LiteralPath $comparisonJsonPath -Destination $verifiedComparisonJsonPath -Force
Copy-Item -LiteralPath $timingCsvPath -Destination $verifiedTimingCsvPath -Force
$lastCharacter = $first.replay_input_hash.Substring(15, 1)
$mismatchHash = $first.replay_input_hash.Substring(0, 15) + $(if ($lastCharacter -eq "0") { "1" } else { "0" })
$mismatch = Invoke-ReplayRun $mismatchHash $false
Copy-Item -LiteralPath $runtimePath -Destination $mismatchRuntimePath -Force
Copy-Item -LiteralPath $verifiedRuntimePath -Destination $runtimePath -Force
Copy-Item -LiteralPath $verifiedComparisonCsvPath -Destination $comparisonCsvPath -Force
Copy-Item -LiteralPath $verifiedComparisonJsonPath -Destination $comparisonJsonPath -Force
Copy-Item -LiteralPath $verifiedTimingCsvPath -Destination $timingCsvPath -Force
$comparisonRows = Import-Csv -LiteralPath $comparisonCsvPath -Encoding UTF8
$timingRows = Import-Csv -LiteralPath $timingCsvPath -Encoding UTF8
$timingValues = @($timingRows | ForEach-Object { [double]$_.gpu_step_ms } | Sort-Object)
$timingMean = ($timingValues | Measure-Object -Average).Average
$timingP95 = $timingValues[[Math]::Ceiling(0.95 * $timingValues.Count) - 1]
$timingP99 = $timingValues[[Math]::Ceiling(0.99 * $timingValues.Count) - 1]
$timingMaximum = $timingValues[-1]
$steadyTimingValues = @($timingRows | Where-Object { [int]$_.step -gt [int]$second.gpu_timing_warmup_steps } | ForEach-Object { [double]$_.gpu_step_ms } | Sort-Object)
$steadyTimingMean = ($steadyTimingValues | Measure-Object -Average).Average
$steadyTimingP95 = $steadyTimingValues[[Math]::Ceiling(0.95 * $steadyTimingValues.Count) - 1]
$steadyTimingP99 = $steadyTimingValues[[Math]::Ceiling(0.99 * $steadyTimingValues.Count) - 1]
$steadyTimingMaximum = $steadyTimingValues[-1]
$timingSummaryMatches = [Math]::Abs($timingMean - [double]$second.mean_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($timingP95 - [double]$second.p95_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($timingP99 - [double]$second.p99_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($timingMaximum - [double]$second.maximum_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($steadyTimingMean - [double]$second.mean_steady_state_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($steadyTimingP95 - [double]$second.p95_steady_state_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($steadyTimingP99 - [double]$second.p99_steady_state_gpu_step_ms) -lt 0.000001 `
    -and [Math]::Abs($steadyTimingMaximum - [double]$second.maximum_steady_state_gpu_step_ms) -lt 0.000001
$screenshot = if (Test-Path -LiteralPath $screenshotPath -PathType Leaf) { Get-Item -LiteralPath $screenshotPath } else { $null }
$exactResearchFields = @(
    "animation_target_motion_cm",
    "fixed_target_error_cm",
    "average_dynamic_displacement_cm",
    "maximum_edge_error_cm",
    "maximum_collision_penetration_cm"
)
$researchOutputsMatch = $true
foreach ($field in $exactResearchFields) {
    if ([Math]::Abs([double]$first.$field - [double]$second.$field) -gt 0.000000001) {
        $researchOutputsMatch = $false
    }
}
$checks = [ordered]@{
    both_runs_passed = ($first.result -eq "passed" -and $second.result -eq "passed")
    replay_mode_enabled = ($first.replay_mode -eq $true -and $second.replay_mode -eq $true)
    fixed_sixty_hz_contract = ($first.fixed_frame_contract_active -eq $true -and $second.fixed_frame_contract_active -eq $true)
    exact_three_hundred_step_inputs = ([int]$first.target_applications -eq 300 -and [int]$second.target_applications -eq 300 -and [int]$first.collision_applications -eq 300 -and [int]$second.collision_applications -eq 300)
    input_hash_repeated = ($first.replay_input_hash -eq $second.replay_input_hash)
    expected_hash_enforced = ($second.replay_expected_input_hash -eq $first.replay_input_hash -and $second.replay_hash_matched -eq $true)
    mismatched_hash_rejected = ($mismatch.result -eq "failed" -and $mismatch.replay_expected_input_hash -eq $mismatchHash -and $mismatch.replay_hash_matched -eq $false)
    input_hash_scope_declared = ($second.replay_hash_scope -like "step-indexed UE Clothing skinned targets*")
    research_outputs_repeated = $researchOutputsMatch
    paired_curve_complete = (@($comparisonRows).Count -eq 75 -and [int]$comparisonRows[0].step -eq 4 -and [int]$comparisonRows[-1].step -eq 300)
    gpu_curve_complete = (@($timingRows).Count -eq 300 -and [int]$timingRows[0].step -eq 1 -and [int]$timingRows[-1].step -eq 300)
    verified_curve_files_restored = ((Get-FileHash -LiteralPath $comparisonCsvPath).Hash -eq (Get-FileHash -LiteralPath $verifiedComparisonCsvPath).Hash -and (Get-FileHash -LiteralPath $comparisonJsonPath).Hash -eq (Get-FileHash -LiteralPath $verifiedComparisonJsonPath).Hash -and (Get-FileHash -LiteralPath $timingCsvPath).Hash -eq (Get-FileHash -LiteralPath $verifiedTimingCsvPath).Hash)
    gpu_curve_matches_verified_summary = $timingSummaryMatches
    replay_screenshot_present = ($null -ne $screenshot -and $screenshot.Length -gt 100000)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        replay_input_hash = $second.replay_input_hash
        expected_hash = $second.replay_expected_input_hash
        hash_scope = $second.replay_hash_scope
        first_runtime = $firstRuntimePath
        verified_runtime = $verifiedRuntimePath
        mismatch_runtime = $mismatchRuntimePath
        restored_runtime = $runtimePath
        comparison_csv = $comparisonCsvPath
        comparison_json = $comparisonJsonPath
        gpu_timing_csv = $timingCsvPath
        screenshot = $screenshotPath
        research_output_fields = $exactResearchFields
        scope = "两次独立 UE 5.4 D3D12 进程重置同一 Assassin 动画；哈希覆盖逐步 UE Clothing 蒙皮目标及组件空间 Physics Asset 球/胶囊。Chaos 输出与 GPU 时间不要求逐位一致。"
    }
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $validationPath -Encoding UTF8
if ($report.result -ne "passed") { throw "确定性输入重放验证失败：$validationPath" }
Write-Output "M2_GPU_EXPLICIT_REPLAY_VALIDATION_PASSED hash=$($second.replay_input_hash) report=$validationPath"
