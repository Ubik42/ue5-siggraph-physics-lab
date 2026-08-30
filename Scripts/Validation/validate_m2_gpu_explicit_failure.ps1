param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$evidence = Join-Path $ProjectRoot "Docs\Validation\M2"
$runtimePath = Join-Path $evidence "gpu_explicit_assassin_failure_weak_barrier_runtime.json"
$nominalPath = Join-Path $evidence "gpu_explicit_assassin_runtime.json"
$comparisonPath = Join-Path $evidence "gpu_explicit_assassin_failure_weak_barrier_comparison.csv"
$timingPath = Join-Path $evidence "gpu_explicit_assassin_failure_weak_barrier_gpu_timing.csv"
$screenshotPath = Join-Path $evidence "Screenshots\M2_ExplicitAssassinFailureWeakBarrier.png"
$reportPath = Join-Path $evidence "gpu_explicit_assassin_failure_weak_barrier_validation.json"
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$nominal = Get-Content -LiteralPath $nominalPath -Raw -Encoding UTF8 | ConvertFrom-Json
$comparison = Import-Csv -LiteralPath $comparisonPath -Encoding UTF8
$timing = Import-Csv -LiteralPath $timingPath -Encoding UTF8
$screenshot = if (Test-Path -LiteralPath $screenshotPath -PathType Leaf) { Get-Item -LiteralPath $screenshotPath } else { $null }
$checks = [ordered]@{
    expected_failure_run_passed = ($runtime.result -eq "passed" -and $runtime.expected_failure_observed -eq $true)
    weak_ndb_is_only_solver_profile_change = ($runtime.failure_profile -eq "weak_ndb" -and [Math]::Abs([double]$runtime.ndb_base_weight - 0.05) -lt 0.000001 -and $runtime.complete_ccd_enabled -eq $true -and $runtime.rank_one_enabled -eq $true)
    nominal_contract_is_strong_ndb = ($nominal.failure_profile -eq "none" -and [Math]::Abs([double]$nominal.ndb_base_weight - 10.0) -lt 0.000001 -and $nominal.complete_ccd_enabled -eq $true -and $nominal.rank_one_enabled -eq $true)
    exact_assassin_contract = ([int]$runtime.particles -eq 2011 -and [int]$runtime.edges -eq 5714 -and [int]$runtime.collision_spheres -eq 1 -and [int]$runtime.collision_capsules -eq 16)
    all_solver_passes_executed = ([int]$runtime.steps_executed -eq 300 -and [int]$runtime.modal_corrections -eq 300 -and [int]$runtime.rank_one_collision_systems -eq 300 -and [int]$runtime.ndb_state_updates -eq 300 -and [int]$runtime.complete_ccd_passes -eq 300)
    penetration_gate_failed_as_designed = ([double]$runtime.maximum_collision_penetration_cm -ge 0.001)
    failure_is_material_vs_nominal = ([double]$runtime.maximum_collision_penetration_cm -gt ([double]$nominal.maximum_collision_penetration_cm * 10.0))
    structure_remains_bounded = ([double]$runtime.maximum_edge_error_cm -lt 20.0)
    input_sequence_matches_nominal = ($runtime.replay_input_hash -eq $nominal.replay_input_hash)
    paired_curve_complete = (@($comparison).Count -eq 75 -and [int]$comparison[0].step -eq 4 -and [int]$comparison[-1].step -eq 300)
    gpu_curve_complete = (@($timing).Count -eq 300 -and [int]$runtime.gpu_timing_samples_skipped -eq 0)
    screenshot_present = ($null -ne $screenshot -and $screenshot.Length -gt 100000)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        runtime = $runtimePath
        nominal_runtime = $nominalPath
        screenshot = $screenshotPath
        input_hash = $runtime.replay_input_hash
        nominal_ndb_base_weight = [double]$nominal.ndb_base_weight
        failure_ndb_base_weight = [double]$runtime.ndb_base_weight
        nominal_maximum_penetration_cm = [double]$nominal.maximum_collision_penetration_cm
        failure_maximum_penetration_cm = [double]$runtime.maximum_collision_penetration_cm
        scope = "同一 Assassin 输入、拓扑、rank-one、CCD 与 300 步预算下，仅把 NDB 基础权重从 10 降至 0.05；穿透超过原有 0.001 cm 门即为预期故障。"
    }
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "Case 01 弱障碍故障验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_FAILURE_VALIDATION_PASSED report=$reportPath"
