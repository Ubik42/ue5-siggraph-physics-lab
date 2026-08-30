param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuModalCorrectionAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_modal_correction_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少 GPU 缩减模态自动化报告：$automationPath"
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuReducedModalCorrection" })[0]
$messages = if ($null -ne $test) { @($test.entries | ForEach-Object { $_.event.message }) } else { @() }
$metricMessage = @($messages | Where-Object { $_ -like "GPU reduced modal correction:*" })[0]
$metricMatch = [regex]::Match($metricMessage, "particles=(?<particles>\d+) modes=(?<modes>\d+) steps=(?<steps>\d+) baseline_low_energy=(?<baseline_energy>[0-9.]+) modal_low_energy=(?<modal_energy>[0-9.]+) baseline_bottom_drop_cm=(?<baseline_drop>[0-9.]+) modal_bottom_drop_cm=(?<modal_drop>[0-9.]+) modal_passes=(?<passes>\d+)")

$baselineEnergy = if ($metricMatch.Success) { [double]$metricMatch.Groups["baseline_energy"].Value } else { 0.0 }
$modalEnergy = if ($metricMatch.Success) { [double]$metricMatch.Groups["modal_energy"].Value } else { 0.0 }
$baselineDrop = if ($metricMatch.Success) { [double]$metricMatch.Groups["baseline_drop"].Value } else { 0.0 }
$modalDrop = if ($metricMatch.Success) { [double]$metricMatch.Groups["modal_drop"].Value } else { 0.0 }
$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = (@($automation.devices | Where-Object { $_.renderMode -eq "D3D12_SM6" }).Count -ge 1)
    metrics_present = $metricMatch.Success
    expected_particle_count = ($metricMatch.Success -and [int]$metricMatch.Groups["particles"].Value -eq 300)
    expected_mode_count = ($metricMatch.Success -and [int]$metricMatch.Groups["modes"].Value -eq 12)
    modal_pass_each_step = ($metricMatch.Success -and [int64]$metricMatch.Groups["passes"].Value -eq [int64]$metricMatch.Groups["steps"].Value)
    low_frequency_energy_reduced = ($metricMatch.Success -and $modalEnergy -lt $baselineEnergy * 0.8)
    sag_reduced = ($metricMatch.Success -and $modalDrop -lt $baselineDrop * 0.9)
    meaningful_gravity_response_preserved = ($metricMatch.Success -and $modalDrop -gt $baselineDrop * 0.1)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = if ($metricMatch.Success) { [int]$metricMatch.Groups["particles"].Value } else { $null }
        modes = if ($metricMatch.Success) { [int]$metricMatch.Groups["modes"].Value } else { $null }
        steps = if ($metricMatch.Success) { [int]$metricMatch.Groups["steps"].Value } else { $null }
        baseline_low_frequency_energy = $baselineEnergy
        modal_low_frequency_energy = $modalEnergy
        energy_ratio = if ($baselineEnergy -gt 0.0) { $modalEnergy / $baselineEnergy } else { $null }
        baseline_bottom_drop_cm = $baselineDrop
        modal_bottom_drop_cm = $modalDrop
        gravity_response_ratio = if ($baselineDrop -gt 0.0) { $modalDrop / $baselineDrop } else { $null }
        modal_passes = if ($metricMatch.Success) { [int64]$metricMatch.Groups["passes"].Value } else { $null }
        automation_report = $automationPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "GPU 缩减模态验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_MODAL_CORRECTION_VALIDATION_PASSED report=$reportPath"
