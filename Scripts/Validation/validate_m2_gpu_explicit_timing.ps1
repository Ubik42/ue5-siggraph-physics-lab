param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuExplicitTimingAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_timing_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少显式 GPU timestamp 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.NonBlockingGpuTimestamp")
$info = @($test[0].entries | ForEach-Object { $_.event.message }) -join "`n"
$checks = [ordered]@{
    d3d12_sm6 = ($automation.devices[0].renderMode -eq "D3D12_SM6")
    target_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success" -and [int]$automation.failed -eq 0)
    timestamp_supported = ($info -match "supported=true")
    non_blocking_ring_exercised = ($info -match "steps=16 samples=8 skipped=8")
    finite_distribution = ($info -match "mean_ms=[0-9]+\.[0-9]+ p95_ms=[0-9]+\.[0-9]+ p99_ms=[0-9]+\.[0-9]+ max_ms=[0-9]+\.[0-9]+")
    full_step_scope_declared = ($info -match "scope=full_step_rdg_interval")
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        automation_test = "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.NonBlockingGpuTimestamp"
        render_mode = $automation.devices[0].renderMode
        summary = $info
        scope = "RQT_AbsoluteTime 查询围住完整显式固定步 RDG 区间；8 槽满时记录 skipped，不等待 GPU，因此不是 CPU 提交耗时，也不是单 kernel 时间"
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式 GPU timestamp 验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_TIMING_VALIDATION_PASSED report=$reportPath"
