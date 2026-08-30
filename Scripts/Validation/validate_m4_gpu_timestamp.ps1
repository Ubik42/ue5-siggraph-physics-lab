param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuTimestampD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_timestamp_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU timestamp 证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuClosedLoopTimestamp")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU timestamp*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Source\PhysicsResearchShaders\Private\FlowMapGpuClosedLoop.cpp"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$enqueue = [regex]::Match($source, "bool FFlowMapGpuClosedLoopSession::EnqueueStep\(\)(?<body>[\s\S]*?)void FFlowMapGpuClosedLoopSession::RequestReadback\(\)").Groups["body"].Value
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) steps=(?<steps>\d+) samples=(?<samples>\d+) skipped=(?<skipped>\d+) warmup_steps=(?<warmup>\d+) steady_samples=(?<steady>\d+) latest_step=(?<latest>\d+) latest_ms=(?<latestms>$number) mean_ms=(?<mean>$number) p95_ms=(?<p95>$number) p99_ms=(?<p99>$number) max_ms=(?<max>$number) steady_mean_ms=(?<steadymean>$number) steady_p95_ms=(?<steadyp95>$number) steady_p99_ms=(?<steadyp99>$number) steady_max_ms=(?<steadymax>$number) scope=(?<scope>\S+)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    all_steps_are_completed_or_explicitly_skipped = ([int]$match.Groups["steps"].Value -eq 32 -and [int]$match.Groups["samples"].Value -gt 0 -and ([int]$match.Groups["samples"].Value + [int]$match.Groups["skipped"].Value) -eq 32)
    warmup_and_steady_samples_are_explicit = ([int]$match.Groups["warmup"].Value -eq 4 -and [int]$match.Groups["steady"].Value -gt 0 -and [int]$match.Groups["steady"].Value -le [int]$match.Groups["samples"].Value)
    all_sample_statistics_are_positive = ([double]$match.Groups["latestms"].Value -gt 0 -and [double]$match.Groups["mean"].Value -gt 0 -and [double]$match.Groups["steadymean"].Value -gt 0)
    total_percentiles_are_ordered = ([double]$match.Groups["p95"].Value -gt 0 -and [double]$match.Groups["p99"].Value -ge [double]$match.Groups["p95"].Value -and [double]$match.Groups["max"].Value -ge [double]$match.Groups["p99"].Value)
    steady_percentiles_are_ordered = ([double]$match.Groups["steadyp95"].Value -gt 0 -and [double]$match.Groups["steadyp99"].Value -ge [double]$match.Groups["steadyp95"].Value -and [double]$match.Groups["steadymax"].Value -ge [double]$match.Groups["steadyp99"].Value)
    timing_scope_is_full_step_rdg_interval = ($match.Groups["scope"].Value -eq "full_step_rdg_interval")
    query_ring_is_nonblocking_and_bounded = ($source -match "GpuTimingSlots.SetNum\(8\)" -and $source -match "RHIGetRenderQueryResult\([^\r\n]+false\)" -and $enqueue -notmatch "FlushRenderingCommands|BlockUntilGPUIdle")
    timestamp_interval_wraps_graph_execution = ($enqueue.IndexOf("StartQuery.GetReference") -ge 0 -and $enqueue.IndexOf("G.Execute()") -gt $enqueue.IndexOf("StartQuery.GetReference") -and $enqueue.IndexOf("EndQuery.GetReference") -gt $enqueue.IndexOf("G.Execute()"))
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "Eight reusable RQT_AbsoluteTime query pairs wrap the complete persistent Flow Map RDG step. Query polling is nonblocking, a full ring increments an explicit skipped counter, and statistics retain both all-sample and warmup-excluded distributions."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ steps = [int]$match.Groups["steps"].Value; query_slots = 8; warmup_steps = [int]$match.Groups["warmup"].Value; scope = $match.Groups["scope"].Value }
    lifecycle = [ordered]@{ samples_completed = [int]$match.Groups["samples"].Value; samples_skipped = [int]$match.Groups["skipped"].Value; steady_state_samples = [int]$match.Groups["steady"].Value; latest_timed_step = [int]$match.Groups["latest"].Value }
    all_samples_ms = [ordered]@{ latest = [double]$match.Groups["latestms"].Value; mean = [double]$match.Groups["mean"].Value; p95 = [double]$match.Groups["p95"].Value; p99 = [double]$match.Groups["p99"].Value; maximum = [double]$match.Groups["max"].Value }
    steady_state_ms = [ordered]@{ mean = [double]$match.Groups["steadymean"].Value; p95 = [double]$match.Groups["steadyp95"].Value; p99 = [double]$match.Groups["steadyp99"].Value; maximum = [double]$match.Groups["steadymax"].Value }
    boundary = "This is a mechanism gate with only a few warmup-excluded samples because 32 steps are queued faster than the GPU drains the eight-slot ring. It proves nonblocking timing and explicit sample accounting, not a portfolio-grade performance distribution. Long runtime-map capture and raw CSV remain required. The interval covers the full RDG step, including any scheduled readback copy, and is not a single-kernel time."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU timestamp 验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_TIMESTAMP_VALIDATION_PASSED report=$reportPath"
