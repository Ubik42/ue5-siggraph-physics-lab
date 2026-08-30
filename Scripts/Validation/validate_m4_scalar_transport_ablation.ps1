param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "ScalarTransportAutomation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "scalar_transport_ablation_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 标量演化证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$fullAutomation = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.EvolvingScalarTransportAblation")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap evolving scalar*" })[0]
$number = "[0-9.eE+-]+"
$pattern = "steps=(?<steps>\d+) cells=(?<cells>\S+) particles=(?<particles>\d+) dt=(?<dt>$number) velocity_cm_s=(?<velocity>\S+) field_cycles=(?<cycles>\S+) pic_resets=(?<picresets>\d+) long_resets=(?<longresets>\d+) pic_rms=(?<picrms>$number) long_pic_rms=(?<longpicrms>$number) long_gradient_rms=(?<longrms>$number) long_gradient_to_pic_rms=(?<ratio>$number) long_gradient_to_long_pic_rms=(?<gradientratio>$number) pic_relative_rms=(?<picrelative>$number) long_gradient_relative_rms=(?<longrelative>$number) pic_amplitude=(?<picamplitude>$number) long_pic_amplitude=(?<longpicamplitude>$number) long_gradient_amplitude=(?<longamplitude>$number) exact_amplitude=(?<exactamplitude>$number) pic_energy=(?<picenergy>$number) long_pic_energy=(?<longpicenergy>$number) long_gradient_energy=(?<longenergy>$number) exact_energy=(?<exactenergy>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    full_flow_map_regression_is_fifteen_of_fifteen = ($fullAutomation.succeeded -eq 15 -and $fullAutomation.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    identical_grid_particles_steps_dt_and_velocity = ([int]$match.Groups["steps"].Value -eq 120 -and $match.Groups["cells"].Value -eq "32x32" -and [int]$match.Groups["particles"].Value -eq 4096 -and [double]$match.Groups["dt"].Value -gt 0.016 -and $match.Groups["velocity"].Value -eq "20,10")
    reset_cadence_is_exact = ([int]$match.Groups["picresets"].Value -eq 120 -and [int]$match.Groups["longresets"].Value -eq 6)
    long_map_gradient_reduces_error_at_least_fifty_percent = ([double]$match.Groups["ratio"].Value -lt 0.5)
    gradient_term_isolated_at_same_long_map_cadence = ([double]$match.Groups["gradientratio"].Value -lt 0.75)
    long_map_relative_error_is_below_twenty_percent = ([double]$match.Groups["longrelative"].Value -lt 0.2)
    long_map_preserves_material_amplitude = ([double]$match.Groups["longamplitude"].Value -gt [double]$match.Groups["picamplitude"].Value + 0.1)
    short_pic_expected_dissipation_is_observed = ([double]$match.Groups["picrelative"].Value -gt 0.9 -and [double]$match.Groups["picamplitude"].Value -lt 0.01)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "All three paths share the same periodic translation, grid, particles, fixed step, and scalar field. Short PIC resets every step; LongMapPic and LongMapGradient reset every 20 steps, isolating map cadence and the gradient term."
    checks = $checks
    contract = [ordered]@{ steps = [int]$match.Groups["steps"].Value; cells = $match.Groups["cells"].Value; particles = [int]$match.Groups["particles"].Value; fixed_dt_seconds = [double]$match.Groups["dt"].Value; velocity_cm_per_second = $match.Groups["velocity"].Value; field_cycles = $match.Groups["cycles"].Value }
    short_pic = [ordered]@{ map_resets = [int]$match.Groups["picresets"].Value; rms_error = [double]$match.Groups["picrms"].Value; relative_rms_error = [double]$match.Groups["picrelative"].Value; amplitude = [double]$match.Groups["picamplitude"].Value; energy_proxy = [double]$match.Groups["picenergy"].Value }
    long_map_pic = [ordered]@{ map_resets = [int]$match.Groups["longresets"].Value; rms_error = [double]$match.Groups["longpicrms"].Value; amplitude = [double]$match.Groups["longpicamplitude"].Value; energy_proxy = [double]$match.Groups["longpicenergy"].Value }
    long_map_gradient = [ordered]@{ map_resets = [int]$match.Groups["longresets"].Value; rms_error = [double]$match.Groups["longrms"].Value; relative_rms_error = [double]$match.Groups["longrelative"].Value; amplitude = [double]$match.Groups["longamplitude"].Value; energy_proxy = [double]$match.Groups["longenergy"].Value; rms_to_short_pic = [double]$match.Groups["ratio"].Value; rms_to_long_map_pic = [double]$match.Groups["gradientratio"].Value }
    exact = [ordered]@{ amplitude = [double]$match.Groups["exactamplitude"].Value; energy_proxy = [double]$match.Groups["exactenergy"].Value }
    boundary = "This is a periodic passive-scalar/impulse-component transport benchmark under prescribed uniform velocity. It does not yet couple vector impulse transport back into an evolving pressure-projected velocity field."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 演化态标量消融失败：$($failed -join ', ')" }
Write-Output "M4_SCALAR_TRANSPORT_ABLATION_PASSED report=$reportPath"
