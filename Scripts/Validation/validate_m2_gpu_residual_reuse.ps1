param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuResidualReuseAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_residual_reuse_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 GPU 残差复用报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuResidualReuse" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU residual reuse:*" })[0]
$match = [regex]::Match($message, "steps=(?<steps>\d+) max_residual_cm=(?<maximum>[0-9.]+) safe_fallback_events=(?<safe>\d+) topology_fallback_events=(?<topology>\d+)")
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    d3d12_sm6 = (@($automation.devices | Where-Object { $_.renderMode -eq "D3D12_SM6" }).Count -eq 1)
    metrics_present = $match.Success
    residual_captured_each_step = ($match.Success -and [int]$match.Groups["steps"].Value -eq 12)
    nonzero_safe_residual = ($match.Success -and [double]$match.Groups["maximum"].Value -gt 0.0 -and [double]$match.Groups["maximum"].Value -lt 100.0)
    safe_run_has_no_fallback = ($match.Success -and [int64]$match.Groups["safe"].Value -eq 0)
    topology_change_invalidates_all_free_particles = ($match.Success -and [int64]$match.Groups["topology"].Value -eq 30)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        maximum_forwarded_residual_cm = if ($match.Success) { [double]$match.Groups["maximum"].Value } else { $null }
        topology_fallback_events = if ($match.Success) { [int64]$match.Groups["topology"].Value } else { $null }
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "GPU 残差复用验证失败：$reportPath" }
Write-Output "M2_GPU_RESIDUAL_REUSE_VALIDATION_PASSED report=$reportPath"
