param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$validationRoot = Join-Path $ProjectRoot "Docs\Validation\M3"
$reportPath = Join-Path $validationRoot "case02_acceptance_validation.json"

$componentFiles = @(
    "tetrahedral_grid_validation.json",
    "local_xpbd_validation.json",
    "compression_materials_validation.json",
    "low_frequency_correction_validation.json",
    "gpu_topology_validation.json",
    "gpu_xpbd_d3d12_validation.json",
    "gpu_xpbd_scaling_validation.json",
    "gpu_low_frequency_correction_validation.json",
    "gpu_counterbalanced_timing_validation.json",
    "dragon_surface_binding_validation.json",
    "dragon_gpu_surface_runtime_validation.json",
    "dragon_gpu_surface_interaction_validation.json",
    "ground_contact_ablation_validation.json",
    "cpu_reference_backend_validation.json"
)

$components = [ordered]@{}
foreach ($file in $componentFiles) {
    $path = Join-Path $validationRoot $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 Case 02 组件证据：$path"
    }
    $components[$file] = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

$automationPath = Join-Path $validationRoot "Automation\index.json"
$chaosPath = Join-Path $validationRoot "chaos_flesh_isolated_spike.json"
foreach ($path in @($automationPath, $chaosPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 Case 02 总验收证据：$path"
    }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$chaos = Get-Content -LiteralPath $chaosPath -Raw -Encoding UTF8 | ConvertFrom-Json

$gpu = $components["gpu_xpbd_d3d12_validation.json"]
$scaling = $components["gpu_xpbd_scaling_validation.json"]
$lowFrequency = $components["gpu_low_frequency_correction_validation.json"]
$timing = $components["gpu_counterbalanced_timing_validation.json"]
$binding = $components["dragon_surface_binding_validation.json"]
$runtime = $components["dragon_gpu_surface_runtime_validation.json"]
$interaction = $components["dragon_gpu_surface_interaction_validation.json"]
$contact = $components["ground_contact_ablation_validation.json"]
$cpu = $components["cpu_reference_backend_validation.json"]

$screenNames = @(
    "M3_DragonGpuSurface.png",
    "M3_DragonGpuSurface_Interaction.png",
    "M3_DragonGpuSurface_Contact.png",
    "M3_DragonGpuSurface_WeakContact.png",
    "M3_DragonCpuLocalXpbdReference.png",
    "M3_DragonBackendToggle.png"
)
$screens = [ordered]@{}
foreach ($name in $screenNames) {
    $path = Join-Path $validationRoot ("Screenshots\" + $name)
    $screens[$name] = if (Test-Path -LiteralPath $path -PathType Leaf) {
        (Get-Item -LiteralPath $path).Length
    } else {
        0
    }
}

$sourceFiles = @(
    "Plugins\PhysicsResearch\Shaders\Private\SoftBodyGpuXpbd.usf",
    "Plugins\GameFeatures\PhysicsSoftBody\Source\PhysicsSoftBodyRuntime\Private\SoftBodyDragonRealtimeActor.cpp",
    "Plugins\GameFeatures\PhysicsSoftBody\Source\PhysicsSoftBodyRuntime\Private\SoftBodyLocalXpbdSolver.cpp",
    "Plugins\GameFeatures\PhysicsSoftBody\Content\Maps\L_DragonGpuSurface.umap"
)

$checks = [ordered]@{
    nullrhi_module_regression_is_11_of_11 = ($automation.succeeded -eq 11 -and $automation.failed -eq 0 -and @($automation.tests).Count -eq 11)
    all_fourteen_component_reports_pass = (@($components.Values | Where-Object { $_.result -ne "passed" }).Count -eq 0)
    no_component_has_failed_checks = (@($components.Values | Where-Object { @($_.failed_checks).Count -ne 0 }).Count -eq 0)
    real_d3d12_sm6_compute_is_proven = ($gpu.device.rhi -eq "DirectX 12 (SM6)" -and $gpu.device.render_mode -eq "D3D12_SM6" -and $scaling.device.rhi -eq "DirectX 12 (SM6)" -and $timing.device.rhi -eq "DirectX 12 (SM6)")
    convergence_and_scaling_are_independently_gated = ($lowFrequency.checks.medium_residual_improves_at_least_fifteen_percent -eq $true -and $lowFrequency.checks.high_residual_improves_at_least_twenty_five_percent -eq $true -and @($scaling.failed_checks).Count -eq 0)
    performance_claim_is_counterbalanced_and_bounded = ($timing.protocol -like "Per tier: Local, Corrected, Corrected, Local*" -and $timing.tiers.medium.corrected_to_local_ratio -gt 1.0 -and $timing.tiers.medium.corrected_to_local_ratio -lt 1.5 -and $timing.tiers.high.corrected_to_local_ratio -gt 1.0 -and $timing.tiers.high.corrected_to_local_ratio -lt 1.5)
    licensed_dragon_derivative_boundary_is_explicit = ($binding.redistribution -like "*gitignored*" -and $binding.generation.license_boundary -like "*do not redistribute*" -and $binding.checks.local_licensed_derivative_exists -eq $true)
    dragon_gpu_runtime_closes_the_ue_render_loop = ($runtime.runtime.steps_executed -eq 300 -and $runtime.runtime.latest_readback_step -eq 300 -and $runtime.runtime.surface_updates -gt 0 -and $runtime.runtime.maximum_fixed_face_drift_cm -eq 0.0)
    ue_player_controls_change_real_simulation_state = ($interaction.runtime.pause_toggles -eq 2 -and $interaction.runtime.single_steps -eq 1 -and $interaction.runtime.resets -eq 1 -and $interaction.runtime.debug_toggles -eq 2 -and $interaction.runtime.manual_exports -eq 1)
    contact_ablation_contains_success_and_expected_failure = ($contact.standard.maximum_ground_penetration_cm -eq 0.0 -and $contact.standard.ground_contact_stiffness -eq 1.0 -and $contact.weak.ground_contact_stiffness -eq 0.02 -and $contact.weak.expected_failure_observed -eq $true -and $contact.weak.maximum_ground_penetration_cm -gt 50.0)
    cpu_backend_toggle_is_real_and_quality_failure_is_honest = ($cpu.cpu_reference.not_chaos_flesh -eq $true -and $cpu.cpu_reference.execution_contract_passed -eq $true -and $cpu.cpu_reference.physics_quality_gate_passed -eq $false -and $cpu.backend_toggle.backend_toggles -eq 1 -and $cpu.backend_toggle.steps_executed -eq 300)
    chaos_flesh_engine_boundary_is_recorded_without_faking_integration = ($chaos.result -eq "blocked_for_default_enablement" -and $chaos.plugin_mounted -eq $true -and $chaos.chaos_flesh_nodes_error_observed -eq $true -and $chaos.error_module -eq "ChaosFleshNodes")
    six_portfolio_screenshots_are_nontrivial = (@($screens.Values | Where-Object { $_ -lt 200000 }).Count -eq 0)
    implementation_shader_actor_solver_and_map_exist = (@($sourceFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ProjectRoot $_)) }).Count -eq 0)
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    case = "Case 02 / GPU 多尺度软体与 UE 动态 Dragon"
    acceptance_scope = "Algorithm contracts, real D3D12 compute, quality-cost attribution, UE runtime rendering, player interaction, controlled contact failure, alternate CPU backend, Chaos Flesh boundary, screenshots, and licensed-asset handling."
    checks = $checks
    component_reports = @($componentFiles)
    screenshots_bytes = $screens
    headline_metrics = [ordered]@{
        nullrhi_tests = 11
        dragon_surface_vertices = $contact.standard.surface_vertices
        cage_vertices = $contact.standard.cage_vertices
        cage_tetrahedra = $contact.standard.cage_tetrahedra
        gpu_standard_max_displacement_cm = $contact.standard.maximum_surface_displacement_cm
        gpu_standard_max_penetration_cm = $contact.standard.maximum_ground_penetration_cm
        weak_contact_max_penetration_cm = $contact.weak.maximum_ground_penetration_cm
        cpu_reference_max_displacement_cm = $cpu.cpu_reference.maximum_surface_displacement_cm
        medium_counterbalanced_overhead_percent = $timing.tiers.medium.overhead_percent
        high_counterbalanced_overhead_percent = $timing.tiers.high.overhead_percent
    }
    caveats = @(
        "Ground contact is a frictionless infinite-plane projection, not a Chaos contact manifold, friction, or CCD implementation.",
        "The CPU local XPBD path is a real alternate backend but intentionally not Chaos Flesh; its expected quality failure remains visible.",
        "The Fab-derived Dragon mesh and generated UAsset remain local and gitignored; only scripts, metrics, and screenshots are tracked.",
        "Counterbalanced GPU timing is machine/protocol evidence, not a universal cross-hardware speedup law."
    )
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8

if ($failed.Count -gt 0) {
    throw "Case 02 作品级验收失败：$($failed -join ', ')"
}
Write-Output "M3_CASE02_ACCEPTANCE_PASSED report=$reportPath"
