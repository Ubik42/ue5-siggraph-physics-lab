param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuExplicitNdbAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_ndb_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少显式拓扑 NDB 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.NonDistanceBarrierAblation")
$info = @($test[0].entries | ForEach-Object { $_.event.message }) -join "`n"
$checks = [ordered]@{
    d3d12_sm6 = ($automation.devices[0].renderMode -eq "D3D12_SM6")
    target_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success" -and [int]$automation.failed -eq 0)
    constant_penalty_baseline = ($info -match "constant_penetration_cm=4\.113")
    continuous_history_improves_contact = ($info -match "ndb_penetration_cm=1\.503")
    four_state_updates_observed = ($info -match "ndb_updates=4")
    activation_readback_observed = ($info -match "active_pairs=1 maximum_activation=4")
    inverse_mass_fixed_contract = ($info -match "fixed_index_1_drift_cm=0\.000000000")
    capacity_overflow_rejected = ($info -match "capacity_overflow_rejected=true")
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        automation_test = "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.NonDistanceBarrierAblation"
        render_mode = $automation.devices[0].renderMode
        summary = $info
        scope = "粒子×分析碰撞原语的跨固定步连续激活状态；有限罚权重只由激活历史增长，容量越界被拒绝，不代表布料自碰撞障碍"
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式拓扑 NDB 验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_NDB_VALIDATION_PASSED report=$reportPath"
