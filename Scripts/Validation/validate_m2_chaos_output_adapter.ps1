param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\ChaosOutputAdapterAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\chaos_output_adapter_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 Chaos 输出适配器自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.ContactCloth.ChaosOutputComponentSpaceMerge")
$info = @($test[0].entries | ForEach-Object { $_.event.message }) -join "`n"
$checks = [ordered]@{
    d3d12_sm6 = ($automation.devices[0].renderMode -eq "D3D12_SM6")
    exactly_one_test_passed = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    two_assets_merged = ($info -match "assets=2")
    assassin_vertex_contract = ($info -match "vertices=2011")
    component_space_roundtrip_recorded = ($info -match "maximum_error_cm=")
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        automation_test = "PhysicsResearch.ContactCloth.ChaosOutputComponentSpaceMerge"
        render_mode = $automation.devices[0].renderMode
        summary = $info
        scope = "验证两个 Chaos Clothing Asset 以各自 ComponentRelativeTransform 合并到与显式拓扑一致的组件空间，并拒绝缺失资产"
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "Chaos 输出适配器验证失败：$reportPath" }
Write-Output "M2_CHAOS_OUTPUT_ADAPTER_VALIDATION_PASSED report=$reportPath"
