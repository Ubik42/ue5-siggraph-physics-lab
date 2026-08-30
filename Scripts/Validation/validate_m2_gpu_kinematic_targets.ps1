param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuKinematicTargetsAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_kinematic_targets_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 GPU 运动学目标报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.KinematicTargets" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU kinematic cloth targets:*" })[0]
$pattern = "particles=(?<particles>\d+) fixed=(?<fixed>\d+) dynamic=(?<dynamic>\d+) steps=(?<steps>\d+) target_updates=(?<updates>\d+) target_applications=(?<applications>\d+) target_version=(?<version>\d+) final_readback_step=(?<final>\d+) fixed_target_error_cm=(?<targeterror>[0-9.eE+-]+) minimum_fixed_motion_cm=(?<minmotion>[0-9.eE+-]+) average_dynamic_displacement_cm=(?<dynamicmotion>[0-9.eE+-]+) maximum_edge_error_cm=(?<edgeerror>[0-9.eE+-]+)"
$match = [regex]::Match($message, $pattern)
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = ($automation.devices[0].rHI -eq "DirectX 12 (SM6)")
    metrics_present = $match.Success
    exact_assassin_partition = ($match.Success -and [int]$match.Groups["particles"].Value -eq 2011 -and [int]$match.Groups["fixed"].Value -eq 1307 -and [int]$match.Groups["dynamic"].Value -eq 704)
    every_step_executed = ($match.Success -and [int]$match.Groups["steps"].Value -eq 30 -and [int]$match.Groups["final"].Value -eq 30)
    every_target_update_applied = ($match.Success -and [int]$match.Groups["updates"].Value -eq 30 -and [int]$match.Groups["applications"].Value -eq 30 -and [int]$match.Groups["version"].Value -eq 30)
    fixed_particles_match_targets = ($match.Success -and (Metric "targeterror") -lt 1.e-5)
    all_fixed_targets_moved = ($match.Success -and (Metric "minmotion") -gt 1.0)
    dynamic_particles_follow_boundary = ($match.Success -and (Metric "dynamicmotion") -gt 0.1)
    structural_error_controlled = ($match.Success -and [double]::IsFinite((Metric "edgeerror")) -and (Metric "edgeerror") -lt 15.0)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = if ($match.Success) { [int]$match.Groups["particles"].Value } else { $null }
        fixed_vertices = if ($match.Success) { [int]$match.Groups["fixed"].Value } else { $null }
        dynamic_vertices = if ($match.Success) { [int]$match.Groups["dynamic"].Value } else { $null }
        fixed_steps = if ($match.Success) { [int]$match.Groups["steps"].Value } else { $null }
        target_updates = if ($match.Success) { [int]$match.Groups["updates"].Value } else { $null }
        target_applications = if ($match.Success) { [int]$match.Groups["applications"].Value } else { $null }
        fixed_target_error_cm = if ($match.Success) { Metric "targeterror" } else { $null }
        minimum_fixed_target_motion_cm = if ($match.Success) { Metric "minmotion" } else { $null }
        average_dynamic_displacement_cm = if ($match.Success) { Metric "dynamicmotion" } else { $null }
        maximum_edge_error_cm = if ($match.Success) { Metric "edgeerror" } else { $null }
        scope = "Per-fixed-step UE Clothing skinning target upload and persistent GPU application gate; live component animation, collision, and Chaos time-series comparison remain pending"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "GPU 运动学目标验证失败：$reportPath" }
Write-Output "M2_GPU_KINEMATIC_TARGETS_VALIDATION_PASSED report=$reportPath"
