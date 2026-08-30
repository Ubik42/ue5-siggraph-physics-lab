param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.4",
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
$editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$project = Join-Path $ProjectRoot "SIGGRAPHPhysicsLab.uproject"
$logDirectory = Join-Path $ProjectRoot "Docs\Validation\M4\ScreenshotStates"
$screenshotDirectory = Join-Path $ProjectRoot "Docs\Validation\M4\Screenshots"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_portfolio_states_validation.json"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

$profiles = @(
    [ordered]@{ name = 'ShortMapBaseline_Medium'; arguments = '-PhysicsResearchFlowMapShortMap -PhysicsResearchFlowMapBudget=Medium'; screenshot = 'Case03_FlowMap_ShortMapBaseline_Medium.png'; expected_pass = 'true'; expected_backend = 'ShortMapBaseline'; expected_budget = '中预算'; expected_ablation = 'false' },
    [ordered]@{ name = 'ParticleFlowMapResearch_High'; arguments = '-PhysicsResearchFlowMapBudget=High -PhysicsResearchFlowMapCaptureStep=450'; screenshot = 'Case03_FlowMap_ParticleFlowMapResearch_High.png'; expected_pass = 'true'; expected_backend = 'ParticleFlowMapResearch'; expected_budget = '高预算'; expected_ablation = 'false' },
    [ordered]@{ name = 'FailureVelocityBudget'; arguments = '-PhysicsResearchFlowMapVelocityBudgetAblation'; screenshot = 'Case03_FlowMap_FailureVelocityBudget.png'; expected_pass = 'false'; expected_backend = 'ParticleFlowMapResearch'; expected_budget = '中预算'; expected_ablation = 'true' }
)

function Convert-KeyValueLine([string]$line) {
    $values = @{}
    foreach ($match in [regex]::Matches($line, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return $values
}

$existingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | ForEach-Object Id)
$runs = @()
foreach ($profile in $profiles) {
    $logPath = Join-Path $logDirectory "$($profile.name).log"
    $screenshotPath = Join-Path $screenshotDirectory $profile.screenshot
    $autoExitSteps = if ($profile.name -eq 'ParticleFlowMapResearch_High') { 520 } else { 360 }
    $argumentLine = '"' + $project + '" /PhysicsFlowMap/Maps/L_FlowMapChaosObstacle -game -RenderOffscreen -unattended -ResX=1920 -ResY=1080 -ForceRes -d3d12 -sm6 -nosplash -NoSound -NoLoadingScreen -UseFixedTimeStep -FPS=60 -PhysicsResearchFlowMapCapture -PhysicsResearchFlowMapAutoExitSteps=' + $autoExitSteps + ' ' + $profile.arguments + ' "-abslog=' + $logPath + '"'
    $process = Start-Process -FilePath $editor -ArgumentList $argumentLine -PassThru -WindowStyle Hidden
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue; $process.WaitForExit() }
    $requestLine = if (Test-Path $logPath) { (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_REALTIME_SCREENSHOT_REQUESTED' | Select-Object -Last 1).Line } else { '' }
    $summaryLine = if (Test-Path $logPath) { (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_REALTIME_SUMMARY' | Select-Object -Last 1).Line } else { '' }
    $request = Convert-KeyValueLine $requestLine
    $summary = Convert-KeyValueLine $summaryLine
    $criticalErrors = if (Test-Path $logPath) { @(Select-String -LiteralPath $logPath -Pattern 'Ensure condition failed|GPU Crashed|Shader compilation failures|M4_FLOWMAP_REALTIME_SNAPSHOT_REJECTED' | ForEach-Object Line) } else { @('log missing') }
    $checks = [ordered]@{
        process_exited_without_timeout = -not $timedOut
        process_exit_code_is_zero = (-not $timedOut -and $process.ExitCode -eq 0)
        screenshot_request_marker_present = -not [string]::IsNullOrWhiteSpace($requestLine)
        screenshot_file_is_nontrivial = ((Test-Path $screenshotPath) -and (Get-Item $screenshotPath).Length -gt 100000)
        screenshot_state_matches_profile = ($request.backend -eq $profile.expected_backend -and (($profile.name -eq 'ParticleFlowMapResearch_High' -and $request.budget -eq 'High') -or ($profile.name -eq 'ShortMapBaseline_Medium' -and $request.budget -eq 'Medium') -or $profile.name -eq 'FailureVelocityBudget'))
        runtime_result_matches_expected_state = ($summary.pass -eq $profile.expected_pass -and $summary.backend -eq $profile.expected_backend -and $summary.budget -eq $profile.expected_budget -and $summary.ablation -eq $profile.expected_ablation)
        at_least_300_snapshot_steps_completed = ([int64]$summary.snapshots_latest -ge 300)
        no_snapshot_was_rejected = ([int64]$summary.rejected -eq 0)
        no_critical_runtime_error = ($criticalErrors.Count -eq 0)
    }
    $failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
    $runs += [ordered]@{ name = $profile.name; pid = $process.Id; passed = ($failed.Count -eq 0); screenshot = "Docs/Validation/M4/Screenshots/$($profile.screenshot)"; screenshot_bytes = if (Test-Path $screenshotPath) { (Get-Item $screenshotPath).Length } else { 0 }; checks = $checks; failed_checks = $failed; request = $request; summary = $summary; critical_errors = $criticalErrors }
}

$remainingExistingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $existingPids -contains $_.Id } | ForEach-Object Id)
$allPassed = @($runs | Where-Object { -not $_.passed }).Count -eq 0
$report = [ordered]@{ generated_at = (Get-Date).ToString('o'); result = if ($allPassed) { 'passed' } else { 'failed' }; engine = 'UE 5.4.4 CL 35576357'; rhi = 'D3D12 SM6'; runs = $runs; preexisting_processes_preserved = (@($existingPids | Where-Object { $remainingExistingPids -notcontains $_ }).Count -eq 0) }
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
if (-not $allPassed -or -not $report.preexisting_processes_preserved) { exit 1 }
