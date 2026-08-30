param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$validationRoot = Join-Path $ProjectRoot "Docs\Validation\M4"
$readJson = {
    param([string]$RelativePath)
    Get-Content -LiteralPath (Join-Path $validationRoot $RelativePath) -Raw -Encoding UTF8 | ConvertFrom-Json
}

$foundation = & $readJson "flow_map_foundation_validation.json"
$scalar = & $readJson "scalar_transport_ablation_validation.json"
$impulse = & $readJson "impulse_vortex_validation.json"
$pressure = & $readJson "gpu_pressure_validation.json"
$closedLoop = & $readJson "gpu_closed_loop_validation.json"
$solid = & $readJson "gpu_solid_boundary_validation.json"
$realtime = & $readJson "flowmap_realtime_d3d12_validation.json"
$ablation = & $readJson "flowmap_velocity_budget_ablation_validation.json"
$matrix = & $readJson "flowmap_backend_budget_validation.json"
$interaction = & $readJson "flowmap_backend_budget_interaction_validation.json"
$longSequence = & $readJson "flowmap_long_sequence_validation.json"
$portfolio = & $readJson "flowmap_portfolio_states_validation.json"
$regression = & $readJson "BackendBudgetRegressionAutomation\index.json"

$readmePath = Join-Path $validationRoot "README.md"
$scopePath = Join-Path $ProjectRoot "Docs\Research\CASE03_FlowMap流体复现范围.md"
$readme = Get-Content -LiteralPath $readmePath -Raw -Encoding UTF8
$scope = Get-Content -LiteralPath $scopePath -Raw -Encoding UTF8
$screenshotPaths = @(
    "Screenshots\Case03_FlowMap_LiveChaos.png",
    "Screenshots\Case03_FlowMap_ShortMapBaseline_Medium.png",
    "Screenshots\Case03_FlowMap_ParticleFlowMapResearch_High.png",
    "Screenshots\Case03_FlowMap_FailureVelocityBudget.png"
)

$checks = [ordered]@{
    foundation_contract_passed = ($foundation.result -eq "passed")
    scalar_ablation_passed = ($scalar.result -eq "passed")
    vector_impulse_closed_loop_passed = ($impulse.result -eq "passed")
    gpu_pressure_cpu_truth_passed = ($pressure.result -eq "passed")
    persistent_gpu_closed_loop_passed = ($closedLoop.result -eq "passed")
    chaos_solid_boundary_gpu_consumer_passed = ($solid.result -eq "passed")
    live_map_runtime_passed = [bool]$realtime.runtime_gate_passed
    expected_failure_ablation_is_recorded = ([bool]$ablation.stable_profile.runtime_gate_passed -and -not [bool]$ablation.failure_profile.runtime_gate_passed)
    real_backend_budget_matrix_passed = ($matrix.result -eq "passed" -and @($matrix.runs).Count -eq 6)
    in_process_backend_budget_rebuild_passed = ($interaction.result -eq "passed" -and [int]$interaction.summary.reset -eq 3)
    long_sequence_raw_timing_passed = ($longSequence.result -eq "passed" -and @($longSequence.runs | Where-Object { [int]$_.recomputed.steady_samples -lt 1000 }).Count -eq 0)
    full_flowmap_regression_15_of_15 = ([int]$regression.succeeded -eq 15 -and [int]$regression.failed -eq 0 -and [int]$regression.notRun -eq 0)
    portfolio_three_states_passed = ($portfolio.result -eq "passed" -and @($portfolio.runs).Count -eq 3 -and @($portfolio.runs | Where-Object { -not $_.passed }).Count -eq 0)
    four_gui_screenshots_are_nontrivial = (@($screenshotPaths | Where-Object { -not (Test-Path -LiteralPath (Join-Path $validationRoot $_)) -or (Get-Item -LiteralPath (Join-Path $validationRoot $_)).Length -le 100000 }).Count -eq 0)
    standard_benchmark_mapping_documented = ($readme -match "标准基准与作品场景映射" -and $readme -match "数值基准 3")
    golden_path_is_30_to_90_seconds = ($readme -match "60–75 秒黄金演示路径" -and $readme -match "B/C/Space/N/D/E")
    attribution_is_documented = ($readme -match "UE、Chaos、论文与个人贡献归因" -and $readme -match "个人实现")
    honest_scope_is_documented = ($scope -match "不声称完整复现论文" -and $scope -match "尚未实现 3D")
    case_is_marked_frozen_in_scope = ($scope -match "Case03 进入冻结状态")
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    case = "Case03_ParticleFlowMap"
    result = $(if ($failed.Count -eq 0) { "passed" } else { "failed" })
    frozen_scope = "2D PFM-inspired UE/RDG flow research reproduction with Live Chaos rigid boundary"
    checks = $checks
    failed_checks = $failed
    next_milestone = "M5 / Case04"
}

$outputPath = Join-Path $validationRoot "case03_acceptance_validation.json"
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) { exit 1 }
