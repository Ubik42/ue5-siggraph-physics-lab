param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuRealtimeAutomation\index.json"
$logPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuRealtimeAutomation.log"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_realtime_validation.json"

foreach ($path in @($automationPath, $logPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 持久 GPU 运行验证产物：$path"
    }
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$device = @($automation.devices)[0]
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuRealtimePersistentBuffers" })[0]
$logText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
$metricMatch = [regex]::Match($logText, "Persistent GPU cloth: steps=(?<steps>\d+) readbacks=(?<readbacks>\d+) latest_step=(?<latest>\d+) particles=(?<particles>\d+) max_edge_error_cm=(?<error>[0-9.]+)")
$edgeError = if ($metricMatch.Success) { [double]$metricMatch.Groups["error"].Value } else { [double]::NaN }

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = ($null -ne $device -and $device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    physical_gpu = ($null -ne $device -and -not [string]::IsNullOrWhiteSpace([string]$device.gPU))
    persistent_metrics_present = $metricMatch.Success
    all_fixed_steps_executed = ($metricMatch.Success -and [int64]$metricMatch.Groups["steps"].Value -eq 120)
    final_step_read_back = ($metricMatch.Success -and [int64]$metricMatch.Groups["latest"].Value -eq 120)
    multiple_readbacks_completed = ($metricMatch.Success -and [int64]$metricMatch.Groups["readbacks"].Value -ge 2)
    expected_particle_count = ($metricMatch.Success -and [int]$metricMatch.Groups["particles"].Value -eq 120)
    finite_bounded_edge_error = ($metricMatch.Success -and -not [double]::IsNaN($edgeError) -and -not [double]::IsInfinity($edgeError) -and $edgeError -lt 5.0)
    no_fatal_error = ($logText -notmatch "Fatal error:")
    no_ensure = ($logText -notmatch "Ensure condition failed")
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
        steps = if ($metricMatch.Success) { [int64]$metricMatch.Groups["steps"].Value } else { $null }
        readbacks = if ($metricMatch.Success) { [int64]$metricMatch.Groups["readbacks"].Value } else { $null }
        latest_readback_step = if ($metricMatch.Success) { [int64]$metricMatch.Groups["latest"].Value } else { $null }
        particles = if ($metricMatch.Success) { [int]$metricMatch.Groups["particles"].Value } else { $null }
        maximum_edge_error_cm = if ($metricMatch.Success) { $edgeError } else { $null }
        automation_report = $automationPath
        engine_log = $logPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 持久 GPU 运行验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_REALTIME_VALIDATION_PASSED report=$reportPath"
