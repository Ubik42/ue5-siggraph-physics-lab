param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$runtimePath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_gpu_surface_runtime.json"
$mapPath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_gpu_surface_map_setup.json"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M3\Screenshots\M3_DragonGpuSurface.png"
$mapAssetPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsSoftBody\Content\Maps\L_DragonGpuSurface.umap"
$validationPath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_gpu_surface_runtime_validation.json"
foreach ($path in @($runtimePath, $mapPath, $screenshotPath, $mapAssetPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M3 Dragon GPU 表面运行证据：$path" }
}
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$map = Get-Content -LiteralPath $mapPath -Raw -Encoding UTF8 | ConvertFrom-Json
$screenshot = Get-Item -LiteralPath $screenshotPath
$checks = [ordered]@{
    runtime_passed = ($runtime.result -eq "passed")
    exact_fixed_step_alignment = ($runtime.steps_executed -eq 300 -and $runtime.latest_readback_step -eq 300)
    asynchronous_surface_updates_observed = ($runtime.readbacks_completed -gt 0 -and $runtime.surface_updates -gt 0 -and $runtime.surface_updates -le $runtime.readbacks_completed)
    exact_dragon_surface_contract = ($runtime.surface_vertices -eq 4854 -and $runtime.surface_triangles -eq 6000)
    exact_volume_cage_contract = ($runtime.cage_vertices -eq 729 -and $runtime.cage_tetrahedra -eq 3072 -and $runtime.fixed_cage_vertices -eq 81)
    rest_binding_error_below_one_hundredth_cm = ([double]$runtime.rest_reconstruction_error_cm -lt 0.01)
    fixed_face_has_zero_drift = ([double]$runtime.maximum_fixed_face_drift_cm -lt 0.001)
    surface_deformation_is_visible_and_finite = ([double]::IsFinite([double]$runtime.maximum_surface_displacement_cm) -and [double]$runtime.maximum_surface_displacement_cm -gt 1.0)
    low_frequency_gpu_path_executed_exactly = ($runtime.low_frequency_modes -eq 2 -and $runtime.low_frequency_projection_passes -eq 9600 -and $runtime.low_frequency_apply_passes -eq 9600)
    map_created_with_runtime_actor = ($map.saved -eq $true -and @($map.errors).Count -eq 0 -and $map.map -eq "/PhysicsSoftBody/Maps/L_DragonGpuSurface" -and $map.actor -like "*SoftBodyDragonRealtimeActor*")
    screenshot_is_nontrivial = ($screenshot.Length -gt 100000)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    checks = $checks
    runtime = $runtime
    map = $map
    screenshot_bytes = $screenshot.Length
    failed_checks = $failed
    evidence_scope = "Real UE 5.4 D3D12 game window; persistent tetrahedral buffers, asynchronous cage readback, surface binding, DynamicMesh position updates, HUD, and screenshot."
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $validationPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 Dragon GPU 动态表面验证失败：$($failed -join ', ')" }
Write-Output "M3_DRAGON_GPU_SURFACE_RUNTIME_VALIDATION_PASSED report=$validationPath"
