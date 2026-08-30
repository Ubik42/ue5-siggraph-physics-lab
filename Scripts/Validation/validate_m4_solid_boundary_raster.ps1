param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "SolidBoundaryAutomation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "solid_boundary_raster_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 运动固体边界证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.KinematicSolidBoundaryRaster")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap kinematic solid raster*" })[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsFlowMap\Source\PhysicsFlowMapRuntime\Private\ParticleFlowMap2D.cpp"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$number = "[0-9.eE+-]+"
$pattern = "cells=(?<cells>\S+) radius_cm=(?<radius>$number) center=\((?<cx>$number),(?<cy>$number)\) linear_velocity=\((?<vx>$number),(?<vy>$number)\) angular_velocity=(?<omega>$number) solid_cells=(?<solidcells>\d+) solid_u_faces=(?<solidu>\d+) solid_v_faces=(?<solidv>\d+) max_no_slip_residual=(?<residual>$number) source=(?<source>\S+)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    rigid_transform_difference_recovers_translation = ([math]::Abs([double]$match.Groups["vx"].Value - 20.0) -lt 1.e-5 -and [math]::Abs([double]$match.Groups["vy"].Value) -lt 1.e-6)
    ue_yaw_recovers_counterclockwise_angular_velocity = ([math]::Abs([double]$match.Groups["omega"].Value - [math]::PI / 3.0) -lt 1.e-5)
    staggered_raster_counts_are_interface_inclusive = ($match.Groups["cells"].Value -eq "16x16" -and [int]$match.Groups["solidcells"].Value -eq 16 -and [int]$match.Groups["solidu"].Value -eq 20 -and [int]$match.Groups["solidv"].Value -eq 20)
    all_solid_face_velocities_match_rigid_no_slip_motion = ([double]$match.Groups["residual"].Value -lt 1.e-6)
    source_contract_is_ue_chaos_rigid_body_transforms = ($match.Groups["source"].Value -eq "ue_chaos_rigid_body_transforms")
    invalid_fixed_step_is_rejected_before_rasterization = ($source -match "FixedDeltaSeconds <= 0\.0f")
    raster_has_separate_cell_u_and_v_masks = ($source -match "SolidCellMask" -and $source -match "SolidFaceUMask" -and $source -match "SolidFaceVMask")
    face_velocity_uses_2d_rigid_cross_product = ($source -match "LinearVelocityCmPerSecond\.X - Circle\.AngularVelocityRadiansPerSecond" -and $source -match "LinearVelocityCmPerSecond\.Y \+ Circle\.AngularVelocityRadiansPerSecond")
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "Previous/current UE rigid-body transforms are differenced at the fixed step to recover planar translation and UE Z/Yaw angular velocity. A moving circle is then rasterized independently at cell centers, U faces, and V faces, with every covered face carrying the analytic rigid no-slip velocity."
    checks = $checks
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; radius_cm = [double]$match.Groups["radius"].Value; source = $match.Groups["source"].Value }
    motion = [ordered]@{ center_cm = @([double]$match.Groups["cx"].Value,[double]$match.Groups["cy"].Value); linear_velocity_cm_per_second = @([double]$match.Groups["vx"].Value,[double]$match.Groups["vy"].Value); angular_velocity_radians_per_second = [double]$match.Groups["omega"].Value }
    raster = [ordered]@{ solid_cells = [int]$match.Groups["solidcells"].Value; solid_u_faces = [int]$match.Groups["solidu"].Value; solid_v_faces = [int]$match.Groups["solidv"].Value; maximum_no_slip_residual_cm_per_second = [double]$match.Groups["residual"].Value }
    boundary = "This is the CPU truth and UE/Chaos pose adapter for boundary data. The persistent GPU Flow Map session does not consume these masks yet, pressure stencils are still periodic, and no runtime Actor or Chaos body has been wired to UpdateBoundary in a map."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 运动固体边界栅格验证失败：$($failed -join ', ')" }
Write-Output "M4_SOLID_BOUNDARY_RASTER_VALIDATION_PASSED report=$reportPath"
