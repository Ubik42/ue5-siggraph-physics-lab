param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M3"
$cpuPath = Join-Path $root "dragon_cpu_local_xpbd_reference_runtime.json"
$togglePath = Join-Path $root "dragon_backend_toggle_runtime.json"
$gpuPath = Join-Path $root "dragon_gpu_surface_contact_runtime.json"
$automationPath = Join-Path $root "CpuContactReferenceAutomation\index.json"
$cpuScreenshotPath = Join-Path $root "Screenshots\M3_DragonCpuLocalXpbdReference.png"
$toggleScreenshotPath = Join-Path $root "Screenshots\M3_DragonBackendToggle.png"
$validationPath = Join-Path $root "cpu_reference_backend_validation.json"
foreach ($path in @($cpuPath, $togglePath, $gpuPath, $automationPath, $cpuScreenshotPath, $toggleScreenshotPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M3 CPU 参考后端证据：$path" }
}

$cpu = Get-Content -LiteralPath $cpuPath -Raw -Encoding UTF8 | ConvertFrom-Json
$toggle = Get-Content -LiteralPath $togglePath -Raw -Encoding UTF8 | ConvertFrom-Json
$gpu = Get-Content -LiteralPath $gpuPath -Raw -Encoding UTF8 | ConvertFrom-Json
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$cpuScreenshot = Get-Item -LiteralPath $cpuScreenshotPath
$toggleScreenshot = Get-Item -LiteralPath $toggleScreenshotPath
$cpuAutomationMessage = [string]$automation.tests[0].entries[0].event.message

$checks = [ordered]@{
    cpu_contact_semantics_pass_under_nullrhi = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $cpuAutomationMessage -match "standard=0\.000000000cm" -and $cpuAutomationMessage -match "weak=8\.807254967cm")
    reference_is_honestly_not_chaos_flesh = ($cpu.not_chaos_flesh -eq $true -and $cpu.backend -eq "CPU local XPBD research reference" -and $cpu.chaos_flesh_status -like "*ChaosFleshNodes*")
    execution_contract_passes_but_quality_does_not = ($cpu.result -eq "passed_expected_failure" -and $cpu.execution_contract_passed -eq $true -and $cpu.physics_quality_gate_passed -eq $false -and $cpu.expected_local_propagation_failure_observed -eq $true)
    exact_same_input_contract_as_gpu = ($cpu.steps_executed -eq $gpu.steps_executed -and $cpu.surface_vertices -eq $gpu.surface_vertices -and $cpu.surface_triangles -eq $gpu.surface_triangles -and $cpu.cage_vertices -eq $gpu.cage_vertices -and $cpu.cage_tetrahedra -eq $gpu.cage_tetrahedra -and [double]$cpu.ground_plane_z_cm -eq [double]$gpu.ground_plane_z_cm -and [double]$cpu.ground_contact_stiffness -eq [double]$gpu.ground_contact_stiffness)
    cpu_executes_all_semantic_passes = ($cpu.surface_updates -eq 300 -and $cpu.ground_contact_passes -eq 300 -and $cpu.low_frequency_modes -eq 2 -and $cpu.low_frequency_projection_passes -eq 9600)
    local_propagation_failure_is_material = ([double]$cpu.maximum_surface_displacement_cm -gt 200.0 -and [double]$gpu.maximum_surface_displacement_cm -lt 100.0)
    fixed_boundary_and_contact_stay_valid = ([double]$cpu.maximum_fixed_face_drift_cm -lt 0.001 -and [double]$cpu.maximum_ground_penetration_cm -lt 0.001 -and $cpu.active_ground_contact_vertices -gt 0)
    cpu_timing_is_measured_and_finite = ([double]::IsFinite([double]$cpu.mean_cpu_step_ms) -and [double]$cpu.mean_cpu_step_ms -gt 0.0 -and [double]$cpu.maximum_cpu_step_ms -ge [double]$cpu.mean_cpu_step_ms)
    b_key_rebuilds_and_completes_cpu_session = ($toggle.backend_toggle_script -eq $true -and $toggle.backend_toggles -eq 1 -and $toggle.steps_executed -eq 300 -and $toggle.execution_contract_passed -eq $true)
    screenshots_are_nontrivial = ($cpuScreenshot.Length -gt 100000 -and $toggleScreenshot.Length -gt 100000)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "CPU reference is a real alternate runtime backend and an expected physics-quality failure at the same iteration budget; it is not Chaos Flesh."
    chaos_flesh_boundary = "UE 5.4.4 ChaosFlesh plugin cannot be enabled through normal project dependencies without also loading the recorded defective ChaosFleshNodes module."
    checks = $checks
    gpu_standard = $gpu
    cpu_reference = $cpu
    backend_toggle = $toggle
    cpu_automation_message = $cpuAutomationMessage
    screenshot_bytes = [ordered]@{ cpu_reference = $cpuScreenshot.Length; backend_toggle = $toggleScreenshot.Length }
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $validationPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 CPU 参考后端验证失败：$($failed -join ', ')" }
Write-Output "M3_CPU_REFERENCE_BACKEND_VALIDATION_PASSED report=$validationPath"
