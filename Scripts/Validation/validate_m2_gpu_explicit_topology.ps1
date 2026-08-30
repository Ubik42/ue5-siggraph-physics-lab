param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuExplicitTopologyAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_topology_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少显式拓扑 GPU 报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.Blocking" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU explicit Chaos topology:*" })[0]
$match = [regex]::Match($message, "particles=(?<particles>\d+) edges=(?<edges>\d+) iterations=(?<iterations>\d+) input_edge_error_cm=(?<input>[0-9.eE+-]+) output_edge_error_cm=(?<output>[0-9.eE+-]+) fixed_drift_cm=(?<drift>[0-9.eE+-]+) blocking_wall_ms=(?<wall>[0-9.eE+-]+)")
$inputError = if ($match.Success) { [double]$match.Groups["input"].Value } else { [double]::NaN }
$outputError = if ($match.Success) { [double]$match.Groups["output"].Value } else { [double]::NaN }
$fixedDrift = if ($match.Success) { [double]$match.Groups["drift"].Value } else { [double]::NaN }
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = ($automation.devices[0].rHI -eq "DirectX 12 (SM6)")
    metrics_present = $match.Success
    exact_assassin_particle_count = ($match.Success -and [int]$match.Groups["particles"].Value -eq 2011)
    exact_assassin_unique_edges = ($match.Success -and [int]$match.Groups["edges"].Value -eq 5714)
    expected_iteration_budget = ($match.Success -and [int]$match.Groups["iterations"].Value -eq 128)
    finite_positive_input_error = ($match.Success -and [double]::IsFinite($inputError) -and $inputError -gt 0.1)
    maximum_edge_error_reduced_twenty_percent = ($match.Success -and [double]::IsFinite($outputError) -and $outputError -lt $inputError * 0.8)
    fixed_particles_invariant = ($match.Success -and [double]::IsFinite($fixedDrift) -and $fixedDrift -lt 1.e-5)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        asset = "/Game/Assassin/Mesh/SKM_Assassin_Skin1"
        particles = if ($match.Success) { [int]$match.Groups["particles"].Value } else { $null }
        unique_edges = if ($match.Success) { [int]$match.Groups["edges"].Value } else { $null }
        iterations = if ($match.Success) { [int]$match.Groups["iterations"].Value } else { $null }
        input_maximum_edge_error_cm = if ($match.Success) { $inputError } else { $null }
        output_maximum_edge_error_cm = if ($match.Success) { $outputError } else { $null }
        maximum_fixed_particle_drift_cm = if ($match.Success) { $fixedDrift } else { $null }
        blocking_wall_milliseconds = if ($match.Success) { [double]$match.Groups["wall"].Value } else { $null }
        scope = "Blocking GPU correctness gate for exact Chaos LOD0 particles and CSR structural adjacency; persistent dynamics, skinning, and same-input Chaos comparison remain pending"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式拓扑 GPU 验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_TOPOLOGY_VALIDATION_PASSED report=$reportPath"
