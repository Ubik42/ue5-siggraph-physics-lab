param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$runtimePath = Join-Path $ProjectRoot "Docs\Validation\M2\cpu_reference_runtime.json"
$metricsPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_CpuProjectiveDynamics.json"
$csvPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_CpuProjectiveDynamics.csv"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M2\Screenshots\M2_CpuProjectiveDynamics.png"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\cpu_reference_validation.json"

foreach ($path in @($runtimePath, $metricsPath, $csvPath, $screenshotPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 CPU 数学参考产物：$path"
    }
}

$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$metrics = Get-Content -LiteralPath $metricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @($metrics.samples)
$csv = @(Import-Csv -LiteralPath $csvPath -Encoding UTF8)
$actualMetrics = @($samples.metric | Sort-Object -Unique)
$expectedMetrics = @("CollisionProjections", "MaxStretchRatioError", "MeanStretchRatioError", "RemainingPenetration")
$steps = @($samples | ForEach-Object { [int64]$_.step_index })
$invalidValues = @($samples | Where-Object {
    $value = [double]$_.value
    [double]::IsNaN($value) -or [double]::IsInfinity($value)
})

$checks = [ordered]@{
    runtime_passed = ($runtime.result -eq "passed")
    research_backend = ($metrics.backend -eq "ResearchSolver")
    particle_count = ([int]$runtime.particles -eq 120)
    constraints_present = ([int]$runtime.constraints -gt [int]$runtime.particles)
    fixed_steps = ([int]$runtime.steps -eq 300)
    collision_projection_triggered = ([int64]$runtime.total_collision_projections -gt 0)
    bounded_stretch_error = ([double]$runtime.maximum_observed_stretch_ratio_error -lt 0.5)
    no_remaining_penetration = ([double]$runtime.maximum_remaining_penetration_cm -lt 0.00001)
    json_csv_counts_match = ($samples.Count -eq 1200 -and $csv.Count -eq 1200)
    metric_names = (@(Compare-Object $expectedMetrics $actualMetrics).Count -eq 0)
    step_range = (($steps | Measure-Object -Minimum).Minimum -eq 1 -and ($steps | Measure-Object -Maximum).Maximum -eq 300)
    finite_values = ($invalidValues.Count -eq 0)
    screenshot_nonempty = ((Get-Item -LiteralPath $screenshotPath).Length -gt 1024)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = [int]$runtime.particles
        constraints = [int]$runtime.constraints
        total_collision_projections = [int64]$runtime.total_collision_projections
        maximum_observed_stretch_ratio_error = [double]$runtime.maximum_observed_stretch_ratio_error
        maximum_remaining_penetration_cm = [double]$runtime.maximum_remaining_penetration_cm
        metric_samples = $samples.Count
        screenshot = $screenshotPath
        screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 CPU 数学参考验证失败，详见：$reportPath"
}
Write-Output "M2_CPU_REFERENCE_VALIDATION_PASSED report=$reportPath"
