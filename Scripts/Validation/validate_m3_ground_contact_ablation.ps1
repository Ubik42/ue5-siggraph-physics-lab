param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$evidenceRoot = Join-Path $ProjectRoot "Docs\Validation\M3"
$standardPath = Join-Path $evidenceRoot "dragon_gpu_surface_contact_runtime.json"
$weakPath = Join-Path $evidenceRoot "dragon_gpu_surface_weak_contact_runtime.json"
$automationPath = Join-Path $evidenceRoot "GpuGroundContactD3D12Automation\index.json"
$standardScreenshotPath = Join-Path $evidenceRoot "Screenshots\M3_DragonGpuSurface_Contact.png"
$weakScreenshotPath = Join-Path $evidenceRoot "Screenshots\M3_DragonGpuSurface_WeakContact.png"
$validationPath = Join-Path $evidenceRoot "ground_contact_ablation_validation.json"

foreach ($path in @($standardPath, $weakPath, $automationPath, $standardScreenshotPath, $weakScreenshotPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M3 地面接触消融证据：$path" }
}

$standard = Get-Content -LiteralPath $standardPath -Raw -Encoding UTF8 | ConvertFrom-Json
$weak = Get-Content -LiteralPath $weakPath -Raw -Encoding UTF8 | ConvertFrom-Json
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$standardScreenshot = Get-Item -LiteralPath $standardScreenshotPath
$weakScreenshot = Get-Item -LiteralPath $weakScreenshotPath
$automationMessage = [string]$automation.tests[0].entries[0].event.message

$checks = [ordered]@{
    real_d3d12_sm6_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $automation.devices[0].renderMode -eq "D3D12_SM6")
    module_ablation_logged_exact_metrics = ($automationMessage -match "standard_max_penetration_cm=0\.000000000" -and $automationMessage -match "weak_max_penetration_cm=7\.999312401")
    same_dragon_topology_and_step_budget = ($standard.steps_executed -eq 300 -and $weak.steps_executed -eq 300 -and $standard.surface_vertices -eq $weak.surface_vertices -and $standard.surface_triangles -eq $weak.surface_triangles -and $standard.cage_vertices -eq $weak.cage_vertices -and $standard.cage_tetrahedra -eq $weak.cage_tetrahedra)
    only_contact_stiffness_changes = ($standard.single_parameter_ablation -eq $weak.single_parameter_ablation -and [double]$standard.ground_contact_stiffness -eq 1.0 -and [double]$weak.ground_contact_stiffness -eq 0.02 -and [double]$standard.ground_plane_z_cm -eq [double]$weak.ground_plane_z_cm)
    exactly_one_contact_projection_per_fixed_step = ($standard.ground_contact_passes -eq 300 -and $weak.ground_contact_passes -eq 300)
    standard_contact_blocks_penetration = ($standard.result -eq "passed" -and [double]$standard.maximum_ground_penetration_cm -lt 0.001 -and $standard.active_ground_contact_vertices -gt 0)
    weak_contact_expected_failure_is_observed = ($weak.result -eq "passed" -and $weak.expected_failure_observed -eq $true -and [double]$weak.maximum_ground_penetration_cm -gt 0.5)
    fixed_boundary_remains_exact = ([double]$standard.maximum_fixed_face_drift_cm -lt 0.001 -and [double]$weak.maximum_fixed_face_drift_cm -lt 0.001)
    both_screenshots_are_nontrivial = ($standardScreenshot.Length -gt 100000 -and $weakScreenshot.Length -gt 100000)
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "The weak profile passing means the expected-failure experiment was observed, not that weak contact is physically acceptable."
    controlled_variable = "ground_contact_stiffness: 1.0 versus 0.02"
    checks = $checks
    standard = $standard
    weak = $weak
    automation_metric_line = $automationMessage
    screenshot_bytes = [ordered]@{ standard = $standardScreenshot.Length; weak = $weakScreenshot.Length }
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $validationPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 地面接触消融验证失败：$($failed -join ', ')" }
Write-Output "M3_GROUND_CONTACT_ABLATION_VALIDATION_PASSED report=$validationPath"
