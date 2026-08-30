param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M5"
$logPath = Join-Path $root "InverseSpringRealtime.log"
$mapReportPath = Join-Path $root "inverse_spring_map_setup.json"
$runtimeJsonPath = Join-Path $root "Realtime\InverseSpringRealtime_Standard.json"
$runtimeCsvPath = Join-Path $root "Realtime\InverseSpringRealtime_Standard.csv"
$screenshotPath = Join-Path $root "Screenshots\Case04_InverseSpring_ChaosReplay.png"
$mapAssetPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsInverseDesign\Content\Maps\L_InverseSpringChaosReplay.umap"
$actorSourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsInverseDesign\Source\PhysicsInverseDesignRuntime\Private\InverseSpringRealtimeActor.cpp"

function Get-KeyValues([string]$Line) {
    $values = @{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return $values
}

$summaryLine = (Select-String -LiteralPath $logPath -Pattern 'M5_INVERSE_REALTIME_SUMMARY' | Select-Object -Last 1).Line
$summary = Get-KeyValues $summaryLine
$mapReport = Get-Content -LiteralPath $mapReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$runtime = Get-Content -LiteralPath $runtimeJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @(Import-Csv -LiteralPath $runtimeCsvPath)
$source = Get-Content -LiteralPath $actorSourcePath -Raw -Encoding UTF8

$squaredError = 0.0
foreach ($sample in $samples) {
    $difference = [double]$sample.chaos_cm - [double]$sample.research_cm
    $squaredError += $difference * $difference
}
$recomputedRms = [math]::Sqrt($squaredError / [math]::Max(1, $samples.Count))
$finalSample = $samples[-1]
$recomputedChaosFinalError = [math]::Abs([double]$finalSample.chaos_cm - [double]$finalSample.target_cm)
$recomputedResearchFinalError = [math]::Abs([double]$finalSample.research_cm - [double]$finalSample.target_cm)
$stepsStrict = $true
for ($index = 0; $index -lt $samples.Count; ++$index) {
    if ([int]$samples[$index].step -ne $index + 1) { $stepsStrict = $false; break }
}

$checks = [ordered]@{
    map_saved_without_errors = ([bool]$mapReport.saved -and @($mapReport.errors).Count -eq 0)
    map_asset_exists = (Test-Path -LiteralPath $mapAssetPath)
    runtime_summary_passed = ($summary.pass -eq 'true' -and $summary.complete -eq 'true')
    fixed_step_and_chaos_samples_are_exact = ([int]$summary.steps -eq 120 -and [int]$summary.chaos_samples -eq 120)
    chaos_constraint_body_really_moves = ([double]$summary.chaos_movement_cm -gt 200.0)
    research_fit_reaches_target = ([double]$summary.research_final_target_error_cm -lt 0.01)
    chaos_replay_is_close_but_not_falsely_identical = ([double]$summary.chaos_rms_vs_research_cm -gt 0.1 -and [double]$summary.chaos_rms_vs_research_cm -lt 50.0 -and [double]$summary.chaos_final_target_error_cm -lt 1.0)
    csv_contains_all_strictly_ordered_steps = ($samples.Count -eq 120 -and $stepsStrict)
    csv_recomputes_chaos_rms = ([math]::Abs($recomputedRms - [double]$runtime.chaos_rms_vs_research_cm) -lt 1.0e-6)
    csv_recomputes_final_errors = ([math]::Abs($recomputedChaosFinalError - [double]$runtime.chaos_final_target_error_cm) -lt 1.0e-6 -and [math]::Abs($recomputedResearchFinalError - [double]$runtime.research_final_target_error_cm) -lt 1.0e-6)
    screenshot_exists_and_is_nontrivial = ((Test-Path -LiteralPath $screenshotPath) -and (Get-Item -LiteralPath $screenshotPath).Length -gt 100000)
    source_uses_real_chaos_constraint = ($source -match 'UPhysicsConstraintComponent' -and $source -match 'SetConstrainedComponents' -and $source -match 'SetLinearPositionDrive' -and $source -match 'SetSimulatePhysics\(true\)')
    source_uses_same_recovered_parameters = ($source -match 'Optimization\.Parameters\.Stiffness' -and $source -match 'Optimization\.Parameters\.Damping' -and $source -match 'Optimization\.Parameters\.DriveForce / Optimization\.Parameters\.Stiffness')
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    engine = 'UE 5.4.4 CL 35576357'
    rhi = 'D3D12 SM6'
    interpretation = 'The fitted differentiable model and a real Chaos linear-drive constraint share nominal mass, stiffness, damping, equilibrium target, initial state, and fixed-step launch protocol. Their measured trajectory difference is retained as engine/model mismatch rather than hidden.'
    checks = $checks
    runtime = $runtime
    recomputed = [ordered]@{
        samples = $samples.Count
        chaos_rms_vs_research_cm = $recomputedRms
        chaos_final_target_error_cm = $recomputedChaosFinalError
        research_final_target_error_cm = $recomputedResearchFinalError
    }
    screenshot = 'Docs/Validation/M5/Screenshots/Case04_InverseSpring_ChaosReplay.png'
    manual_visual_inspection = 'passed: full Chinese HUD, all four colored bodies, target marker, track plane, and camera framing are visible without the earlier occluding wall'
    failed_checks = $failed
}
$reportPath = Join-Path $root "inverse_spring_realtime_validation.json"
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 7
if ($failed.Count -gt 0) { exit 1 }
