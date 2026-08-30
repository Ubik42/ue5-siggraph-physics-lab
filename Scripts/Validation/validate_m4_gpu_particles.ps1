param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuParticleD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_particle_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU 粒子证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuParticleAdvection")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU particles*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Source\PhysicsResearchShaders\Private\FlowMapGpuParticles.cpp"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$enqueueSection = [regex]::Match($source, "bool FFlowMapGpuParticleSession::EnqueueAdvectionStep\(\)(?<body>[\s\S]*?)void FFlowMapGpuParticleSession::RequestReadback\(\)").Groups["body"].Value
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) cells=(?<cells>\S+) particles=(?<particles>\d+) steps=(?<steps>\d+) dt=(?<dt>$number) readback_interval=(?<interval>\d+) steps_enqueued=(?<enqueued>\d+) steps_executed=(?<executed>\d+) advection_passes=(?<passes>\d+) readbacks_completed=(?<readbacks>\d+) snapshot_step=(?<snapshot>\d+) max_position_error_cm=(?<position>$number) max_jacobian_component_error=(?<jacobian>$number) max_jacobian_determinant_difference=(?<determinant>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    particle_and_step_contract_is_exact = ($match.Groups["cells"].Value -eq "32x32" -and [int]$match.Groups["particles"].Value -eq 1024 -and [int]$match.Groups["steps"].Value -eq 20)
    queue_execution_and_pass_counts_match = ([int]$match.Groups["enqueued"].Value -eq 20 -and [int]$match.Groups["executed"].Value -eq 20 -and [int]$match.Groups["passes"].Value -eq 20)
    final_snapshot_matches_last_step = ([int]$match.Groups["snapshot"].Value -eq 20 -and [int]$match.Groups["readbacks"].Value -ge 1)
    gpu_matches_cpu_particle_positions = ([double]$match.Groups["position"].Value -lt 1.e-4)
    gpu_matches_cpu_backward_jacobian = ([double]$match.Groups["jacobian"].Value -lt 1.e-5 -and [double]$match.Groups["determinant"].Value -lt 1.e-5)
    runtime_enqueue_contains_no_explicit_gpu_wait = ($enqueueSection.Length -gt 0 -and $enqueueSection -notmatch "FlushRenderingCommands|BlockUntilGPUIdle")
    paired_particle_state_buffers_are_externalized = ($source -match "ConvertToExternalBuffer\(CurrentA\)" -and $source -match "ConvertToExternalBuffer\(CurrentB\)" -and $source -match "PendingReadbackA" -and $source -match "PendingReadbackB")
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "A persistent GPU particle session samples a periodic staggered MAC field, advances 1024 particles with RK2, and updates each 2x2 backward-map Jacobian for twenty queued steps. Position and Jacobian states use paired external RDG buffers and paired asynchronous readbacks tagged by source step."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; particles = [int]$match.Groups["particles"].Value; steps = [int]$match.Groups["steps"].Value; fixed_delta_seconds = [double]$match.Groups["dt"].Value; readback_interval_steps = [int]$match.Groups["interval"].Value }
    lifecycle = [ordered]@{ steps_enqueued = [int]$match.Groups["enqueued"].Value; steps_executed = [int]$match.Groups["executed"].Value; advection_passes = [int]$match.Groups["passes"].Value; readbacks_completed = [int]$match.Groups["readbacks"].Value; latest_snapshot_step = [int]$match.Groups["snapshot"].Value }
    errors = [ordered]@{ maximum_position_cm = [double]$match.Groups["position"].Value; maximum_jacobian_component = [double]$match.Groups["jacobian"].Value; maximum_jacobian_determinant_difference = [double]$match.Groups["determinant"].Value }
    first_failure = "Unconditional periodic fmod on every in-domain step produced 5.11568e-4 cm CPU/GPU position error and failed the 1e-4 cm gate. The implementation now wraps only on real boundary crossing; the threshold was not relaxed."
    boundary = "Velocity is prescribed and static in this gate. Particle impulse/gradient payload, P2G deposition, pressure feedback, map reset policy, moving solids, and GPU timing remain outside this milestone."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU 粒子验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_PARTICLE_VALIDATION_PASSED report=$reportPath"
