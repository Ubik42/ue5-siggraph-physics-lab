param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\GpuXpbdD3D12Automation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\gpu_xpbd_d3d12_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 D3D12 GPU XPBD 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.GpuXpbd.PersistentCantilever")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Persistent GPU soft body:*" })[0]
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) steps=(?<steps>\d+) iterations=(?<iterations>\d+) vertices=(?<vertices>\d+) edges=(?<edges>\d+) tets=(?<tets>\d+) readbacks=(?<readbacks>\d+) latest_step=(?<latest>\d+) distance_passes=(?<distance>\d+) volume_passes=(?<volume>\d+) gather_passes=(?<gather>\d+) gpu_residual=(?<gpures>$number) cpu_residual=(?<cpures>$number) residual_ratio=(?<ratio>$number) gpu_edge_max=(?<edge>$number) gpu_volume_mean=(?<volmean>$number) gpu_tip_cm=(?<tip>$number) gpu_fixed_drift_cm=(?<drift>$number)"
$match = [regex]::Match($message, $pattern)
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
function CountMetric([string]$Name) { if ($match.Success) { [int64]$match.Groups[$Name].Value } else { -1 } }
$device = @($automation.devices)[0]
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    isolated_suite_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    zero_test_warnings_and_errors = ([int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    real_d3d12_sm6_device = ($device.rHI -eq "DirectX 12 (SM6)" -and $device.renderMode -eq "D3D12_SM6" -and $match.Success -and $match.Groups["rhi"].Value -eq "D3D12")
    exact_low_tier_contract = ($match.Success -and (CountMetric "vertices") -eq 45 -and (CountMetric "edges") -eq 180 -and (CountMetric "tets") -eq 96)
    exact_fixed_budget = ($match.Success -and (CountMetric "steps") -eq 120 -and (CountMetric "iterations") -eq 32 -and (CountMetric "latest") -eq 120)
    all_three_passes_executed = ($match.Success -and (CountMetric "distance") -eq 3840 -and (CountMetric "volume") -eq 3840 -and (CountMetric "gather") -eq 3840)
    asynchronous_readback_completed = ($match.Success -and (CountMetric "readbacks") -ge 1)
    gpu_residual_bounded = ($match.Success -and (Metric "gpures") -lt 0.15)
    cpu_gpu_same_residual_scale = ($match.Success -and (Metric "ratio") -gt 0.5 -and (Metric "ratio") -lt 2.0)
    edge_error_bounded = ($match.Success -and (Metric "edge") -lt 0.40)
    volume_error_bounded = ($match.Success -and (Metric "volmean") -lt 0.15)
    fixed_face_zero_drift = ($match.Success -and (Metric "drift") -lt 0.001)
    finite_tip_displacement = ($match.Success -and (Metric "tip") -gt 1.0 -and (Metric "tip") -lt 200.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    device = [ordered]@{ gpu = $device.gPU; rhi = $device.rHI; render_mode = $device.renderMode }
    checks = $checks
    metrics = if ($match.Success) { [ordered]@{
        steps = CountMetric "steps"; iterations_per_step = CountMetric "iterations"
        vertices = CountMetric "vertices"; edges = CountMetric "edges"; tetrahedra = CountMetric "tets"
        readbacks_completed = CountMetric "readbacks"; latest_readback_step = CountMetric "latest"
        distance_passes = CountMetric "distance"; volume_passes = CountMetric "volume"; gather_passes = CountMetric "gather"
        gpu_residual_rms = Metric "gpures"; cpu_residual_rms = Metric "cpures"; gpu_to_cpu_residual_ratio = Metric "ratio"
        gpu_maximum_edge_relative_error = Metric "edge"; gpu_mean_volume_relative_error = Metric "volmean"
        gpu_tip_displacement_cm = Metric "tip"; gpu_fixed_drift_cm = Metric "drift"
    } } else { $null }
    failure_history = [ordered]@{
        first_gpu_residual_rms = 0.880154719
        first_fixed_drift_cm = 23.100236893
        cause = "同一 HLSL 全局资源名跨 Pass 同时声明为 RWStructuredBuffer 并以 SRV 绑定，导致 Gather 资源反射类型错位"
        resolution = "为约束输出 UAV 与 Gather 输入 SRV 使用不同 Shader 参数名，仍引用同一 RDG Buffer"
    }
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 D3D12 GPU XPBD 验证失败：$($failed -join ', ')" }
Write-Output "M3_GPU_XPBD_D3D12_VALIDATION_PASSED report=$reportPath"
