param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\ModalSubspaceAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\modal_subspace_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少模态子空间自动化报告：$automationPath"
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.ModalSubspaceReference" })[0]
$messages = if ($null -ne $test) { @($test.entries | ForEach-Object { $_.event.message }) } else { @() }
$metricMessage = @($messages | Where-Object { $_ -like "Modal subspace reference:*" })[0]
$metricMatch = [regex]::Match($metricMessage, "particles=(?<particles>\d+) modes=(?<modes>\d+) orthonormality_error=(?<orth>[0-9.eE+-]+) smooth_retention=(?<smooth>[0-9.]+) checker_retention=(?<checker>[0-9.]+) reduced_coordinate_error=(?<reduced>[0-9.eE+-]+) inverse_error=(?<inverse>[0-9.eE+-]+)")

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    metrics_present = $metricMatch.Success
    expected_particles = ($metricMatch.Success -and [int]$metricMatch.Groups["particles"].Value -eq 300)
    expected_modes = ($metricMatch.Success -and [int]$metricMatch.Groups["modes"].Value -eq 12)
    orthonormal_basis = ($metricMatch.Success -and [double]$metricMatch.Groups["orth"].Value -lt 1e-10)
    smooth_signal_retained = ($metricMatch.Success -and [double]$metricMatch.Groups["smooth"].Value -gt 0.99)
    checkerboard_rejected = ($metricMatch.Success -and [double]$metricMatch.Groups["checker"].Value -lt 0.05)
    reduced_system_recovers_known_solution = ($metricMatch.Success -and [double]$metricMatch.Groups["reduced"].Value -lt 1e-10)
    inverse_system_is_identity = ($metricMatch.Success -and [double]$metricMatch.Groups["inverse"].Value -lt 1e-10)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = if ($metricMatch.Success) { [int]$metricMatch.Groups["particles"].Value } else { $null }
        modes = if ($metricMatch.Success) { [int]$metricMatch.Groups["modes"].Value } else { $null }
        orthonormality_error = if ($metricMatch.Success) { [double]$metricMatch.Groups["orth"].Value } else { $null }
        smooth_retention = if ($metricMatch.Success) { [double]$metricMatch.Groups["smooth"].Value } else { $null }
        checker_retention = if ($metricMatch.Success) { [double]$metricMatch.Groups["checker"].Value } else { $null }
        reduced_coordinate_error = if ($metricMatch.Success) { [double]$metricMatch.Groups["reduced"].Value } else { $null }
        inverse_error = if ($metricMatch.Success) { [double]$metricMatch.Groups["inverse"].Value } else { $null }
        automation_report = $automationPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "模态子空间验证失败，详见：$reportPath"
}
Write-Output "M2_MODAL_SUBSPACE_VALIDATION_PASSED report=$reportPath"
