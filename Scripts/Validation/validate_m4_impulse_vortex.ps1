param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "ImpulseVortexAutomation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "impulse_vortex_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 impulse vortex 证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.ImpulseVortexClosedLoop")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap impulse vortex*" })[0]
$number = "[0-9.eE+-]+"
$pattern = "steps=(?<steps>\d+) cells=(?<cells>\S+) particles=(?<particles>\d+) dt=(?<dt>$number) pressure_iterations=(?<pressure>\d+) short_resets=(?<shortresets>\d+) long_resets=(?<longresets>\d+) short_velocity_rms=(?<shortrms>$number) long_pic_velocity_rms=(?<longpicrms>$number) long_gradient_velocity_rms=(?<longgradientrms>$number) long_pic_to_short_rms=(?<picratio>$number) long_gradient_to_long_pic_rms=(?<gradientratio>$number) short_relative_rms=(?<shortrelative>$number) long_pic_relative_rms=(?<longpicrelative>$number) long_gradient_relative_rms=(?<longgradientrelative>$number) short_divergence_rms=(?<shortdiv>$number) long_pic_divergence_rms=(?<longpicdiv>$number) long_gradient_divergence_rms=(?<longgradientdiv>$number) short_max_divergence=(?<shortmaxdiv>$number) long_pic_max_divergence=(?<longpicmaxdiv>$number) long_gradient_max_divergence=(?<longgradientmaxdiv>$number) initial_energy=(?<initialenergy>$number) short_energy=(?<shortenergy>$number) long_pic_energy=(?<longpicenergy>$number) long_gradient_energy=(?<longgradientenergy>$number) long_gradient_jacobian_det_error_max=(?<jacobian>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    full_flow_map_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    identical_closed_loop_budget_is_exact = ([int]$match.Groups["steps"].Value -eq 120 -and $match.Groups["cells"].Value -eq "32x32" -and [int]$match.Groups["particles"].Value -eq 4096 -and [int]$match.Groups["pressure"].Value -eq 128)
    reset_cadence_is_exact = ([int]$match.Groups["shortresets"].Value -eq 120 -and [int]$match.Groups["longresets"].Value -eq 6)
    every_step_pressure_projection_keeps_all_divergence_bounded = ([double]$match.Groups["shortdiv"].Value -lt 1.e-3 -and [double]$match.Groups["longpicdiv"].Value -lt 1.e-3 -and [double]$match.Groups["longgradientdiv"].Value -lt 1.e-3 -and [double]$match.Groups["longgradientmaxdiv"].Value -lt 1.e-3)
    long_map_pic_velocity_error_is_lower_than_short_pic = ([double]$match.Groups["picratio"].Value -lt 1.0 -and [double]$match.Groups["longpicrelative"].Value -lt 0.2)
    vector_gradient_term_isolated_at_same_map_cadence = ([double]$match.Groups["gradientratio"].Value -lt 0.75 -and [double]$match.Groups["longgradientrelative"].Value -lt 0.1)
    gradient_long_map_retains_vortex_energy_within_two_percent = ([double]$match.Groups["longgradientenergy"].Value -gt [double]$match.Groups["shortenergy"].Value -and [double]$match.Groups["longgradientenergy"].Value -gt [double]$match.Groups["initialenergy"].Value * 0.98 -and [double]$match.Groups["longgradientenergy"].Value -lt [double]$match.Groups["initialenergy"].Value * 1.02)
    backward_map_jacobian_remains_near_volume_preserving = ([double]$match.Groups["jacobian"].Value -lt 1.e-3)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "Particles are advected by the current nonuniform MAC velocity, carry reference impulse, its first spatial gradient, and backward-map Jacobian, deposit B^T m0 with optional B^T G0 B Taylor reconstruction, and pressure-project every fixed step. Short PIC, LongMapPic, and LongMapGradient isolate reset cadence and the vector gradient term."
    checks = $checks
    contract = [ordered]@{ steps = [int]$match.Groups["steps"].Value; cells = $match.Groups["cells"].Value; particles = [int]$match.Groups["particles"].Value; fixed_dt_seconds = [double]$match.Groups["dt"].Value; pressure_iterations_per_step = [int]$match.Groups["pressure"].Value }
    short_pic = [ordered]@{ map_resets = [int]$match.Groups["shortresets"].Value; velocity_rms_cm_per_second = [double]$match.Groups["shortrms"].Value; relative_velocity_rms = [double]$match.Groups["shortrelative"].Value; divergence_rms = [double]$match.Groups["shortdiv"].Value; maximum_divergence = [double]$match.Groups["shortmaxdiv"].Value; kinetic_energy_proxy = [double]$match.Groups["shortenergy"].Value }
    long_map_pic = [ordered]@{ map_resets = [int]$match.Groups["longresets"].Value; velocity_rms_cm_per_second = [double]$match.Groups["longpicrms"].Value; relative_velocity_rms = [double]$match.Groups["longpicrelative"].Value; rms_to_short_pic = [double]$match.Groups["picratio"].Value; divergence_rms = [double]$match.Groups["longpicdiv"].Value; maximum_divergence = [double]$match.Groups["longpicmaxdiv"].Value; kinetic_energy_proxy = [double]$match.Groups["longpicenergy"].Value }
    long_map_gradient = [ordered]@{ map_resets = [int]$match.Groups["longresets"].Value; velocity_rms_cm_per_second = [double]$match.Groups["longgradientrms"].Value; relative_velocity_rms = [double]$match.Groups["longgradientrelative"].Value; rms_to_long_map_pic = [double]$match.Groups["gradientratio"].Value; divergence_rms = [double]$match.Groups["longgradientdiv"].Value; maximum_divergence = [double]$match.Groups["longgradientmaxdiv"].Value; kinetic_energy_proxy = [double]$match.Groups["longgradientenergy"].Value; maximum_jacobian_determinant_error = [double]$match.Groups["jacobian"].Value }
    initial_kinetic_energy_proxy = [double]$match.Groups["initialenergy"].Value
    boundary = "This is a 2D periodic inviscid Taylor-Green CPU truth loop with first-order backward-Jacobian integration and a first-order gradient impulse transfer that omits derivatives of B (the Hessian-like term). A separate gate now validates GPU pressure projection, but the full particle/Map loop is not yet GPU or a complete paper implementation."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 impulse vortex 验证失败：$($failed -join ', ')" }
Write-Output "M4_IMPULSE_VORTEX_VALIDATION_PASSED report=$reportPath"
