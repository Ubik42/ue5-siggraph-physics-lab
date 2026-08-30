param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$validationRoot = Join-Path $ProjectRoot 'Docs\Validation\M2'

$componentReportNames = @(
    'asset_gate_validation.json',
    'cpu_reference_validation.json',
    'gpu_jacobi_visual_validation.json',
    'modal_subspace_validation.json',
    'non_distance_barrier_validation.json',
    'rank_one_hessian_validation.json',
    'complete_ccd_validation.json',
    'gpu_explicit_assassin_validation.json',
    'gpu_explicit_assassin_interactive_validation.json',
    'gpu_explicit_assassin_replay_validation.json',
    'gpu_explicit_assassin_failure_weak_barrier_validation.json',
    'gpu_explicit_timing_validation.json'
)
$componentReports = [ordered]@{}
foreach ($name in $componentReportNames)
{
    $componentReports[$name] = Get-Content -LiteralPath (Join-Path $validationRoot $name) -Raw -Encoding UTF8 | ConvertFrom-Json
}

$automation = Get-Content -LiteralPath (Join-Path $validationRoot 'Automation\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$runtime = Get-Content -LiteralPath (Join-Path $validationRoot 'gpu_explicit_assassin_runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$finalReport = $componentReports['gpu_explicit_assassin_validation.json']
$failureReport = $componentReports['gpu_explicit_assassin_failure_weak_barrier_validation.json']
$replayReport = $componentReports['gpu_explicit_assassin_replay_validation.json']
$readme = Get-Content -LiteralPath (Join-Path $validationRoot 'README.md') -Raw -Encoding UTF8
$scope = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Research\CASE01_接触与GPU布料复现范围.md') -Raw -Encoding UTF8
$attribution = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Development\ATTRIBUTION.md') -Raw -Encoding UTF8

$mapPath = Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsContactCloth\Content\Maps\L_GpuExplicitAssassin.umap'
$shaderPaths = @(
    'Plugins\PhysicsResearch\Shaders\Private\ContactClothExplicitTopology.usf',
    'Plugins\PhysicsResearch\Shaders\Private\ContactClothExplicitTopologyNdb.usf',
    'Plugins\PhysicsResearch\Shaders\Private\ContactClothExplicitTopologyRankOne.usf',
    'Plugins\PhysicsResearch\Shaders\Private\ContactClothExplicitTopologyCcd.usf'
)
$structuredEvidence = @(
    'Docs\Validation\M2\gpu_explicit_assassin_comparison.csv',
    'Docs\Validation\M2\gpu_explicit_assassin_comparison.json',
    'Docs\Validation\M2\gpu_explicit_assassin_gpu_timing.csv'
)
$screenshotNames = @(
    'M2_ChaosClothAssetGate.png',
    'M2_GpuJacobiValidation.png',
    'M2_ExplicitAssassinCloth.png',
    'M2_ExplicitAssassinInteractive.png',
    'M2_ExplicitAssassinReplay.png',
    'M2_ExplicitAssassinFailureWeakBarrier.png'
)

Add-Type -AssemblyName System.Drawing
$screenshotEvidence = [ordered]@{}
foreach ($name in $screenshotNames)
{
    $path = Join-Path $validationRoot "Screenshots\$name"
    $image = [Drawing.Image]::FromFile($path)
    $screenshotEvidence[$name] = [ordered]@{
        width = $image.Width
        height = $image.Height
        bytes = (Get-Item -LiteralPath $path).Length
    }
    $image.Dispose()
}

$allComponentReportsPass = @($componentReports.GetEnumerator() | Where-Object { $_.Value.result -ne 'passed' }).Count -eq 0
$allScreenshotsNontrivial = @($screenshotEvidence.GetEnumerator() | Where-Object {
    $_.Value.width -lt 1280 -or $_.Value.height -lt 720 -or $_.Value.bytes -lt 40000
}).Count -eq 0

$checks = [ordered]@{
    all_twelve_component_reports_pass = [bool]$allComponentReportsPass
    full_d3d12_sm6_automation_is_32_of_32_clean = [bool]($automation.succeeded -eq 32 -and $automation.failed -eq 0 -and $automation.succeededWithWarnings -eq 0 -and $automation.devices[0].renderMode -eq 'D3D12_SM6')
    three_or_more_standard_benchmarks_pass = [bool]($componentReports['cpu_reference_validation.json'].result -eq 'passed' -and $componentReports['gpu_jacobi_visual_validation.json'].result -eq 'passed' -and $componentReports['complete_ccd_validation.json'].result -eq 'passed')
    independent_final_map_exists = [bool](Test-Path -LiteralPath $mapPath)
    real_chaos_same_input_baseline_is_measured = [bool]($finalReport.evidence.chaos_snapshots -eq 300 -and $finalReport.evidence.comparison_samples -eq 75 -and $finalReport.checks.paired_time_series_complete)
    research_gpu_pipeline_executes_all_fixed_steps = [bool]($finalReport.evidence.fixed_steps -eq 300 -and $finalReport.evidence.rank_one_collision_systems -eq 300 -and $finalReport.evidence.complete_ccd_passes -eq 300 -and $finalReport.evidence.ndb_state_updates -eq 300)
    final_assassin_scene_quality_gate_passes = [bool]($finalReport.checks.structural_error_controlled -and $finalReport.checks.collision_penetration_removed -and $finalReport.evidence.maximum_collision_penetration_cm -lt 0.001)
    player_controls_change_real_simulation_state = [bool]($componentReports['gpu_explicit_assassin_interactive_validation.json'].result -eq 'passed')
    deterministic_replay_and_negative_hash_gate_pass = [bool]($replayReport.checks.input_hash_repeated -and $replayReport.checks.mismatched_hash_rejected -and $replayReport.evidence.replay_input_hash -eq '429e63168b51a679')
    expected_failure_is_single_variable_and_observed = [bool]($failureReport.checks.weak_ndb_is_only_solver_profile_change -and $failureReport.checks.penetration_gate_failed_as_designed -and $failureReport.evidence.failure_maximum_penetration_cm -ge 0.001)
    gpu_timing_has_raw_mean_p95_p99_evidence = [bool]($finalReport.evidence.steady_state_gpu_timing_samples -eq 284 -and $finalReport.evidence.p95_steady_state_gpu_step_ms -gt 0 -and $finalReport.evidence.p99_steady_state_gpu_step_ms -ge $finalReport.evidence.p95_steady_state_gpu_step_ms)
    csv_and_json_metric_exports_exist = [bool](@($structuredEvidence | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ProjectRoot $_)) }).Count -eq 0)
    six_gui_screenshots_are_nontrivial = [bool]$allScreenshotsNontrivial
    implementation_actor_and_four_core_shaders_exist = [bool]((Test-Path -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsContactCloth\Source\PhysicsContactClothRuntime\Private\ContactClothExplicitTopologyActor.cpp')) -and @($shaderPaths | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ProjectRoot $_)) }).Count -eq 0)
    golden_path_is_explicitly_sixty_seconds = [bool]($readme -match '60 秒黄金演示路径' -and $readme -match '50–60 秒')
    reproduction_scope_and_unimplemented_boundaries_are_explicit = [bool]($scope -match '工程复现范围已完成' -and $scope -match '不提前宣称' -and $readme -match '摩擦与布料自碰撞尚未迁移')
    epic_asset_chaos_and_personal_attribution_are_explicit = [bool]($attribution -match 'Assassin Fab / Marketplace' -and $attribution -match 'Chaos Cloth 是 UE 基线' -and $attribution -match '个人实现')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    case = 'Case01 / Contact-aware GPU cloth on UE Clothing topology with Chaos Cloth same-input baseline'
    frozen_scope = 'CPU and GPU numerical benchmarks, persistent RDG solver, Assassin UE Clothing/Physics Asset integration, Chaos paired baseline, interaction, replay, expected failure, timing, screenshots and attribution.'
    component_reports = $componentReportNames
    screenshots = $screenshotEvidence
    headline_metrics = [ordered]@{
        particles = $finalReport.evidence.particles
        edges = $finalReport.evidence.edges
        fixed_steps = $finalReport.evidence.fixed_steps
        chaos_snapshots = $finalReport.evidence.chaos_snapshots
        paired_samples = $finalReport.evidence.comparison_samples
        nominal_penetration_cm = $finalReport.evidence.maximum_collision_penetration_cm
        weak_barrier_penetration_cm = $failureReport.evidence.failure_maximum_penetration_cm
        steady_gpu_p95_ms = $finalReport.evidence.p95_steady_state_gpu_step_ms
        steady_gpu_p99_ms = $finalReport.evidence.p99_steady_state_gpu_step_ms
        replay_input_hash = $replayReport.evidence.replay_input_hash
    }
    checks = $checks
    failed_checks = $failed
    claim_boundary = 'Research backend is a paper-inspired UE/RDG engineering reproduction on the same Clothing and Physics Asset inputs. It does not claim full Chaos Cloth equivalence, cloth self-collision/friction parity, or ownership of Epic/Fab assets.'
}

$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $validationRoot 'case01_acceptance_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
