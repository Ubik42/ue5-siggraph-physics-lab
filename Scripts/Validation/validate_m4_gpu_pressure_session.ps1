param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuPressureSessionD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_pressure_session_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU 持久压力会话证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuPressurePersistentSession")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU pressure session*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Source\PhysicsResearchShaders\Private\FlowMapGpuPressure.cpp"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$enqueueSection = [regex]::Match($source, "bool FFlowMapGpuPressureSession::EnqueueProjectionStep\(\)(?<body>[\s\S]*?)void FFlowMapGpuPressureSession::RequestReadback\(\)").Groups["body"].Value
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) cells=(?<cells>\S+) steps=(?<steps>\d+) iterations_per_step=(?<iterations>\d+) readback_interval=(?<interval>\d+) steps_enqueued=(?<enqueued>\d+) steps_executed=(?<executed>\d+) readbacks_completed=(?<readbacks>\d+) snapshot_step=(?<snapshot>\d+) divergence_passes=(?<divpasses>\d+) jacobi_passes=(?<jacobipasses>\d+) apply_passes=(?<applypasses>\d+) cpu_divergence_after=(?<cpuafter>$number) gpu_divergence_after=(?<gpuafter>$number) max_face_error=(?<faceerror>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    four_step_session_contract_is_exact = ([int]$match.Groups["steps"].Value -eq 4 -and [int]$match.Groups["iterations"].Value -eq 128 -and [int]$match.Groups["interval"].Value -eq 2)
    queue_and_execution_counts_match = ([int]$match.Groups["enqueued"].Value -eq 4 -and [int]$match.Groups["executed"].Value -eq 4)
    final_snapshot_matches_last_step = ([int]$match.Groups["snapshot"].Value -eq 4 -and [int]$match.Groups["readbacks"].Value -ge 1)
    persistent_pass_accounting_is_exact = ([int]$match.Groups["divpasses"].Value -eq 4 -and [int]$match.Groups["jacobipasses"].Value -eq 512 -and [int]$match.Groups["applypasses"].Value -eq 8)
    runtime_enqueue_contains_no_explicit_gpu_wait = ($enqueueSection.Length -gt 0 -and $enqueueSection -notmatch "FlushRenderingCommands|BlockUntilGPUIdle")
    velocity_and_pressure_resources_are_externalized = (($source | Select-String -Pattern "ConvertToExternalBuffer\(CurrentU\)" -AllMatches).Matches.Count -eq 1 -and ($source | Select-String -Pattern "ConvertToExternalBuffer\(PressureA\)" -AllMatches).Matches.Count -eq 1)
    paired_readbacks_and_source_step_are_implemented = ($source -match "PendingReadbackU" -and $source -match "PendingReadbackV" -and $source -match "PendingReadbackStep")
    sequential_gpu_matches_cpu_faces = ([double]$match.Groups["faceerror"].Value -lt 1.e-4)
    sequential_gpu_matches_cpu_divergence = ([math]::Abs([double]$match.Groups["gpuafter"].Value - [double]$match.Groups["cpuafter"].Value) -lt 1.e-5)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "Face velocities, divergence, and both pressure work buffers remain external RDG resources across four queued projection steps. The game-thread enqueue API performs no explicit GPU wait; paired U/V readbacks are consumed asynchronously and the final validation snapshot is tagged with its source step."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; steps = [int]$match.Groups["steps"].Value; jacobi_iterations_per_step = [int]$match.Groups["iterations"].Value; readback_interval_steps = [int]$match.Groups["interval"].Value }
    lifecycle = [ordered]@{ steps_enqueued = [int]$match.Groups["enqueued"].Value; steps_executed = [int]$match.Groups["executed"].Value; readbacks_completed = [int]$match.Groups["readbacks"].Value; latest_snapshot_step = [int]$match.Groups["snapshot"].Value }
    pass_accounting = [ordered]@{ divergence = [int]$match.Groups["divpasses"].Value; jacobi = [int]$match.Groups["jacobipasses"].Value; apply = [int]$match.Groups["applypasses"].Value }
    metrics = [ordered]@{ cpu_divergence_after = [double]$match.Groups["cpuafter"].Value; gpu_divergence_after = [double]$match.Groups["gpuafter"].Value; maximum_cpu_gpu_face_velocity_error = [double]$match.Groups["faceerror"].Value }
    boundary = "FlushAndWaitForReadbackForValidation is used only after all normal steps to make the automated truth gate deterministic. Runtime enqueue, periodic readback scheduling, and polling do not call FlushRenderingCommands or BlockUntilGPUIdle. GPU timestamp and the particle/Map state are not yet part of this session."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU 持久压力会话验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_PRESSURE_SESSION_VALIDATION_PASSED report=$reportPath"
