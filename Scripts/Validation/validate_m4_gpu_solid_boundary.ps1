param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuSolidBoundaryD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_solid_boundary_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU 固体边界证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuClosedLoopSolidBoundary")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU solid boundary*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Source\PhysicsResearchShaders\Private\FlowMapGpuClosedLoop.cpp"
$shaderPath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Shaders\Private\FlowMapPressureProjection.usf"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$shader = Get-Content -LiteralPath $shaderPath -Raw -Encoding UTF8
$enqueue = [regex]::Match($source, "bool FFlowMapGpuClosedLoopSession::EnqueueStep\(\)(?<body>[\s\S]*?)void FFlowMapGpuClosedLoopSession::RequestReadback\(\)").Groups["body"].Value
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) steps=(?<steps>\d+) updates=(?<updates>\d+) applications=(?<applications>\d+) latest_version=(?<version>\d+) solid_cells=(?<solidcells>\d+) solid_u_faces=(?<solidu>\d+) solid_v_faces=(?<solidv>\d+) solid_circles=(?<circles>\d+) particle_projection_passes=(?<projectionpasses>\d+) particles_inside_solid=(?<inside>\d+) minimum_particle_clearance_cm=(?<clearance>$number) max_no_slip_residual=(?<residual>$number) fluid_divergence_rms=(?<divrms>$number) max_fluid_divergence=(?<maxdiv>$number) pressure_iterations=(?<iterations>\d+) boundary_source=(?<source>\S+)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    two_pose_versions_are_applied_to_two_steps = ([int]$match.Groups["steps"].Value -eq 2 -and [int]$match.Groups["updates"].Value -eq 2 -and [int]$match.Groups["applications"].Value -eq 2 -and [int]$match.Groups["version"].Value -eq 2)
    final_boundary_counts_are_nonzero = ([int]$match.Groups["solidcells"].Value -eq 16 -and [int]$match.Groups["solidu"].Value -gt 0 -and [int]$match.Groups["solidv"].Value -gt 0)
    one_collision_circle_is_applied_on_both_steps = ([int]$match.Groups["circles"].Value -eq 1 -and [int]$match.Groups["projectionpasses"].Value -eq 2)
    all_particles_remain_outside_the_moving_solid = ([int]$match.Groups["inside"].Value -eq 0 -and [double]$match.Groups["clearance"].Value -ge 0.049)
    projected_solid_faces_match_body_velocity = ([double]$match.Groups["residual"].Value -lt 1.e-6)
    fluid_divergence_gate_passes = ([double]$match.Groups["divrms"].Value -lt 0.05 -and [double]$match.Groups["maxdiv"].Value -lt 0.05)
    pressure_budget_is_explicit = ([int]$match.Groups["iterations"].Value -eq 256)
    boundary_source_is_ue_chaos_pose_versions = ($match.Groups["source"].Value -eq "ue_chaos_pose_versions")
    game_thread_update_validates_and_versions_all_five_arrays = ($source -match "ValidateBoundary\(State->Input,Boundary\)" -and $source -match "SolidBoundaryVersion\+\+" -and $source -match "SolidBoundaryUpdatesAccepted\+\+")
    one_enqueue_captures_one_boundary_version = ($enqueue -match "BoundaryVersion=S->SolidBoundaryVersion" -and $enqueue -match "Boundary=S->SolidBoundary")
    all_boundary_buffers_are_uploaded_before_solver_passes = ($enqueue -match "QueueBufferUpload\(SCM" -and $enqueue -match "QueueBufferUpload\(SUM" -and $enqueue -match "QueueBufferUpload\(SVM" -and $enqueue.IndexOf("QueueBufferUpload(SCM") -lt $enqueue.IndexOf("Closed Divergence SolidAware"))
    divergence_jacobi_and_apply_are_solid_aware = ($shader -match "void SolidDivergenceCS" -and $shader -match "void SolidJacobiCS" -and $shader -match "void SolidApplyUCS" -and $shader -match "void SolidApplyVCS")
    solid_cells_do_not_enter_pressure_unknowns = ($shader -match "SolidCellMask\[Index\] != 0[\s\S]*SolidOutputPressure\[Index\] = 0\.0")
    solid_apply_writes_prescribed_body_velocity = ($shader -match "SolidApplyOutputFaceU\[Index\] = SolidFaceVelocityU\[Index\]" -and $shader -match "SolidApplyOutputFaceV\[Index\] = SolidFaceVelocityV\[Index\]")
    particle_advection_resolves_the_same_boundary_circles = ($source -match "ParticleSolidCircleCount=Boundary\.ParticleCollisionCircles\.Num" -and (Get-Content -LiteralPath (Join-Path $ProjectRoot "Plugins\PhysicsResearch\Shaders\Private\FlowMapParticleAdvection.usf") -Raw -Encoding UTF8) -match "void AdvectParticlesWithSolidsCS" -and (Get-Content -LiteralPath (Join-Path $ProjectRoot "Plugins\PhysicsResearch\Shaders\Private\FlowMapParticleAdvection.usf") -Raw -Encoding UTF8) -match "ResolveParticleSolidCircles")
    runtime_enqueue_contains_no_explicit_gpu_wait = ($enqueue -notmatch "FlushRenderingCommands|BlockUntilGPUIdle")
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "Two different UE/Chaos pose-derived MAC boundary versions are captured by two consecutive nonblocking GPU steps. Solid face velocities replace gathered fluid velocities in divergence, solid cells are removed from the pressure unknowns, and final U/V Apply writes the prescribed rigid velocity back to every solid face."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ steps = [int]$match.Groups["steps"].Value; boundary_updates = [int]$match.Groups["updates"].Value; boundary_applications = [int]$match.Groups["applications"].Value; latest_boundary_version = [int]$match.Groups["version"].Value; pressure_iterations_per_step = [int]$match.Groups["iterations"].Value; source = $match.Groups["source"].Value }
    final_raster = [ordered]@{ solid_cells = [int]$match.Groups["solidcells"].Value; solid_u_faces = [int]$match.Groups["solidu"].Value; solid_v_faces = [int]$match.Groups["solidv"].Value; particle_collision_circles = [int]$match.Groups["circles"].Value }
    metrics = [ordered]@{ particle_projection_passes = [int]$match.Groups["projectionpasses"].Value; particles_inside_solid = [int]$match.Groups["inside"].Value; minimum_particle_clearance_cm = [double]$match.Groups["clearance"].Value; maximum_solid_face_no_slip_residual_cm_per_second = [double]$match.Groups["residual"].Value; fluid_cell_divergence_rms = [double]$match.Groups["divrms"].Value; maximum_fluid_cell_divergence = [double]$match.Groups["maxdiv"].Value }
    boundary = "The GPU velocity, pressure, and particle-advection paths consume one coherent moving-solid version. The current primitive set is limited to at most eight analytic circles and collision projection does not yet reset the particle map Jacobian at each contact. The test uses pose-derived updates rather than a live Chaos body component; runtime map, body lifecycle, visualization, interaction, failure ablation, and screenshots remain future work."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU 固体边界验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_SOLID_BOUNDARY_VALIDATION_PASSED report=$reportPath"
