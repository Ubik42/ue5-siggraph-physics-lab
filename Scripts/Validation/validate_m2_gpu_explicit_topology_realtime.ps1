param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuExplicitTopologyRealtimeAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_topology_realtime_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少显式拓扑持久 GPU 报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.Realtime" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU explicit realtime Chaos topology:*" })[0]
$pattern = "particles=(?<particles>\d+) edges=(?<edges>\d+) fixed=(?<fixed>\d+) dynamic=(?<dynamic>\d+) steps=(?<steps>\d+) readbacks=(?<readbacks>\d+) final_readback_step=(?<final>\d+) average_dynamic_displacement_cm=(?<avgdisp>[0-9.eE+-]+) dynamic_displacement_p95_cm=(?<p95disp>[0-9.eE+-]+) average_dynamic_z_cm=(?<avgz>[0-9.eE+-]+) average_edge_error_cm=(?<avgedge>[0-9.eE+-]+) edge_error_p95_cm=(?<p95edge>[0-9.eE+-]+) edge_error_p99_cm=(?<p99edge>[0-9.eE+-]+) maximum_edge_error_cm=(?<maxedge>[0-9.eE+-]+) fixed_drift_cm=(?<drift>[0-9.eE+-]+) average_submission_ms=(?<avgsubmit>[0-9.eE+-]+) maximum_submission_ms=(?<maxsubmit>[0-9.eE+-]+)"
$match = [regex]::Match($message, $pattern)
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
$avgDisp = Metric "avgdisp"
$avgZ = Metric "avgz"
$avgEdge = Metric "avgedge"
$p95Edge = Metric "p95edge"
$p99Edge = Metric "p99edge"
$maxEdge = Metric "maxedge"
$fixedDrift = Metric "drift"
$avgSubmission = Metric "avgsubmit"
$maxSubmission = Metric "maxsubmit"
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = ($automation.devices[0].rHI -eq "DirectX 12 (SM6)")
    metrics_present = $match.Success
    exact_assassin_topology = ($match.Success -and [int]$match.Groups["particles"].Value -eq 2011 -and [int]$match.Groups["edges"].Value -eq 5714)
    exact_fixed_dynamic_partition = ($match.Success -and [int]$match.Groups["fixed"].Value -eq 1307 -and [int]$match.Groups["dynamic"].Value -eq 704)
    all_fixed_steps_executed = ($match.Success -and [int]$match.Groups["steps"].Value -eq 90)
    asynchronous_readback_completed = ($match.Success -and [int]$match.Groups["readbacks"].Value -gt 0)
    final_snapshot_matches_last_step = ($match.Success -and [int]$match.Groups["final"].Value -eq 90)
    dynamic_particles_respond_to_gravity = ($match.Success -and [double]::IsFinite($avgDisp) -and $avgDisp -gt 0.01 -and [double]::IsFinite($avgZ) -and $avgZ -lt -0.01)
    edge_error_distribution_controlled = ($match.Success -and [double]::IsFinite($avgEdge) -and $avgEdge -lt 1.0 -and [double]::IsFinite($p95Edge) -and $p95Edge -lt 2.0 -and [double]::IsFinite($p99Edge) -and $p99Edge -lt 4.5 -and [double]::IsFinite($maxEdge) -and $maxEdge -lt 10.0)
    fixed_particles_invariant = ($match.Success -and [double]::IsFinite($fixedDrift) -and $fixedDrift -lt 1.e-5)
    render_submission_metrics_finite = ($match.Success -and [double]::IsFinite($avgSubmission) -and $avgSubmission -ge 0.0 -and [double]::IsFinite($maxSubmission) -and $maxSubmission -ge $avgSubmission)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        asset = "/Game/Assassin/Mesh/SKM_Assassin_Skin1"
        particles = if ($match.Success) { [int]$match.Groups["particles"].Value } else { $null }
        unique_edges = if ($match.Success) { [int]$match.Groups["edges"].Value } else { $null }
        fixed_vertices = if ($match.Success) { [int]$match.Groups["fixed"].Value } else { $null }
        dynamic_vertices = if ($match.Success) { [int]$match.Groups["dynamic"].Value } else { $null }
        fixed_steps = if ($match.Success) { [int]$match.Groups["steps"].Value } else { $null }
        readbacks_completed = if ($match.Success) { [int]$match.Groups["readbacks"].Value } else { $null }
        average_dynamic_displacement_cm = if ($match.Success) { $avgDisp } else { $null }
        dynamic_displacement_p95_cm = if ($match.Success) { Metric "p95disp" } else { $null }
        average_dynamic_vertical_displacement_cm = if ($match.Success) { $avgZ } else { $null }
        average_edge_error_cm = if ($match.Success) { $avgEdge } else { $null }
        edge_error_p95_cm = if ($match.Success) { $p95Edge } else { $null }
        edge_error_p99_cm = if ($match.Success) { $p99Edge } else { $null }
        maximum_edge_error_cm = if ($match.Success) { $maxEdge } else { $null }
        maximum_fixed_particle_drift_cm = if ($match.Success) { $fixedDrift } else { $null }
        average_render_submission_ms = if ($match.Success) { $avgSubmission } else { $null }
        maximum_render_submission_ms = if ($match.Success) { $maxSubmission } else { $null }
        timing_scope = "CPU render-thread RDG construction and submission; not GPU kernel time"
        scope = "Persistent exact Chaos LOD0 CSR dynamics gate; animated skinning targets, collision, NDB/modal/rank-one integration, and same-input Chaos comparison remain pending"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式拓扑持久 GPU 验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_TOPOLOGY_REALTIME_VALIDATION_PASSED report=$reportPath"
