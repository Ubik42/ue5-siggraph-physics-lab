param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M5'
function Read-Report([string]$Name) {
    return Get-Content -LiteralPath (Join-Path $root $Name) -Raw -Encoding UTF8 | ConvertFrom-Json
}

$spring = Read-Report 'inverse_spring_foundation_validation.json'
$ballistic = Read-Report 'ballistic_foundation_validation.json'
$chain = Read-Report 'spring_chain_foundation_validation.json'
$map = Read-Report 'inverse_spring_map_setup.json'
$realtime = Read-Report 'inverse_spring_realtime_validation.json'
$interaction = Read-Report 'inverse_spring_interaction_failure_validation.json'
$automation = Read-Report 'Automation\index.json'
$readme = Get-Content -LiteralPath (Join-Path $root 'README.md') -Raw -Encoding UTF8
$scope = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Research\CASE04_可微物理与反向设计复现范围.md') -Raw -Encoding UTF8
$attribution = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Development\ATTRIBUTION.md') -Raw -Encoding UTF8
$screenshots = @(
    'Screenshots\Case04_InverseSpring_ChaosReplay.png',
    'Screenshots\Case04_InverseSpring_IdentifiabilityFailure.png'
)

$checks = [ordered]@{
    standard_benchmark_1_damped_spring_passed = ($spring.result -eq 'passed')
    standard_benchmark_2_ballistic_passed = ($ballistic.result -eq 'passed')
    standard_benchmark_3_coupled_chain_passed = ($chain.result -eq 'passed')
    full_inverse_design_regression_10_of_10 = ([int]$automation.succeeded -eq 10 -and [int]$automation.failed -eq 0 -and [int]$automation.notRun -eq 0)
    independent_ue_map_is_saved = ([bool]$map.saved -and @($map.errors).Count -eq 0 -and $map.map -eq '/PhysicsInverseDesign/Maps/L_InverseSpringChaosReplay')
    real_chaos_constraint_replay_passed = ($realtime.result -eq 'passed')
    interaction_and_expected_failure_passed = ($interaction.result -eq 'passed')
    structured_runtime_csv_and_json_exist = ((Test-Path -LiteralPath (Join-Path $root 'Realtime\InverseSpringRealtime_Standard.csv')) -and (Test-Path -LiteralPath (Join-Path $root 'Realtime\InverseSpringRealtime_Standard.json')) -and (Test-Path -LiteralPath (Join-Path $root 'Realtime\InverseSpringRealtime_IdentifiabilityFailure.csv')))
    two_gui_screenshots_are_nontrivial = (@($screenshots | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_)) -or (Get-Item -LiteralPath (Join-Path $root $_)).Length -le 100000 }).Count -eq 0)
    benchmark_scene_mapping_is_documented = ($readme -match '三个标准基准与最终 UE 场景映射' -and $readme -match '数值基准 3')
    performance_mean_and_p95_are_reported = ($readme -match 'mean' -and $readme -match 'P95' -and @($chain.evaluation_cost_distribution).Count -eq 3)
    golden_path_is_30_to_90_seconds = ($readme -match '45–60 秒黄金演示路径' -and $readme -match 'Space' -and $readme -match 'N' -and $readme -match 'O' -and $readme -match 'F' -and $readme -match 'E')
    honest_scope_is_frozen = ($scope -match '不声称完整复现论文' -and $scope -match '进入冻结状态')
    ue_chaos_paper_and_personal_attribution_is_documented = ($readme -match 'UE、Chaos、论文与个人贡献归因' -and $attribution -match 'Case 04：可微物理与反向设计' -and $attribution -match '个人实现')
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    case = 'Case04_DifferentiablePhysicsInverseDesign'
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    frozen_scope = 'Forward-tangent differentiable dynamics across spring, ballistic, and coupled-chain benchmarks, with same-parameter Chaos Constraint replay'
    checks = $checks
    failed_checks = $failed
    next_milestone = 'M6 / Case05 Physics Driven Character'
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'case04_acceptance_validation.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) { exit 1 }
