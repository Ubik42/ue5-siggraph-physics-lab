param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$mapSetupPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_realtime_map_setup.json"
$runtimePath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_realtime_runtime.json"
$metricsPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_GpuRealtimeCloth.json"
$csvPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_GpuRealtimeCloth.csv"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M2\Screenshots\M2_GpuRealtimeCloth.png"
$mapPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsContactCloth\Content\Maps\L_GpuRealtimeCloth.umap"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_realtime_visual_validation.json"

foreach ($path in @($mapSetupPath, $runtimePath, $metricsPath, $csvPath, $screenshotPath, $mapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 GPU 逐帧可视化产物：$path"
    }
}

$mapSetup = Get-Content -LiteralPath $mapSetupPath -Raw -Encoding UTF8 | ConvertFrom-Json
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$metrics = Get-Content -LiteralPath $metricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @($metrics.samples)
$csv = @(Import-Csv -LiteralPath $csvPath -Encoding UTF8)
$actualMetrics = @($samples.metric | Sort-Object -Unique)
$expectedMetrics = @("ActiveContactPairs", "BottomDisplacement", "MaximumCapsulePenetration", "MaximumEdgeError", "MaximumNdbActivationCount", "ReadbackLagSteps", "RenderSubmissionTime")
$invalidValues = @($samples | Where-Object {
    $value = [double]$_.value
    [double]::IsNaN($value) -or [double]::IsInfinity($value)
})
$lagValues = @($samples | Where-Object { $_.metric -eq "ReadbackLagSteps" } | ForEach-Object { [double]$_.value })
$stepValues = @($samples | ForEach-Object { [int64]$_.step_index })
$activePairValues = @($samples | Where-Object { $_.metric -eq "ActiveContactPairs" } | ForEach-Object { [double]$_.value })
$activationValues = @($samples | Where-Object { $_.metric -eq "MaximumNdbActivationCount" } | ForEach-Object { [double]$_.value })
$submissionValues = @($samples | Where-Object { $_.metric -eq "RenderSubmissionTime" } | ForEach-Object { [double]$_.value })
function Get-Percentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [Math]::Min($sorted.Count - 1, [Math]::Ceiling($Percentile * $sorted.Count) - 1))
    return [double]$sorted[$index]
}

$checks = [ordered]@{
    map_saved = ([bool]$mapSetup.saved -and @($mapSetup.errors).Count -eq 0)
    expected_map = ($mapSetup.map -eq "/PhysicsContactCloth/Maps/L_GpuRealtimeCloth")
    runtime_passed = ($runtime.result -eq "passed")
    research_backend = ($metrics.backend -eq "ResearchSolver")
    particle_count = ([int]$runtime.particles -eq 300)
    iteration_budget = ([int]$runtime.iterations_per_step -eq 24)
    exact_fixed_steps = ([int64]$runtime.steps_executed -eq 300)
    exact_final_readback = ([int64]$runtime.latest_readback_step -eq 300)
    many_async_readbacks = ([int64]$runtime.readbacks_completed -ge 50)
    assassin_component_pose_source = ($runtime.collision_source -eq "Assassin animated Physics Asset component pose")
    exact_collision_spheres = ([int]$runtime.collision_spheres -eq 1)
    exact_collision_capsules = ([int]$runtime.collision_capsules -eq 16)
    pose_updated_each_fixed_step = ([int64]$runtime.pose_updates -ge 300)
    hybrid_gpu_solver = ($runtime.solver -eq "Persistent D3D12 reduced modal, rank-one NDB global contact, local Jacobi, complete CCD, and guarded residual forwarding")
    expected_modal_modes = ([int]$runtime.modal_modes -eq 12)
    modal_correction_each_fixed_step = ([int64]$runtime.modal_corrections_executed -eq 300)
    rank_one_collision_system_each_fixed_step = ([int64]$runtime.rank_one_collision_systems_executed -eq 300)
    ndb_state_update_each_fixed_step = ([int64]$runtime.ndb_state_updates_executed -eq 300)
    complete_ccd_each_fixed_step = ([int64]$runtime.complete_ccd_passes_executed -eq 300)
    collision_capacity_matches_pose = ([int]$runtime.collision_primitive_capacity -eq 17)
    residual_reuse_each_fixed_step = ([int64]$runtime.residual_reuse_passes_executed -eq 300)
    residual_metric_finite_and_bounded = ([double]$runtime.maximum_forwarded_residual_cm -ge 0.0 -and [double]$runtime.maximum_forwarded_residual_cm -le 2.001)
    residual_fallbacks_recorded = ([int64]$runtime.residual_fallback_events -ge 0)
    active_contact_distribution_present = ($activePairValues.Count -gt 0 -and ($activePairValues | Measure-Object -Maximum).Maximum -gt 0)
    ndb_activation_distribution_present = ($activationValues.Count -gt 0 -and ($activationValues | Measure-Object -Maximum).Maximum -gt 0)
    render_submission_distribution_present = ($submissionValues.Count -gt 0 -and ($submissionValues | Measure-Object -Minimum).Minimum -ge 0.0)
    timing_scope_is_cpu_submission = ($runtime.timing_scope -eq "CPU render-thread RDG construction and submission; not GPU kernel time")
    bounded_edge_error = ([double]$runtime.maximum_edge_error_cm -lt 8.0)
    no_capsule_penetration = ([double]$runtime.maximum_capsule_penetration_cm -lt 0.001)
    gravity_displacement = ([double]$runtime.bottom_displacement_cm -gt 1.0)
    finite_metrics = ($invalidValues.Count -eq 0)
    metric_names = (@(Compare-Object $expectedMetrics $actualMetrics).Count -eq 0)
    metric_counts_match = ($samples.Count -eq [int64]$runtime.readbacks_completed * $expectedMetrics.Count -and $csv.Count -eq $samples.Count)
    bounded_readback_lag = ($lagValues.Count -gt 0 -and ($lagValues | Measure-Object -Maximum).Maximum -le 8.0)
    final_metric_step = ($stepValues.Count -gt 0 -and ($stepValues | Measure-Object -Maximum).Maximum -eq 300)
    screenshot_nonempty = ((Get-Item -LiteralPath $screenshotPath).Length -gt 1024)
    map_asset_nonempty = ((Get-Item -LiteralPath $mapPath).Length -gt 1024)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = [int]$runtime.particles
        iterations_per_step = [int]$runtime.iterations_per_step
        steps_executed = [int64]$runtime.steps_executed
        readbacks_completed = [int64]$runtime.readbacks_completed
        latest_readback_step = [int64]$runtime.latest_readback_step
        collision_source = $runtime.collision_source
        pose_updates = [int64]$runtime.pose_updates
        collision_spheres = [int]$runtime.collision_spheres
        collision_capsules = [int]$runtime.collision_capsules
        modal_modes = [int]$runtime.modal_modes
        modal_corrections_executed = [int64]$runtime.modal_corrections_executed
        rank_one_collision_systems_executed = [int64]$runtime.rank_one_collision_systems_executed
        ndb_state_updates_executed = [int64]$runtime.ndb_state_updates_executed
        complete_ccd_passes_executed = [int64]$runtime.complete_ccd_passes_executed
        collision_primitive_capacity = [int]$runtime.collision_primitive_capacity
        residual_reuse_passes_executed = [int64]$runtime.residual_reuse_passes_executed
        residual_fallback_events = [int64]$runtime.residual_fallback_events
        maximum_forwarded_residual_cm = [double]$runtime.maximum_forwarded_residual_cm
        maximum_active_contact_pairs = [int]$runtime.maximum_active_contact_pairs
        maximum_ndb_activation_count = [int]$runtime.maximum_ndb_activation_count
        average_active_contact_pairs = if ($activePairValues.Count -gt 0) { ($activePairValues | Measure-Object -Average).Average } else { $null }
        p95_active_contact_pairs = Get-Percentile $activePairValues 0.95
        p99_active_contact_pairs = Get-Percentile $activePairValues 0.99
        average_maximum_ndb_activation = if ($activationValues.Count -gt 0) { ($activationValues | Measure-Object -Average).Average } else { $null }
        p95_maximum_ndb_activation = Get-Percentile $activationValues 0.95
        p99_maximum_ndb_activation = Get-Percentile $activationValues 0.99
        average_render_submission_ms = if ($submissionValues.Count -gt 0) { ($submissionValues | Measure-Object -Average).Average } else { $null }
        p95_render_submission_ms = Get-Percentile $submissionValues 0.95
        p99_render_submission_ms = Get-Percentile $submissionValues 0.99
        maximum_render_submission_ms = [double]$runtime.maximum_render_submission_ms
        timing_scope = $runtime.timing_scope
        maximum_edge_error_cm = [double]$runtime.maximum_edge_error_cm
        maximum_capsule_penetration_cm = [double]$runtime.maximum_capsule_penetration_cm
        bottom_displacement_cm = [double]$runtime.bottom_displacement_cm
        maximum_readback_lag_steps = ($lagValues | Measure-Object -Maximum).Maximum
        metric_samples = $samples.Count
        screenshot = $screenshotPath
        screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
        map_asset_bytes = (Get-Item -LiteralPath $mapPath).Length
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 GPU 逐帧可视化验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_REALTIME_VISUAL_VALIDATION_PASSED report=$reportPath"
