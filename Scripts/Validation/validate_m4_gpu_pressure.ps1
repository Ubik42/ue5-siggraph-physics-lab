param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuPressureD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_pressure_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU 压力投影证据：$path" }
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuPressureProjection")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU pressure*" })[0]
$device = @($automation.devices)[0]
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) cells=(?<cells>\S+) iterations=(?<iterations>\d+) divergence_passes=(?<divpasses>\d+) jacobi_passes=(?<jacobipasses>\d+) apply_passes=(?<applypasses>\d+) cpu_divergence_before=(?<before>$number) cpu_divergence_after=(?<cpuafter>$number) gpu_divergence_after=(?<gpuafter>$number) gpu_to_initial_ratio=(?<ratio>$number) max_face_error=(?<faceerror>$number)"
$match = [regex]::Match($message, $pattern)

$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    input_and_same_budget_contract_is_exact = ($match.Groups["cells"].Value -eq "32x16" -and [int]$match.Groups["iterations"].Value -eq 512)
    rdg_pass_accounting_is_exact = ([int]$match.Groups["divpasses"].Value -eq 1 -and [int]$match.Groups["jacobipasses"].Value -eq 512 -and [int]$match.Groups["applypasses"].Value -eq 2)
    gpu_matches_cpu_faces_within_one_e_minus_four = ([double]$match.Groups["faceerror"].Value -lt 1.e-4)
    gpu_matches_cpu_divergence_within_one_e_minus_five = ([math]::Abs([double]$match.Groups["gpuafter"].Value - [double]$match.Groups["cpuafter"].Value) -lt 1.e-5)
    gpu_reduces_divergence_by_more_than_99_percent = ([double]$match.Groups["before"].Value -gt 0.1 -and [double]$match.Groups["ratio"].Value -lt 0.01)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "The CPU and RDG compute paths receive the same 32x16 staggered velocity field, cell size, periodic boundary convention, and 512-iteration Jacobi budget. The gate checks dispatch accounting, readback, face velocities, and post-projection divergence on a real D3D12 SM6 device."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; jacobi_iterations = [int]$match.Groups["iterations"].Value; divergence_passes = [int]$match.Groups["divpasses"].Value; jacobi_passes = [int]$match.Groups["jacobipasses"].Value; apply_passes = [int]$match.Groups["applypasses"].Value }
    metrics = [ordered]@{ cpu_divergence_before = [double]$match.Groups["before"].Value; cpu_divergence_after = [double]$match.Groups["cpuafter"].Value; gpu_divergence_after = [double]$match.Groups["gpuafter"].Value; gpu_to_initial_ratio = [double]$match.Groups["ratio"].Value; maximum_cpu_gpu_face_velocity_error = [double]$match.Groups["faceerror"].Value }
    boundary = "This is a blocking validation readback for numerical truth, not the runtime frame architecture or a GPU performance benchmark. UE map integration, moving Chaos obstacles, asynchronous persistent resources, and Niagara presentation remain future work."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU 压力投影验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_PRESSURE_VALIDATION_PASSED report=$reportPath"
