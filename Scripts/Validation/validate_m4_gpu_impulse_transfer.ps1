param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M4"
$automationPath = Join-Path $root "GpuImpulseD3D12Automation\index.json"
$fullAutomationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "gpu_impulse_transfer_validation.json"
foreach ($path in @($automationPath, $fullAutomationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M4 GPU impulse P2G 证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$full = Get-Content -LiteralPath $fullAutomationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.FlowMap.GpuImpulseGradientTransfer")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "FlowMap GPU impulse transfer*" })[0]
$device = @($automation.devices)[0]
$sourcePath = Join-Path $ProjectRoot "Plugins\PhysicsResearch\Shaders\Private\FlowMapImpulseTransfer.usf"
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$number = "[0-9.eE+-]+"
$pattern = "rhi=(?<rhi>\S+) cells=(?<cells>\S+) particles=(?<particles>\d+) modes=(?<modes>\d+) gather_passes_per_mode=(?<passes>\d+) pic_max_face_error=(?<pic>$number) gradient_max_face_error=(?<gradient>$number)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    isolated_d3d12_automation_passed = ($automation.succeeded -eq 1 -and $automation.failed -eq 0 -and $test.Count -eq 1 -and $test[0].state -eq "Success")
    real_rhi_is_d3d12_sm6 = ($device.renderMode -eq "D3D12_SM6" -and $device.rHI -like "DirectX 12*")
    full_flow_map_nullrhi_regression_is_fifteen_of_fifteen = ($full.succeeded -eq 15 -and $full.failed -eq 0)
    metric_line_is_machine_readable = $match.Success
    distorted_particle_contract_is_exact = ($match.Groups["cells"].Value -eq "16x16" -and [int]$match.Groups["particles"].Value -eq 256)
    pic_and_gradient_modes_both_execute_two_face_gathers = ([int]$match.Groups["modes"].Value -eq 2 -and [int]$match.Groups["passes"].Value -eq 2)
    gpu_pic_matches_cpu_faces = ([double]$match.Groups["pic"].Value -lt 1.e-4)
    gpu_gradient_impulse_matches_cpu_faces = ([double]$match.Groups["gradient"].Value -lt 1.e-4)
    shader_contains_transformed_impulse_and_gradient = ($source -match "B\.x \* M0\.x \+ B\.z \* M0\.y" -and $source -match "CurrentGradient\.x" -and $source -match "G0B")
    deterministic_face_gather_avoids_float_atomics = ($source -match "GatherImpulseFace" -and $source -notmatch "InterlockedAdd")
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    interpretation = "Each periodic MAC face deterministically gathers all contributing distorted particles. The two validation modes isolate B-transformed reference impulse from the additional B-transformed first spatial gradient Taylor term without nondeterministic floating-point atomics."
    checks = $checks
    device = [ordered]@{ name = $device.deviceName; gpu = $device.gPU; render_mode = $device.renderMode; rhi = $device.rHI }
    contract = [ordered]@{ cells = $match.Groups["cells"].Value; particles = [int]$match.Groups["particles"].Value; modes = [int]$match.Groups["modes"].Value; gather_passes_per_mode = [int]$match.Groups["passes"].Value }
    errors = [ordered]@{ pic_maximum_face_velocity = [double]$match.Groups["pic"].Value; gradient_maximum_face_velocity = [double]$match.Groups["gradient"].Value }
    boundary = "This is a blocking mathematical truth gate. The O(face_count * particle_count) deterministic gather is suitable for reference and current demo scale, not claimed as the final large-grid performance architecture. It is not yet connected to persistent advection and pressure resources."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M4 GPU impulse/梯度 P2G 验证失败：$($failed -join ', ')" }
Write-Output "M4_GPU_IMPULSE_TRANSFER_VALIDATION_PASSED report=$reportPath"
