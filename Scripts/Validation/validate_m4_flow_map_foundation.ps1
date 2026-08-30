param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "flow_map_foundation_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 M4 Flow Map 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$straight = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GridAndParticleContract")
$vortex = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.SolidBodyVortex")
$picProjection = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.PicTransferAndPressureProjection")
$gradientTransfer = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GradientEnhancedTransfer")
$straightMessage = @($straight[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap straight*" })[0]
$vortexMessage = @($vortex[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap vortex*" })[0]
$picProjectionMessage = @($picProjection[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap PIC+projection*" })[0]
$gradientTransferMessage = @($gradientTransfer[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap gradient transfer*" })[0]

$number = "[0-9.eE+-]+"
$straightPattern = "cells=(?<cells>\S+) particles=(?<particles>\d+) steps=(?<steps>\d+) divergence_rms=(?<div>$number) displacement_cm=(?<displacement>$number) short_resets=(?<short>\d+) long_resets=(?<long>\d+) lost=(?<lost>\d+)"
$vortexPattern = "cells=(?<cells>\S+) particles=(?<particles>\d+) steps=(?<steps>\d+) boundary=(?<boundary>\S+) divergence_rms=(?<div>$number) radius_error=(?<radius>$number) jacobian_det_error_max=(?<jacobian>$number) short_resets=(?<short>\d+) long_resets=(?<long>\d+) lost=(?<lost>\d+) wraps=(?<wraps>\d+)"
$picProjectionPattern = "cells=(?<cells>\S+) particles=(?<particles>\d+) pic_max_velocity_error=(?<pic>$number) projection_iterations=(?<iterations>\d+) divergence_rms_before=(?<before>$number) divergence_rms_after=(?<after>$number) divergence_ratio=(?<ratio>$number) max_divergence_before=(?<maxbefore>$number) max_divergence_after=(?<maxafter>$number) energy_before=(?<energybefore>$number) energy_after=(?<energyafter>$number)"
$gradientPattern = "cells=(?<cells>\S+) particles=(?<particles>\d+) distribution=(?<distribution>\S+) field=(?<field>\S+) pic_rms=(?<pic>$number) gradient_rms=(?<gradient>$number) gradient_to_pic_ratio=(?<ratio>$number)"
$sm = [regex]::Match($straightMessage, $straightPattern)
$vm = [regex]::Match($vortexMessage, $vortexPattern)
$pm = [regex]::Match($picProjectionMessage, $picProjectionPattern)
$gm = [regex]::Match($gradientTransferMessage, $gradientPattern)

$checks = [ordered]@{
    automation_includes_all_four_foundation_tests = ($automation.succeeded -eq 15 -and $automation.failed -eq 0 -and $straight[0].state -eq "Success" -and $vortex[0].state -eq "Success" -and $picProjection[0].state -eq "Success" -and $gradientTransfer[0].state -eq "Success")
    metric_lines_are_machine_readable = ($sm.Success -and $vm.Success -and $pm.Success -and $gm.Success)
    straight_mac_contract_is_exact = ($sm.Groups["cells"].Value -eq "16x8" -and [int]$sm.Groups["particles"].Value -eq 512 -and [int]$sm.Groups["steps"].Value -eq 6)
    straight_is_divergence_free_and_analytic = ([double]$sm.Groups["div"].Value -lt 1.e-8 -and [double]$sm.Groups["displacement"].Value -eq 2.0)
    straight_map_lifecycle_is_exact = ([int]$sm.Groups["short"].Value -eq 2 -and [int]$sm.Groups["long"].Value -eq 0 -and [int]$sm.Groups["lost"].Value -eq 0)
    vortex_periodic_contract_is_exact = ($vm.Groups["cells"].Value -eq "32x32" -and [int]$vm.Groups["particles"].Value -eq 1024 -and [int]$vm.Groups["steps"].Value -eq 60 -and $vm.Groups["boundary"].Value -eq "periodic")
    vortex_preserves_radius_and_volume_proxy = ([double]$vm.Groups["div"].Value -lt 1.e-7 -and [double]$vm.Groups["radius"].Value -lt 1.e-3 -and [double]$vm.Groups["jacobian"].Value -lt 1.e-3)
    vortex_map_lifecycle_is_exact = ([int]$vm.Groups["short"].Value -eq 15 -and [int]$vm.Groups["long"].Value -eq 3 -and [int]$vm.Groups["lost"].Value -eq 0 -and [int]$vm.Groups["wraps"].Value -gt 0)
    normalized_pic_reconstructs_uniform_velocity = ($pm.Groups["cells"].Value -eq "32x16" -and [int]$pm.Groups["particles"].Value -eq 2048 -and [double]$pm.Groups["pic"].Value -lt 1.e-5)
    periodic_projection_removes_at_least_99_9_percent_divergence = ([int]$pm.Groups["iterations"].Value -eq 1024 -and [double]$pm.Groups["before"].Value -gt 0.1 -and [double]$pm.Groups["ratio"].Value -lt 1.e-3 -and [double]$pm.Groups["maxafter"].Value -lt [double]$pm.Groups["maxbefore"].Value * 1.e-3)
    compression_energy_is_removed_by_projection = ([double]$pm.Groups["energyafter"].Value -lt [double]$pm.Groups["energybefore"].Value * 1.e-4)
    gradient_transfer_uses_deterministic_distorted_particles = ($gm.Groups["cells"].Value -eq "32x32" -and [int]$gm.Groups["particles"].Value -eq 1024 -and $gm.Groups["distribution"].Value -eq "deterministic_jittered")
    gradient_term_reduces_scalar_transfer_error_by_at_least_25_percent = ([double]$gm.Groups["gradient"].Value -lt 0.02 -and [double]$gm.Groups["ratio"].Value -lt 0.75)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    scope = "Case 03 CPU foundation: staggered MAC velocity, deterministic particles, RK2 advection, long/short map lifecycle, backward-map Jacobian, clamp and periodic boundaries."
    checks = $checks
    straight = [ordered]@{
        cells = $sm.Groups["cells"].Value; particles = [int]$sm.Groups["particles"].Value; steps = [int]$sm.Groups["steps"].Value
        divergence_rms = [double]$sm.Groups["div"].Value; displacement_cm = [double]$sm.Groups["displacement"].Value
        short_map_resets = [int]$sm.Groups["short"].Value; long_map_resets = [int]$sm.Groups["long"].Value; lost_particles = [int]$sm.Groups["lost"].Value
    }
    vortex = [ordered]@{
        cells = $vm.Groups["cells"].Value; particles = [int]$vm.Groups["particles"].Value; steps = [int]$vm.Groups["steps"].Value; boundary = $vm.Groups["boundary"].Value
        divergence_rms = [double]$vm.Groups["div"].Value; radius_error = [double]$vm.Groups["radius"].Value; maximum_jacobian_determinant_error = [double]$vm.Groups["jacobian"].Value
        short_map_resets = [int]$vm.Groups["short"].Value; long_map_resets = [int]$vm.Groups["long"].Value; lost_particles = [int]$vm.Groups["lost"].Value; boundary_wraps = [int]$vm.Groups["wraps"].Value
    }
    pic_and_projection = [ordered]@{
        cells = $pm.Groups["cells"].Value; particles = [int]$pm.Groups["particles"].Value; pic_max_velocity_error = [double]$pm.Groups["pic"].Value
        projection_iterations = [int]$pm.Groups["iterations"].Value; divergence_rms_before = [double]$pm.Groups["before"].Value; divergence_rms_after = [double]$pm.Groups["after"].Value
        divergence_ratio = [double]$pm.Groups["ratio"].Value; maximum_divergence_before = [double]$pm.Groups["maxbefore"].Value; maximum_divergence_after = [double]$pm.Groups["maxafter"].Value
        energy_before = [double]$pm.Groups["energybefore"].Value; energy_after = [double]$pm.Groups["energyafter"].Value
    }
    gradient_transfer = [ordered]@{
        cells = $gm.Groups["cells"].Value; particles = [int]$gm.Groups["particles"].Value; distribution = $gm.Groups["distribution"].Value; field = $gm.Groups["field"].Value
        pic_rms = [double]$gm.Groups["pic"].Value; gradient_rms = [double]$gm.Groups["gradient"].Value; gradient_to_pic_ratio = [double]$gm.Groups["ratio"].Value
    }
    current_boundary = "This report covers the CPU foundation only. Later gates now add evolving vector impulse/gradient feedback and a real D3D12 pressure-projection subpath; the full particle/Map GPU loop, UE map, moving Chaos obstacle, and Niagara visualization remain future work."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 Flow Map 基础验证失败：$($failed -join ', ')" }
Write-Output "M4_FLOW_MAP_FOUNDATION_VALIDATION_PASSED report=$reportPath"
