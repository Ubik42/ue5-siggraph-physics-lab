param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_realtime_d3d12_validation.json"
$mapSetupPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_chaos_map_setup.json"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M4\PressureAblationRegressionAutomation\index.json"
$visualReportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_visual_gui_validation.json"
$ablationReportPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_velocity_budget_ablation_validation.json"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M4\Screenshots\Case03_FlowMap_LiveChaos.png"
$pressureD3D12Path = Join-Path $ProjectRoot "Docs\Validation\M4\PressureReadbackD3D12Automation\index.json"
$backendBudgetPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_backend_budget_validation.json"
$backendBudgetInteractionPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_backend_budget_interaction_validation.json"
$backendBudgetRegressionPath = Join-Path $ProjectRoot "Docs\Validation\M4\BackendBudgetRegressionAutomation\index.json"
$longSequencePath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_long_sequence_validation.json"
$portfolioStatesPath = Join-Path $ProjectRoot "Docs\Validation\M4\flowmap_portfolio_states_validation.json"
$mapPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsFlowMap\Content\Maps\L_FlowMapChaosObstacle.umap"
$actorPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsFlowMap\Source\PhysicsFlowMapRuntime\Private\FlowMapRealtimeActor.cpp"

$report = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$mapSetup = Get-Content -LiteralPath $mapSetupPath -Raw -Encoding UTF8 | ConvertFrom-Json
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$visualReport = Get-Content -LiteralPath $visualReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$ablationReport = Get-Content -LiteralPath $ablationReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$pressureD3D12 = Get-Content -LiteralPath $pressureD3D12Path -Raw -Encoding UTF8 | ConvertFrom-Json
$backendBudget = Get-Content -LiteralPath $backendBudgetPath -Raw -Encoding UTF8 | ConvertFrom-Json
$backendBudgetInteraction = Get-Content -LiteralPath $backendBudgetInteractionPath -Raw -Encoding UTF8 | ConvertFrom-Json
$backendBudgetRegression = Get-Content -LiteralPath $backendBudgetRegressionPath -Raw -Encoding UTF8 | ConvertFrom-Json
$longSequence = Get-Content -LiteralPath $longSequencePath -Raw -Encoding UTF8 | ConvertFrom-Json
$portfolioStates = Get-Content -LiteralPath $portfolioStatesPath -Raw -Encoding UTF8 | ConvertFrom-Json
$actorSource = Get-Content -LiteralPath $actorPath -Raw -Encoding UTF8

$checks = [ordered]@{
    runtime_gate_passed = [bool]$report.runtime_gate_passed
    runtime_steps_at_least_300 = ([int64]$report.steps_executed -ge 300)
    chaos_boundary_applied_each_step = ([int64]$report.solid_boundary_applications -eq [int64]$report.steps_executed)
    async_readbacks_completed = ([int64]$report.readbacks_completed -gt 0)
    no_bad_snapshot_reached_scene = ([int64]$report.rejected_snapshots -eq 0)
    steady_gpu_timing_available = ([int64]$report.gpu_timing_samples -gt 100 -and [double]$report.steady_state_mean_gpu_step_ms -gt 0.0)
    interaction_script_completed = ([int]$report.interaction.script_phase_completed -eq 6)
    pause_single_step_and_reset_exercised = ([int]$report.interaction.pause_resume_toggles -eq 2 -and [int]$report.interaction.single_steps -eq 1 -and [int]$report.interaction.full_chain_resets -eq 1)
    debug_and_export_exercised = ([int]$report.interaction.debug_toggles -eq 2 -and [int]$report.interaction.metric_exports -eq 1 -and [bool]$report.interaction.export_json_and_csv_created)
    map_setup_saved_without_errors = ([bool]$mapSetup.saved -and @($mapSetup.errors).Count -eq 0)
    map_asset_exists = (Test-Path -LiteralPath $mapPath)
    full_flowmap_regression_15_of_15 = ([int]$automation.succeeded -eq 15 -and [int]$automation.failed -eq 0 -and [int]$automation.notRun -eq 0)
    gui_visual_inspection_passed = ([bool]$visualReport.visual_inspection_passed)
    screenshot_exists_and_nontrivial = ((Test-Path -LiteralPath $screenshotPath) -and (Get-Item -LiteralPath $screenshotPath).Length -gt 100000)
    visual_uses_gpu_snapshot_data = ([int]$visualReport.snapshot.gpu_mac_velocity_arrows -eq 144 -and [int]$visualReport.snapshot.particles -eq 576)
    visual_snapshot_below_velocity_budget = ([double]$visualReport.snapshot.maximum_velocity_cm_per_second -lt 120.0 -and [int]$visualReport.snapshot.velocity_limited_faces -eq 0)
    pressure_heatmap_snapshot_present = ([double]$visualReport.snapshot.pressure_potential_minimum -lt 0.0 -and [double]$visualReport.snapshot.pressure_potential_maximum -gt 0.0 -and ([int]$visualReport.snapshot.negative_pressure_cells + [int]$visualReport.snapshot.positive_pressure_cells) -gt 0)
    stable_profile_passes_and_ablation_fails = ([bool]$ablationReport.stable_profile.runtime_gate_passed -and -not [bool]$ablationReport.failure_profile.runtime_gate_passed)
    ablation_exposes_large_amplification = ([double]$ablationReport.comparison.velocity_amplification_ratio -gt 1000.0 -and [double]$ablationReport.comparison.maximum_absolute_pressure_amplification_ratio -gt 1000.0)
    pressure_readback_d3d12_4_of_4 = ([int]$pressureD3D12.succeeded -eq 4 -and [int]$pressureD3D12.failed -eq 0 -and [int]$pressureD3D12.notRun -eq 0)
    backend_budget_matrix_all_six_pass = ($backendBudget.result -eq 'passed' -and @($backendBudget.runs).Count -eq 6 -and @($backendBudget.runs | Where-Object { -not $_.passed }).Count -eq 0)
    backend_budget_interaction_rebuilds_real_sessions = ($backendBudgetInteraction.result -eq 'passed' -and [int]$backendBudgetInteraction.summary.backend_toggles -eq 2 -and [int]$backendBudgetInteraction.summary.budget_toggles -eq 1 -and [int]$backendBudgetInteraction.summary.reset -eq 3)
    backend_budget_regression_15_of_15 = ([int]$backendBudgetRegression.succeeded -eq 15 -and [int]$backendBudgetRegression.failed -eq 0 -and [int]$backendBudgetRegression.notRun -eq 0)
    long_sequence_both_backends_pass = ($longSequence.result -eq 'passed' -and @($longSequence.runs).Count -eq 2 -and @($longSequence.runs | Where-Object { -not $_.passed }).Count -eq 0)
    long_sequence_has_raw_recomputable_distributions = (@($longSequence.runs | Where-Object { [int]$_.recomputed.samples -lt 1100 -or [int]$_.recomputed.steady_samples -lt 1000 -or -not $_.checks.mean_recomputes_from_csv -or -not $_.checks.p95_recomputes_from_csv -or -not $_.checks.p99_recomputes_from_csv }).Count -eq 0)
    portfolio_states_all_three_pass = ($portfolioStates.result -eq 'passed' -and @($portfolioStates.runs).Count -eq 3 -and @($portfolioStates.runs | Where-Object { -not $_.passed }).Count -eq 0)
    portfolio_screenshots_are_nontrivial = (@($portfolioStates.runs | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ProjectRoot $_.screenshot)) -or [int64]$_.screenshot_bytes -le 100000 }).Count -eq 0)
    portfolio_states_cover_short_pfm_and_failure = (@($portfolioStates.runs | Where-Object { $_.request.backend -eq 'ShortMapBaseline' -and $_.summary.pass -eq 'true' }).Count -eq 1 -and @($portfolioStates.runs | Where-Object { $_.request.backend -eq 'ParticleFlowMapResearch' -and $_.request.ablation -eq 'false' -and $_.summary.pass -eq 'true' }).Count -eq 1 -and @($portfolioStates.runs | Where-Object { $_.request.ablation -eq 'true' -and $_.summary.pass -eq 'false' }).Count -eq 1)
    actor_uses_live_chaos_pose = ($actorSource -match "GetComponentTransform\(\)" -and $actorSource -match "SetSimulatePhysics\(true\)")
    actor_rejects_nonfinite_snapshot = ($actorSource -match "M4_FLOWMAP_REALTIME_SNAPSHOT_REJECTED")
    actor_resets_gpu_and_experiment_together = ($actorSource -match "GpuSession\.Shutdown\(\)" -and $actorSource -match "ExperimentComponent->ResetExperiment\(\)")
    actor_assigns_portfolio_camera = ($actorSource -match "SetViewTarget\(this\)")
    actor_has_explicit_ablation_flag = ($actorSource -match "PhysicsResearchFlowMapVelocityBudgetAblation")
    actor_has_honest_numerical_baseline = ($actorSource -match "EPhysicsSolverBackend::NumericalBaseline" -and $actorSource -match "ShortMapBaseline")
    actor_has_real_backend_and_budget_controls = ($actorSource -match "EKeys::B" -and $actorSource -match "EKeys::C" -and $actorSource -match "GpuSession\.Shutdown\(\)")
    actor_exports_raw_long_sequence = ($actorSource -match "GetGpuTimingSamples" -and $actorSource -match "M4_FLOWMAP_LONG_SEQUENCE_EXPORTED")
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{ passed = ($failed.Count -eq 0); checks = $checks; failed_checks = $failed }
$result | ConvertTo-Json -Depth 5
if ($failed.Count -gt 0) { exit 1 }
