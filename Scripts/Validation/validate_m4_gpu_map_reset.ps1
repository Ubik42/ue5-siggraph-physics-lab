param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuMapResetD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_map_reset_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU Map Reset 证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuClosedLoopMapReset")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU map reset*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Source\PhysicsResearchShaders\Private\FlowMapGpuClosedLoop.cpp"
$shaderPath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Shaders\Private\FlowMapParticleAdvection.usf"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$shader = Get-Content -LiteralPath $shaderPath -Raw -Encoding UTF8
$enqueueSection = [regex]::Match($source, "bool FFlowMapGpuClosedLoopSession::EnqueueStep\(\)(?<body>[\s\S]*?)void FFlowMapGpuClosedLoopSession::RequestReadback\(\)").Groups["body"].Value
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) cells=(?<cells>\S+) particles=(?<particles>\d+) steps=(?<steps>\d+) reset_interval=(?<interval>\d+) cpu_map_resets=(?<cpuresets>\d+) gpu_map_resets=(?<gpuresets>\d+) reset_passes=(?<resetpasses>\d+) advection_passes=(?<advection>\d+) gather_passes=(?<gather>\d+) divergence_passes=(?<divpasses>\d+) jacobi_passes=(?<jacobi>\d+) apply_passes=(?<apply>\d+) readbacks_completed=(?<readbacks>\d+) snapshot_step=(?<snapshot>\d+) cpu_divergence=(?<cpudiv>$number) gpu_divergence=(?<gpudiv>$number) max_face_error=(?<faceerror>$number) max_jacobian_identity_error=(?<identity>$number) max_jacobian_det_error=(?<determinant>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    two_reset_cycle_contract_is_exact = ($match.Groups["cells"].Value -eq "16x16" -and [int]$match.Groups["particles"].Value -eq 256 -and [int]$match.Groups["steps"].Value -eq 8 -and [int]$match.Groups["interval"].Value -eq 4)
    cpu_and_gpu_each_reset_twice = ([int]$match.Groups["cpuresets"].Value -eq 2 -and [int]$match.Groups["gpuresets"].Value -eq 2 -and [int]$match.Groups["resetpasses"].Value -eq 2)
    all_pipeline_pass_counts_are_exact = ([int]$match.Groups["advection"].Value -eq 8 -and [int]$match.Groups["gather"].Value -eq 16 -and [int]$match.Groups["divpasses"].Value -eq 8 -and [int]$match.Groups["jacobi"].Value -eq 512 -and [int]$match.Groups["apply"].Value -eq 16)
    snapshot_is_after_second_reset = ([int]$match.Groups["snapshot"].Value -eq 8 -and [int]$match.Groups["readbacks"].Value -ge 1)
    reset_snapshot_backward_map_is_identity = ([double]$match.Groups["identity"].Value -lt 1.e-7 -and [double]$match.Groups["determinant"].Value -lt 1.e-7)
    gpu_matches_cpu_faces_across_resets = ([double]$match.Groups["faceerror"].Value -lt 1.e-3)
    gpu_matches_cpu_divergence_across_resets = ([math]::Abs([double]$match.Groups["gpudiv"].Value - [double]$match.Groups["cpudiv"].Value) -lt 1.e-4)
    reset_shader_resamples_payload_and_sets_identity = ($shader -match "void ResetParticleMapsCS" -and $shader -match "ResetReferenceImpulseAndGradientRow0" -and $shader -match "float4\(Position, 1\.0, 0\.0\)")
    reset_runs_after_pressure_apply_in_same_enqueue = ($enqueueSection.IndexOf("FlowMap Closed Apply V") -ge 0 -and $enqueueSection.IndexOf("FlowMap Closed Map Reset") -gt $enqueueSection.IndexOf("FlowMap Closed Apply V"))
    reset_step_keeps_current_buffers_and_reads_back_reset_output = ($enqueueSection -match "FinalPA=PA" -and $enqueueSection -match "if\(!bResetMap\)\{Swap")
    runtime_enqueue_contains_no_explicit_gpu_wait = ($enqueueSection.Length -gt 0 -and $enqueueSection -notmatch "FlushRenderingCommands|BlockUntilGPUIdle")
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "The persistent GPU Flow Map loop crosses two long-map reset boundaries. After pressure projection, one reset compute pass samples the final MAC velocity and gradient into m0/G0, restores each backward-map Jacobian to identity, keeps ping-pong ownership on the current buffers, and publishes a reset-step snapshot with the same source-step tag."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; particles = [int]$match.Groups["particles"].Value; steps = [int]$match.Groups["steps"].Value; reset_interval_steps = [int]$match.Groups["interval"].Value }
    passes = [ordered]@{ particle_advection = [int]$match.Groups["advection"].Value; impulse_gather = [int]$match.Groups["gather"].Value; divergence = [int]$match.Groups["divpasses"].Value; pressure_jacobi = [int]$match.Groups["jacobi"].Value; pressure_apply = [int]$match.Groups["apply"].Value; map_reset = [int]$match.Groups["resetpasses"].Value }
    lifecycle = [ordered]@{ cpu_map_resets = [int]$match.Groups["cpuresets"].Value; gpu_map_resets = [int]$match.Groups["gpuresets"].Value; readbacks_completed = [int]$match.Groups["readbacks"].Value; latest_snapshot_step = [int]$match.Groups["snapshot"].Value }
    metrics = [ordered]@{ cpu_divergence_rms = [double]$match.Groups["cpudiv"].Value; gpu_divergence_rms = [double]$match.Groups["gpudiv"].Value; maximum_cpu_gpu_face_velocity_error = [double]$match.Groups["faceerror"].Value; maximum_reset_jacobian_identity_error = [double]$match.Groups["identity"].Value; maximum_reset_jacobian_determinant_error = [double]$match.Groups["determinant"].Value }
    boundary = "This validates reset math, ordering, persistent resource ownership, and CPU/GPU agreement. It is not yet a GPU timing claim and does not include moving-solid boundaries, UE runtime presentation, Niagara visualization, interaction, failure ablation, or screenshots."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU Map Reset 验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_MAP_RESET_VALIDATION_PASSED report=$reportPath"
