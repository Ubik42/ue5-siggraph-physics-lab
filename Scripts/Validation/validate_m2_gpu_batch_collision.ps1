param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuBatchCollisionAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_batch_collision_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少 M2 GPU 批量碰撞自动化报告：$automationPath"
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuBatchCollisionBuffers" })[0]
$messages = if ($null -ne $test) { @($test.entries | ForEach-Object { $_.event.message }) } else { @() }
$batchMessage = @($messages | Where-Object { $_ -like "GPU batch collision:*" })[0]
$dynamicMessage = @($messages | Where-Object { $_ -like "GPU dynamic collision update:*" })[0]
$batchMatch = [regex]::Match($batchMessage, "particles=(?<particles>\d+) spheres=(?<spheres>\d+) capsules=(?<capsules>\d+) input_max_penetration_cm=(?<input>[0-9.]+) output_max_penetration_cm=(?<output>[0-9.]+)")
$dynamicMatch = [regex]::Match($dynamicMessage, "step=(?<step>\d+) output_max_penetration_cm=(?<output>[0-9.]+)")

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = (@($automation.devices | Where-Object { $_.renderMode -eq "D3D12_SM6" }).Count -ge 1)
    batch_metrics_present = $batchMatch.Success
    sphere_buffer_used = ($batchMatch.Success -and [int]$batchMatch.Groups["spheres"].Value -ge 1)
    multiple_capsules_used = ($batchMatch.Success -and [int]$batchMatch.Groups["capsules"].Value -ge 2)
    input_penetration_positive = ($batchMatch.Success -and [double]$batchMatch.Groups["input"].Value -gt 1.0)
    batch_penetration_removed = ($batchMatch.Success -and [double]$batchMatch.Groups["output"].Value -lt 0.001)
    dynamic_update_metrics_present = $dynamicMatch.Success
    dynamic_update_applied_next_step = ($dynamicMatch.Success -and [int]$dynamicMatch.Groups["step"].Value -eq 1)
    dynamic_update_penetration_removed = ($dynamicMatch.Success -and [double]$dynamicMatch.Groups["output"].Value -lt 0.001)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = if ($batchMatch.Success) { [int]$batchMatch.Groups["particles"].Value } else { $null }
        spheres = if ($batchMatch.Success) { [int]$batchMatch.Groups["spheres"].Value } else { $null }
        capsules = if ($batchMatch.Success) { [int]$batchMatch.Groups["capsules"].Value } else { $null }
        input_max_penetration_cm = if ($batchMatch.Success) { [double]$batchMatch.Groups["input"].Value } else { $null }
        output_max_penetration_cm = if ($batchMatch.Success) { [double]$batchMatch.Groups["output"].Value } else { $null }
        dynamic_update_step = if ($dynamicMatch.Success) { [int]$dynamicMatch.Groups["step"].Value } else { $null }
        dynamic_output_max_penetration_cm = if ($dynamicMatch.Success) { [double]$dynamicMatch.Groups["output"].Value } else { $null }
        automation_report = $automationPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 GPU 批量碰撞验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_BATCH_COLLISION_VALIDATION_PASSED report=$reportPath"
