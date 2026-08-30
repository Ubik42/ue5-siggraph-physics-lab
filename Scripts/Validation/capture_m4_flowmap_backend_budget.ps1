param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.4",
    [int]$Steps = 120,
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = "Stop"
$editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$project = Join-Path $ProjectRoot "SIGGRAPHPhysicsLab.uproject"
$outputDirectory = Join-Path $ProjectRoot "Docs\Validation\M4\BackendBudget"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_backend_budget_validation.json"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$profiles = @(
    [ordered]@{ backend = "ShortMapBaseline"; short = $true;  budget = "Low";    budget_cn = "低预算"; cells = 16; particles = 256;  arrows = 64;  cell_size = 15.0; pressure = 32; reset = 1; gradient = $false },
    [ordered]@{ backend = "ShortMapBaseline"; short = $true;  budget = "Medium"; budget_cn = "中预算"; cells = 24; particles = 576;  arrows = 144; cell_size = 10.0; pressure = 64; reset = 1; gradient = $false },
    [ordered]@{ backend = "ShortMapBaseline"; short = $true;  budget = "High";   budget_cn = "高预算"; cells = 32; particles = 1024; arrows = 256; cell_size = 7.5;  pressure = 96; reset = 1; gradient = $false },
    [ordered]@{ backend = "ParticleFlowMapResearch"; short = $false; budget = "Low";    budget_cn = "低预算"; cells = 16; particles = 256;  arrows = 64;  cell_size = 15.0; pressure = 32; reset = 8; gradient = $true },
    [ordered]@{ backend = "ParticleFlowMapResearch"; short = $false; budget = "Medium"; budget_cn = "中预算"; cells = 24; particles = 576;  arrows = 144; cell_size = 10.0; pressure = 64; reset = 8; gradient = $true },
    [ordered]@{ backend = "ParticleFlowMapResearch"; short = $false; budget = "High";   budget_cn = "高预算"; cells = 32; particles = 1024; arrows = 256; cell_size = 7.5;  pressure = 96; reset = 8; gradient = $true }
)

function Convert-KeyValueLine([string]$line) {
    $values = @{}
    foreach ($match in [regex]::Matches($line, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return $values
}

$existingEditorPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | ForEach-Object Id)
$runs = @()

foreach ($profile in $profiles) {
    $name = "$($profile.backend)_$($profile.budget)"
    $logPath = Join-Path $outputDirectory "$name.log"
    $shortArgument = if ($profile.short) { " -PhysicsResearchFlowMapShortMap" } else { "" }
    $argumentLine = '"' + $project + '" /PhysicsFlowMap/Maps/L_FlowMapChaosObstacle -game -RenderOffscreen -unattended -d3d12 -sm6 -nosplash -NoSound -NoLoadingScreen -UseFixedTimeStep -FPS=60' +
        $shortArgument + " -PhysicsResearchFlowMapBudget=$($profile.budget) -PhysicsResearchFlowMapAutoExitSteps=$Steps `"-abslog=$logPath`""

    $process = Start-Process -FilePath $editor -ArgumentList $argumentLine -PassThru -WindowStyle Hidden
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        $process.WaitForExit()
    }

    $startedLine = ""
    $summaryLine = ""
    $criticalErrors = @()
    if (Test-Path -LiteralPath $logPath) {
        $startedLine = (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_REALTIME_STARTED' | Select-Object -Last 1).Line
        $summaryLine = (Select-String -LiteralPath $logPath -Pattern 'M4_FLOWMAP_REALTIME_SUMMARY' | Select-Object -Last 1).Line
        $criticalErrors = @(Select-String -LiteralPath $logPath -Pattern 'Ensure condition failed|GPU Crashed|Shader compilation failures|M4_FLOWMAP_REALTIME_SNAPSHOT_REJECTED' | ForEach-Object Line)
    }

    $started = Convert-KeyValueLine $startedLine
    $summary = Convert-KeyValueLine $summaryLine
    $expectedMinimumResets = if ($profile.short) { $Steps } else { [math]::Floor($Steps / 8) }
    $checks = [ordered]@{
        process_exited_without_timeout = -not $timedOut
        process_exit_code_is_zero = (-not $timedOut -and $process.ExitCode -eq 0)
        started_marker_present = -not [string]::IsNullOrWhiteSpace($startedLine)
        summary_marker_present = -not [string]::IsNullOrWhiteSpace($summaryLine)
        backend_is_exact = ($started.backend -eq $profile.backend -and $summary.backend -eq $profile.backend)
        budget_is_exact = ($started.budget -eq $profile.budget_cn -and $summary.budget -eq $profile.budget_cn)
        grid_and_visual_counts_are_exact = ([int]$started.cells.Split('x')[0] -eq $profile.cells -and [int]$started.particles -eq $profile.particles -and [int]$started.arrows -eq $profile.arrows)
        same_physical_domain_is_preserved = ([math]::Abs(([double]$started.cell_size_cm * [int]$started.cells.Split('x')[0]) - 240.0) -lt 0.001)
        pressure_budget_is_exact = ([int]$started.pressure_iterations -eq $profile.pressure)
        map_reset_contract_is_exact = ([int]$started.map_reset_steps -eq $profile.reset)
        gradient_contract_is_exact = ([bool]::Parse($started.gradient) -eq $profile.gradient)
        runtime_gate_passed = ($summary.pass -eq 'true')
        full_steps_and_boundary_versions_match = ([int64]$summary.steps -ge $Steps -and [int64]$summary.boundary_applied -eq [int64]$summary.steps -and [int64]$summary.boundary_version -eq [int64]$summary.steps)
        no_snapshot_was_rejected = ([int64]$summary.rejected -eq 0)
        timing_distribution_is_available = ([int64]$summary.timing_samples -ge 80 -and [double]$summary.steady_mean_ms -gt 0.0 -and [double]$summary.p95_ms -gt 0.0 -and [double]$summary.p99_ms -gt 0.0)
        observed_map_resets_match_backend = ([int64]$summary.map_resets -ge $expectedMinimumResets)
        no_critical_runtime_error = ($criticalErrors.Count -eq 0)
    }
    $failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
    $runs += [ordered]@{
        name = $name
        pid = $process.Id
        log = "Docs/Validation/M4/BackendBudget/$name.log"
        passed = ($failed.Count -eq 0)
        checks = $checks
        failed_checks = $failed
        started = $started
        summary = $summary
        critical_errors = $criticalErrors
    }
}

$remainingExistingPids = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $existingEditorPids -contains $_.Id } | ForEach-Object Id)
$allPassed = @($runs | Where-Object { -not $_.passed }).Count -eq 0
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = if ($allPassed) { 'passed' } else { 'failed' }
    engine = 'UE 5.4.4 CL 35576357'
    rhi = 'D3D12 SM6'
    shared_contract = [ordered]@{ physical_domain_cm = @(240, 240); fixed_step_seconds = (1.0 / 60.0); steps_per_run = $Steps; live_chaos_boundary = $true }
    runs = $runs
    preexisting_unreal_editor_pids = $existingEditorPids
    surviving_preexisting_unreal_editor_pids = $remainingExistingPids
    preexisting_processes_preserved = (@($existingEditorPids | Where-Object { $remainingExistingPids -notcontains $_ }).Count -eq 0)
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
if (-not $allPassed -or -not $report.preexisting_processes_preserved) { exit 1 }
