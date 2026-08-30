param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuJacobiAutomation\index.json"
$logPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuJacobiAutomation.log"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_jacobi_validation.json"

foreach ($path in @($automationPath, $logPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 GPU Jacobi 验证产物：$path"
    }
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$device = @($automation.devices)[0]
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuJacobiDispatch" })[0]
$logText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
$metricMatch = [regex]::Match($logText, "D3D GPU Jacobi: particles=(?<particles>\d+) iterations=(?<iterations>\d+) input_max_edge_error_cm=(?<input>[0-9.]+) output_max_edge_error_cm=(?<output>[0-9.]+) input_capsule_penetration_cm=(?<inputPen>[0-9.]+) output_capsule_penetration_cm=(?<outputPen>[0-9.]+) blocking_wall_ms=(?<wall>[0-9.]+)")
$inputError = if ($metricMatch.Success) { [double]$metricMatch.Groups["input"].Value } else { [double]::NaN }
$outputError = if ($metricMatch.Success) { [double]$metricMatch.Groups["output"].Value } else { [double]::NaN }
$inputPenetration = if ($metricMatch.Success) { [double]$metricMatch.Groups["inputPen"].Value } else { [double]::NaN }
$outputPenetration = if ($metricMatch.Success) { [double]$metricMatch.Groups["outputPen"].Value } else { [double]::NaN }

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = ($null -ne $device -and $device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    physical_gpu = ($null -ne $device -and -not [string]::IsNullOrWhiteSpace([string]$device.gPU))
    shader_executed = $metricMatch.Success
    dispatch_shape = ($metricMatch.Success -and [int]$metricMatch.Groups["particles"].Value -eq 192 -and [int]$metricMatch.Groups["iterations"].Value -eq 96)
    finite_errors = ($metricMatch.Success -and -not [double]::IsNaN($inputError) -and -not [double]::IsInfinity($inputError) -and -not [double]::IsNaN($outputError) -and -not [double]::IsInfinity($outputError))
    edge_error_reduced_20_percent = ($metricMatch.Success -and $inputError -gt 1.0 -and $outputError -lt $inputError * 0.8)
    capsule_contact_triggered = ($metricMatch.Success -and $inputPenetration -gt 1.0)
    capsule_penetration_removed = ($metricMatch.Success -and $outputPenetration -lt 0.001)
    test_completed = ($logText -match "Test Completed\. Result=\{成功\}.*GpuJacobiDispatch")
    no_fatal_error = ($logText -notmatch "Fatal error:")
    no_late_shader_registration = ($logText -notmatch "Shader type was loaded after engine init")
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        test = if ($null -ne $test) { $test.fullTestPath } else { $null }
        duration_seconds = if ($null -ne $test) { [double]$test.duration } else { $null }
        gpu = if ($null -ne $device) { $device.gPU } else { $null }
        render_mode = if ($null -ne $device) { $device.renderMode } else { $null }
        rhi = if ($null -ne $device) { $device.rHI } else { $null }
        particles = if ($metricMatch.Success) { [int]$metricMatch.Groups["particles"].Value } else { $null }
        iterations = if ($metricMatch.Success) { [int]$metricMatch.Groups["iterations"].Value } else { $null }
        input_max_edge_error_cm = if ($metricMatch.Success) { $inputError } else { $null }
        output_max_edge_error_cm = if ($metricMatch.Success) { $outputError } else { $null }
        input_max_capsule_penetration_cm = if ($metricMatch.Success) { $inputPenetration } else { $null }
        output_max_capsule_penetration_cm = if ($metricMatch.Success) { $outputPenetration } else { $null }
        blocking_wall_ms = if ($metricMatch.Success) { [double]$metricMatch.Groups["wall"].Value } else { $null }
        automation_report = $automationPath
        engine_log = $logPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 GPU Jacobi 验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_JACOBI_VALIDATION_PASSED report=$reportPath"
