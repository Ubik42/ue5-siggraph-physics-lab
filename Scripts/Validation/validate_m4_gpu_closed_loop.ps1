param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuClosedLoopD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_closed_loop_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU 统一闭环证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuClosedLoop")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU closed loop*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Source\PhysicsResearchShaders\Private\FlowMapGpuClosedLoop.cpp"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$enqueueSection = [regex]::Match($source, "bool FFlowMapGpuClosedLoopSession::EnqueueStep\(\)(?<body>[\s\S]*?)void FFlowMapGpuClosedLoopSession::RequestReadback\(\)").Groups["body"].Value
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) cells=(?<cells>\S+) particles=(?<particles>\d+) steps=(?<steps>\d+) dt=(?<dt>$number) pressure_iterations=(?<iterations>\d+) readback_interval=(?<interval>\d+) steps_enqueued=(?<enqueued>\d+) steps_executed=(?<executed>\d+) advection_passes=(?<advection>\d+) gather_passes=(?<gather>\d+) divergence_passes=(?<divpasses>\d+) jacobi_passes=(?<jacobi>\d+) apply_passes=(?<apply>\d+) readbacks_completed=(?<readbacks>\d+) snapshot_step=(?<snapshot>\d+) cpu_divergence=(?<cpudiv>$number) gpu_divergence=(?<gpudiv>$number) max_face_error=(?<faceerror>$number) max_jacobian_det_error=(?<jacobian>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -ge 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    fixed_step_contract_is_exact = ($match.Groups["cells"].Value -eq "16x16" -and [int]$match.Groups["particles"].Value -eq 256 -and [int]$match.Groups["steps"].Value -eq 4 -and [int]$match.Groups["iterations"].Value -eq 64)
    queue_and_execution_counts_match = ([int]$match.Groups["enqueued"].Value -eq 4 -and [int]$match.Groups["executed"].Value -eq 4)
    all_pipeline_pass_counts_are_exact = ([int]$match.Groups["advection"].Value -eq 4 -and [int]$match.Groups["gather"].Value -eq 8 -and [int]$match.Groups["divpasses"].Value -eq 4 -and [int]$match.Groups["jacobi"].Value -eq 256 -and [int]$match.Groups["apply"].Value -eq 8)
    unified_snapshot_matches_last_step = ([int]$match.Groups["snapshot"].Value -eq 4 -and [int]$match.Groups["readbacks"].Value -ge 1)
    gpu_closed_loop_matches_cpu_faces = ([double]$match.Groups["faceerror"].Value -lt 1.e-3)
    gpu_closed_loop_matches_cpu_divergence = ([math]::Abs([double]$match.Groups["gpudiv"].Value - [double]$match.Groups["cpudiv"].Value) -lt 1.e-4)
    backward_map_remains_near_volume_preserving = ([double]$match.Groups["jacobian"].Value -lt 0.01)
    runtime_enqueue_contains_no_explicit_gpu_wait = ($enqueueSection.Length -gt 0 -and $enqueueSection -notmatch "FlushRenderingCommands|BlockUntilGPUIdle")
    one_enqueue_contains_advection_gather_pressure_and_apply = ($enqueueSection -match "Closed Advect" -and $enqueueSection -match "Closed Gather U" -and $enqueueSection -match "Closed Divergence" -and $enqueueSection -match "Closed Jacobi" -and $enqueueSection -match "Closed Apply U")
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "One persistent RDG enqueue advances particles and backward Jacobians from the current projected velocity, gathers transformed impulse and gradient to MAC faces, computes divergence, solves pressure, writes the projected velocity used by the next enqueue, and asynchronously publishes velocity plus particle state with one source-step tag."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; particles = [int]$match.Groups["particles"].Value; steps = [int]$match.Groups["steps"].Value; fixed_delta_seconds = [double]$match.Groups["dt"].Value; pressure_iterations_per_step = [int]$match.Groups["iterations"].Value; readback_interval_steps = [int]$match.Groups["interval"].Value }
    passes = [ordered]@{ particle_advection = [int]$match.Groups["advection"].Value; impulse_gather = [int]$match.Groups["gather"].Value; divergence = [int]$match.Groups["divpasses"].Value; pressure_jacobi = [int]$match.Groups["jacobi"].Value; pressure_apply = [int]$match.Groups["apply"].Value }
    lifecycle = [ordered]@{ steps_enqueued = [int]$match.Groups["enqueued"].Value; steps_executed = [int]$match.Groups["executed"].Value; readbacks_completed = [int]$match.Groups["readbacks"].Value; latest_snapshot_step = [int]$match.Groups["snapshot"].Value }
    metrics = [ordered]@{ cpu_divergence_rms = [double]$match.Groups["cpudiv"].Value; gpu_divergence_rms = [double]$match.Groups["gpudiv"].Value; maximum_cpu_gpu_face_velocity_error = [double]$match.Groups["faceerror"].Value; maximum_backward_jacobian_determinant_error = [double]$match.Groups["jacobian"].Value }
    boundary = "The unified loop now has a separately validated map-reset shader, but this four-step evidence does not cross a reset boundary. GPU timestamp, moving-solid boundaries, UE runtime actor, Niagara presentation, interaction, failure ablation, and screenshots remain future work."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU 统一闭环验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_CLOSED_LOOP_VALIDATION_PASSED report=$reportPath"
