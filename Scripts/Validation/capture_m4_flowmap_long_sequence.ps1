param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.4",
    [int]$Steps = 1200,
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
$editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$project = Join-Path $ProjectRoot "SIGGRAPHPhysicsLab.uproject"
$longDirectory = Join-Path $ProjectRoot "Docs\Validation\M4\LongSequence"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_long_sequence_validation.json"
New-Item -ItemType Directory -Path $longDirectory -Force | Out-Null

$profiles = @(
    [ordered]@{ backend = 'ShortMapBaseline'; short = $true;  expected_resets = $Steps },
    [ordered]@{ backend = 'ParticleFlowMapResearch'; short = $false; expected_resets = [math]::Floor($Steps / 8) }
)
$existingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | ForEach-Object Id)
$runs = @()

foreach ($profile in $profiles) {
    $baseName = "FlowMapLongSequence_$($profile.backend)_Medium"
    $logPath = Join-Path $longDirectory "$baseName.log"
    $csvPath = Join-Path $longDirectory "$baseName.csv"
    $jsonPath = Join-Path $longDirectory "$baseName.json"
    $shortArgument = if ($profile.short) { ' -PhysicsResearchFlowMapShortMap' } else { '' }
    $argumentLine = '"' + $project + '" /PhysicsFlowMap/Maps/L_FlowMapChaosObstacle -game -RenderOffscreen -unattended -d3d12 -sm6 -nosplash -NoSound -NoLoadingScreen -UseFixedTimeStep -FPS=60' +
        $shortArgument + " -PhysicsResearchFlowMapBudget=Medium -PhysicsResearchFlowMapLongSequence -PhysicsResearchFlowMapAutoExitSteps=$Steps `"-abslog=$logPath`""
    $process = Start-Process -FilePath $editor -ArgumentList $argumentLine -PassThru -WindowStyle Hidden
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        $process.WaitForExit()
    }

    $data = if (Test-Path -LiteralPath $jsonPath) { Get-Content -LiteralPath $jsonPath -Raw -Encoding UTF8 | ConvertFrom-Json } else { $null }
    $rows = if (Test-Path -LiteralPath $csvPath) { @(Import-Csv -LiteralPath $csvPath) } else { @() }
    $steadyRows = @($rows | Where-Object { $_.steady_state -eq 'true' })
    $steadyValues = @($steadyRows | ForEach-Object { [double]$_.gpu_step_ms })
    $sorted = @($steadyValues | Sort-Object)
    $mean = if ($steadyValues.Count -gt 0) { ($steadyValues | Measure-Object -Average).Average } else { 0.0 }
    $p95 = if ($sorted.Count -gt 0) { $sorted[[math]::Ceiling(0.95 * $sorted.Count) - 1] } else { 0.0 }
    $p99 = if ($sorted.Count -gt 0) { $sorted[[math]::Ceiling(0.99 * $sorted.Count) - 1] } else { 0.0 }
    $maximum = if ($sorted.Count -gt 0) { $sorted[-1] } else { 0.0 }
    $strictlyIncreasing = $true
    for ($index = 1; $index -lt $rows.Count; $index++) {
        if ([int64]$rows[$index].step -le [int64]$rows[$index - 1].step) { $strictlyIncreasing = $false; break }
    }
    $criticalErrors = if (Test-Path -LiteralPath $logPath) { @(Select-String -LiteralPath $logPath -Pattern 'Ensure condition failed|GPU Crashed|Shader compilation failures|M4_FLOWMAP_REALTIME_SNAPSHOT_REJECTED|M4_FLOWMAP_LONG_SEQUENCE_EXPORT_FAILED' | ForEach-Object Line) } else { @('log missing') }
    $checks = [ordered]@{
        process_exited_without_timeout = -not $timedOut
        process_exit_code_is_zero = (-not $timedOut -and $process.ExitCode -eq 0)
        csv_and_json_exist = ((Test-Path -LiteralPath $csvPath) -and (Test-Path -LiteralPath $jsonPath))
        backend_and_budget_are_exact = ($null -ne $data -and $data.backend -eq $profile.backend -and $data.budget -eq 'Medium')
        at_least_1200_steps_executed = ($null -ne $data -and [int64]$data.steps_executed -ge $Steps)
        chaos_boundary_applied_each_step = ($null -ne $data -and [int64]$data.solid_boundary_applications -eq [int64]$data.steps_executed)
        at_least_1100_raw_samples = ($rows.Count -ge 1100 -and [int]$data.samples -eq $rows.Count)
        at_least_1000_steady_samples = ($steadyRows.Count -ge 1000 -and [int]$data.steady_samples -eq $steadyRows.Count)
        timestamp_steps_are_strictly_increasing = $strictlyIncreasing
        no_timing_samples_skipped = ([int64]$data.timing_samples_skipped -eq 0)
        no_snapshot_was_rejected = ([int64]$data.rejected_snapshots -eq 0)
        map_reset_count_matches_backend = ([int64]$data.map_resets -ge [int64]$profile.expected_resets)
        mean_recomputes_from_csv = ([math]::Abs($mean - [double]$data.steady_mean_ms) -lt 0.000001)
        p95_recomputes_from_csv = ([math]::Abs($p95 - [double]$data.steady_p95_ms) -lt 0.000001)
        p99_recomputes_from_csv = ([math]::Abs($p99 - [double]$data.steady_p99_ms) -lt 0.000001)
        maximum_recomputes_from_csv = ([math]::Abs($maximum - [double]$data.steady_max_ms) -lt 0.000001)
        no_critical_runtime_error = ($criticalErrors.Count -eq 0)
    }
    $failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
    $runs += [ordered]@{
        backend = $profile.backend
        pid = $process.Id
        passed = ($failed.Count -eq 0)
        csv = "Docs/Validation/M4/LongSequence/$baseName.csv"
        json = "Docs/Validation/M4/LongSequence/$baseName.json"
        checks = $checks
        failed_checks = $failed
        recomputed = [ordered]@{ samples = $rows.Count; steady_samples = $steadyRows.Count; mean_ms = $mean; p95_ms = $p95; p99_ms = $p99; maximum_ms = $maximum }
        runtime = [ordered]@{
            steps_executed = $data.steps_executed
            solid_boundary_applications = $data.solid_boundary_applications
            map_resets = $data.map_resets
            samples = $data.samples
            timing_samples_skipped = $data.timing_samples_skipped
            steady_samples = $data.steady_samples
            steady_mean_ms = $data.steady_mean_ms
            steady_p95_ms = $data.steady_p95_ms
            steady_p99_ms = $data.steady_p99_ms
            steady_max_ms = $data.steady_max_ms
            rejected_snapshots = $data.rejected_snapshots
            latest_max_velocity_cm_per_second = $data.latest_max_velocity_cm_per_second
            latest_pressure_minimum = $data.latest_pressure_minimum
            latest_pressure_maximum = $data.latest_pressure_maximum
        }
        critical_errors = $criticalErrors
    }
}

$remainingExistingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $existingPids -contains $_.Id } | ForEach-Object Id)
$allPassed = @($runs | Where-Object { -not $_.passed }).Count -eq 0
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = if ($allPassed) { 'passed' } else { 'failed' }
    engine = 'UE 5.4.4 CL 35576357'
    rhi = 'D3D12 SM6'
    contract = [ordered]@{ steps_per_backend = $Steps; budget = 'Medium'; cells = '24x24'; physical_domain_cm = @(240, 240); live_chaos_boundary = $true; warmup_steps = 16 }
    runs = $runs
    preexisting_processes_preserved = (@($existingPids | Where-Object { $remainingExistingPids -notcontains $_ }).Count -eq 0)
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 6
if (-not $allPassed -or -not $report.preexisting_processes_preserved) { exit 1 }
