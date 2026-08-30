param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.4",
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
$editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$project = Join-Path $ProjectRoot "SIGGRAPHPhysicsLab.uproject"
$logPath = Join-Path $ProjectRoot "Docs\Validation\M4\FlowMapBackendBudgetInteraction.log"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_backend_budget_interaction_validation.json"

function Convert-KeyValueLine([string]$line) {
    $values = @{}
    foreach ($match in [regex]::Matches($line, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return $values
}

$existingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | ForEach-Object Id)
$argumentLine = '"' + $project + '" /PhysicsFlowMap/Maps/L_FlowMapChaosObstacle -game -RenderOffscreen -unattended -d3d12 -sm6 -nosplash -NoSound -NoLoadingScreen -UseFixedTimeStep -FPS=60 -PhysicsResearchFlowMapBackendBudgetScript "-abslog=' + $logPath + '"'
$process = Start-Process -FilePath $editor -ArgumentList $argumentLine -PassThru -WindowStyle Hidden
$timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
if ($timedOut) {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    $process.WaitForExit()
}

$startedLine = ""
$completeLine = ""
$summaryLine = ""
$criticalErrors = @()
if (Test-Path -LiteralPath $logPath) {
    $startedLine = (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_REALTIME_STARTED' | Select-Object -Last 1).Line
    $completeLine = (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_BACKEND_BUDGET_INTERACTION_COMPLETE' | Select-Object -Last 1).Line
    $summaryLine = (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_REALTIME_SUMMARY' | Select-Object -Last 1).Line
    $criticalErrors = @(Select-String -LiteralPath $logPath -Pattern 'Ensure condition failed|GPU Crashed|Shader compilation failures|M4_FLOWMAP_REALTIME_SNAPSHOT_REJECTED|M4_FLOWMAP_REALTIME_STEP_FAILED' | ForEach-Object Line)
}

$started = Convert-KeyValueLine $startedLine
$complete = Convert-KeyValueLine $completeLine
$summary = Convert-KeyValueLine $summaryLine
$remainingExistingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $existingPids -contains $_.Id } | ForEach-Object Id)
$checks = [ordered]@{
    process_exited_without_timeout = -not $timedOut
    process_exit_code_is_zero = (-not $timedOut -and $process.ExitCode -eq 0)
    starts_as_pfm_medium = ($started.backend -eq 'ParticleFlowMapResearch' -and $started.budget -eq '中预算')
    scripted_interaction_completed = -not [string]::IsNullOrWhiteSpace($completeLine)
    two_real_backend_rebuilds_executed = ([int]$complete.backend_toggles -eq 2 -and [int]$summary.backend_toggles -eq 2)
    one_real_budget_rebuild_executed = ([int]$complete.budget_toggles -eq 1 -and [int]$summary.budget_toggles -eq 1)
    finishes_as_pfm_high = ($complete.final_backend -eq 'ParticleFlowMapResearch' -and $complete.final_budget -eq '高预算' -and $summary.backend -eq 'ParticleFlowMapResearch' -and $summary.budget -eq '高预算')
    final_session_reaches_runtime_gate = ($summary.pass -eq 'true' -and [int64]$summary.steps -ge 120)
    final_chaos_boundary_matches_every_step = ([int64]$summary.boundary_applied -eq [int64]$summary.steps -and [int64]$summary.boundary_version -eq [int64]$summary.steps)
    all_three_rebuilds_are_counted = ([int]$summary.reset -eq 3 -and [int]$summary.backend_budget_phase -eq 4)
    no_snapshot_was_rejected = ([int64]$summary.rejected -eq 0)
    final_gpu_distribution_is_available = ([int64]$summary.timing_samples -ge 80 -and [double]$summary.p95_ms -gt 0.0 -and [double]$summary.p99_ms -gt 0.0)
    no_critical_runtime_error = ($criticalErrors.Count -eq 0)
    preexisting_processes_preserved = (@($existingPids | Where-Object { $remainingExistingPids -notcontains $_ }).Count -eq 0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = if ($failed.Count -eq 0) { 'passed' } else { 'failed' }
    engine = 'UE 5.4.4 CL 35576357'
    rhi = 'D3D12 SM6'
    pid = $process.Id
    log = 'Docs/Validation/M4/FlowMapBackendBudgetInteraction.log'
    checks = $checks
    failed_checks = $failed
    started = $started
    interaction_complete = $complete
    summary = $summary
    critical_errors = $criticalErrors
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
