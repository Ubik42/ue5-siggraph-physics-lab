param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\ExplicitModalRankOneAutomation\index.json"
$enabledPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_assassin_runtime.json"
$disabledPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_assassin_rank_one_off_runtime.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_rank_one_validation.json"
foreach ($path in @($automationPath, $enabledPath, $disabledPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少显式模态/rank-one 证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$enabled = Get-Content -LiteralPath $enabledPath -Raw -Encoding UTF8 | ConvertFrom-Json
$disabled = Get-Content -LiteralPath $disabledPath -Raw -Encoding UTF8 | ConvertFrom-Json
$modalTest = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.ContactCloth.ExplicitTopology.ModalSubspaceReference")
$gpuTest = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.RankOneCollisionAblation")
$modalInfo = @($modalTest[0].entries | ForEach-Object { $_.event.message }) -join "`n"
$gpuInfo = @($gpuTest[0].entries | ForEach-Object { $_.event.message }) -join "`n"
$checks = [ordered]@{
    d3d12_sm6 = ($automation.devices[0].renderMode -eq "D3D12_SM6")
    two_targeted_tests_passed = ($modalTest.Count -eq 1 -and $gpuTest.Count -eq 1 -and $modalTest[0].state -eq "Success" -and $gpuTest[0].state -eq "Success" -and [int]$automation.failed -eq 0)
    arbitrary_topology_basis_is_orthonormal = ($modalInfo -match "fixed_support=0\.000e\+00 orthogonality_error=2\.220e-16")
    micro_ablation_improves_penetration = ($gpuInfo -match "baseline_penetration_cm=4\.701429844 rank_one_penetration_cm=4\.642128944")
    non_prefix_fixed_vertices_do_not_drift = ($gpuInfo -match "fixed_1_drift_cm=0\.000000000 fixed_6_drift_cm=0\.000000000")
    same_assassin_topology_and_steps = ([int]$enabled.particles -eq 2011 -and [int]$disabled.particles -eq 2011 -and [int]$enabled.edges -eq 5714 -and [int]$disabled.edges -eq 5714 -and [int]$enabled.steps_executed -eq 300 -and [int]$disabled.steps_executed -eq 300)
    same_twelve_modal_passes = ([int]$enabled.modal_mode_count -eq 12 -and [int]$disabled.modal_mode_count -eq 12 -and [int]$enabled.modal_corrections -eq 300 -and [int]$disabled.modal_corrections -eq 300)
    rank_one_switch_changes_real_backend = ($enabled.rank_one_enabled -eq $true -and [int]$enabled.rank_one_collision_systems -eq 300 -and $disabled.rank_one_enabled -eq $false -and [int]$disabled.rank_one_collision_systems -eq 0)
    both_runs_pass_original_penetration_gate = ($enabled.result -eq "passed" -and $disabled.result -eq "passed" -and [double]$enabled.maximum_collision_penetration_cm -lt 0.001 -and [double]$disabled.maximum_collision_penetration_cm -lt 0.001)
    rank_one_reduces_structural_error = ([double]$enabled.maximum_edge_error_cm -lt [double]$disabled.maximum_edge_error_cm)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        render_mode = $automation.devices[0].renderMode
        modal_reference = $modalInfo
        gpu_micro_ablation = $gpuInfo
        enabled = [ordered]@{ edge_error_cm = [double]$enabled.maximum_edge_error_cm; penetration_cm = [double]$enabled.maximum_collision_penetration_cm; dynamic_displacement_cm = [double]$enabled.average_dynamic_displacement_cm; systems = [int]$enabled.rank_one_collision_systems }
        disabled = [ordered]@{ edge_error_cm = [double]$disabled.maximum_edge_error_cm; penetration_cm = [double]$disabled.maximum_collision_penetration_cm; dynamic_displacement_cm = [double]$disabled.average_dynamic_displacement_cm; systems = [int]$disabled.rank_one_collision_systems }
        scope = "2011 粒子 Assassin 任意拓扑每步一次模态与可开关的完整 3m×3m rank-one 分析接触系统；工程版并非论文每轮 local-global 内重装配，也不包含布料自碰撞"
    }
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式模态/rank-one 验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_RANK_ONE_VALIDATION_PASSED report=$reportPath"
