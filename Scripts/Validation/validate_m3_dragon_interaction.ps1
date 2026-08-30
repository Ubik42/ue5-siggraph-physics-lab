param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$runtimePath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_gpu_surface_interaction_runtime.json"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M3\Screenshots\M3_DragonGpuSurface_Interaction.png"
$manualJsonPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M3_DragonGpuSurfaceManual.json"
$manualCsvPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M3_DragonGpuSurfaceManual.csv"
$validationPath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_gpu_surface_interaction_validation.json"
foreach ($path in @($runtimePath, $screenshotPath, $manualJsonPath, $manualCsvPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M3 Dragon 交互证据：$path" }
}
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$screenshot = Get-Item -LiteralPath $screenshotPath
$manualJson = Get-Content -LiteralPath $manualJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$manualCsvLines = @(Get-Content -LiteralPath $manualCsvPath -Encoding UTF8)
$checks = [ordered]@{
    interaction_run_passed = ($runtime.result -eq "passed" -and $runtime.interaction_script -eq $true)
    pause_resume_exercised = ($runtime.pause_toggles -eq 2)
    exactly_one_single_step_exercised = ($runtime.single_steps -eq 1)
    reset_exercised = ($runtime.resets -eq 1)
    debug_visibility_round_trip_exercised = ($runtime.debug_toggles -eq 2 -and $runtime.debug_draw_enabled_at_end -eq $true)
    manual_export_exercised = ($runtime.manual_exports -eq 1 -and @($manualJson.samples).Count -gt 0 -and $manualCsvLines.Count -gt 1)
    post_reset_run_reached_final_step = ($runtime.steps_executed -eq 300 -and $runtime.latest_readback_step -eq 300)
    interaction_did_not_break_physics_gates = ($runtime.surface_vertices -eq 4854 -and $runtime.surface_updates -gt 0 -and [double]$runtime.maximum_surface_displacement_cm -gt 1.0 -and [double]$runtime.maximum_fixed_face_drift_cm -lt 0.001)
    screenshot_is_nontrivial = ($screenshot.Length -gt 100000)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    controls = [ordered]@{ pause_resume = "Space"; single_step = "N"; reset = "R"; debug_draw = "D"; export = "E" }
    checks = $checks
    runtime = $runtime
    manual_export_samples = @($manualJson.samples).Count
    manual_export_csv_rows = [Math]::Max(0, $manualCsvLines.Count - 1)
    screenshot_bytes = $screenshot.Length
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $validationPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 Dragon 交互验证失败：$($failed -join ', ')" }
Write-Output "M3_DRAGON_INTERACTION_VALIDATION_PASSED report=$validationPath"
