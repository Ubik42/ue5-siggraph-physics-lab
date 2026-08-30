param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuExplicitCompleteCcdAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_complete_ccd_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少显式拓扑完整 CCD 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.CompleteCcdAblation")
$info = @($test[0].entries | ForEach-Object { $_.event.message }) -join "`n"
$checks = [ordered]@{
    d3d12_sm6 = ($automation.devices[0].renderMode -eq "D3D12_SM6")
    target_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success" -and [int]$automation.failed -eq 0)
    discrete_crossing_reproduced = ($info -match "discrete_z=-100\.000")
    ccd_entry_surface_hit = ($info -match "ccd_z=10\.010")
    inverse_mass_fixed_contract = ($info -match "fixed_index_1_drift_cm=0\.000000000")
    ablation_pass_counts = ($info -match "discrete_passes=0 ccd_passes=1")
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        automation_test = "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.CompleteCcdAblation"
        render_mode = $automation.devices[0].renderMode
        summary = $info
        scope = "任意拓扑动态粒子的步初到候选位置线段对当前球/胶囊快照做最早 TOI 扫掠；固定点读取真实逆质量，不代表布料自碰撞或移动碰撞体时空 CCD"
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式拓扑完整 CCD 验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_COMPLETE_CCD_VALIDATION_PASSED report=$reportPath"
