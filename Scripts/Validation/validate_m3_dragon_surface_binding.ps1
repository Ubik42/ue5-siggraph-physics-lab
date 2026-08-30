param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$generationPath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_softbody_cage_generation.json"
$importPath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_softbody_cage_import.json"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\DragonSurfaceBindingAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\dragon_surface_binding_validation.json"
$assetPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsSoftBody\Content\DragonDerived\SM_TerrorBringer_SoftBodyCage.uasset"
foreach ($path in @($generationPath, $importPath, $automationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 Dragon 表面绑定证据：$path" }
}
$generation = Get-Content -LiteralPath $generationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$import = Get-Content -LiteralPath $importPath -Raw -Encoding UTF8 | ConvertFrom-Json
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.SurfaceBinding.Contract")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Dragon surface binding vertices=*" })[0]
$match = [regex]::Match($message, "vertices=(?<vertices>\d+) cage_vertices=(?<cage>\d+) cage_tets=(?<tets>\d+) rest_reconstruction_error_cm=(?<error>[0-9.eE+-]+) maximum_deformation_cm=(?<deformation>[0-9.eE+-]+) fixed_cage_vertices=(?<fixed>\d+)")
$binding = if ($match.Success) { [ordered]@{
    surface_vertices = [int]$match.Groups["vertices"].Value
    cage_vertices = [int]$match.Groups["cage"].Value
    cage_tetrahedra = [int]$match.Groups["tets"].Value
    rest_reconstruction_error_cm = [double]$match.Groups["error"].Value
    maximum_deformation_cm = [double]$match.Groups["deformation"].Value
    fixed_cage_vertices = [int]$match.Groups["fixed"].Value
} } else { $null }
$checks = [ordered]@{
    generation_passed = ($generation.result -eq "passed")
    source_audit_matches_terrorbringer = ($generation.source_stats.vertices -eq 2876 -and $generation.source_stats.triangles -eq 4684 -and $generation.source_stats.boundary_edges -eq 1048 -and $generation.source_stats.connected_components -eq 28)
    derived_shell_is_closed_single_component = ($generation.final_stats.triangles -eq 6000 -and $generation.final_stats.boundary_edges -eq 0 -and $generation.final_stats.non_manifold_edges -eq 0 -and $generation.final_stats.connected_components -eq 1 -and [Math]::Abs([double]$generation.final_stats.signed_volume) -gt 1.0)
    import_passed_with_cpu_access = ($import.result -eq "passed" -and $import.class -eq "StaticMesh" -and $import.allow_cpu_access -eq $true)
    imported_into_case_namespace = ($import.object_path -eq "/PhysicsSoftBody/DragonDerived/SM_TerrorBringer_SoftBodyCage.SM_TerrorBringer_SoftBodyCage")
    local_licensed_derivative_exists = (Test-Path -LiteralPath $assetPath -PathType Leaf)
    automation_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success" -and $automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test[0].warnings -eq 0 -and $test[0].errors -eq 0)
    exact_binding_contract = ($null -ne $binding -and $binding.surface_vertices -eq 4854 -and $binding.cage_vertices -eq 729 -and $binding.cage_tetrahedra -eq 3072 -and $binding.fixed_cage_vertices -eq 81)
    rest_reconstruction_is_submillimeter = ($null -ne $binding -and $binding.rest_reconstruction_error_cm -lt 0.01)
    deformation_is_observable = ($null -ne $binding -and $binding.maximum_deformation_cm -gt 20.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    source_asset = "/Game/FourEvilDragonsHP/Meshes/DragonTheTerrorBringer/DragonTheTerrorBringerSK"
    derived_asset = $import.object_path
    redistribution = "The generated UAsset remains local and is gitignored; scripts and non-geometric statistics are tracked."
    checks = $checks
    generation = $generation
    import = $import
    binding = $binding
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 Dragon 表面绑定验证失败：$($failed -join ', ')" }
Write-Output "M3_DRAGON_SURFACE_BINDING_VALIDATION_PASSED report=$reportPath"
