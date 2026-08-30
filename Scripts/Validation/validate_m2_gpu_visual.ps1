param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$mapSetupPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_jacobi_map_setup.json"
$runtimePath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_jacobi_runtime.json"
$metricsPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_GpuJacobiValidation.json"
$csvPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_GpuJacobiValidation.csv"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M2\Screenshots\M2_GpuJacobiValidation.png"
$mapPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsContactCloth\Content\Maps\L_GpuJacobi_Validation.umap"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_jacobi_visual_validation.json"

foreach ($path in @($mapSetupPath, $runtimePath, $metricsPath, $csvPath, $screenshotPath, $mapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 GPU 可视化验证产物：$path"
    }
}

$mapSetup = Get-Content -LiteralPath $mapSetupPath -Raw -Encoding UTF8 | ConvertFrom-Json
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$metrics = Get-Content -LiteralPath $metricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @($metrics.samples)
$csv = @(Import-Csv -LiteralPath $csvPath -Encoding UTF8)
$actualMetrics = @($samples.metric | Sort-Object -Unique)
$expectedMetrics = @("BlockingWallTime", "ErrorReductionRatio", "InputMaximumEdgeError", "OutputMaximumEdgeError")
$inputError = [double]$runtime.input_maximum_edge_error_cm
$outputError = [double]$runtime.output_maximum_edge_error_cm
$invalidValues = @($samples | Where-Object {
    $value = [double]$_.value
    [double]::IsNaN($value) -or [double]::IsInfinity($value)
})

$checks = [ordered]@{
    map_saved = ([bool]$mapSetup.saved -and @($mapSetup.errors).Count -eq 0)
    expected_map = ($mapSetup.map -eq "/PhysicsContactCloth/Maps/L_GpuJacobi_Validation")
    runtime_passed = ($runtime.result -eq "passed")
    research_backend = ($metrics.backend -eq "ResearchSolver")
    dispatch_and_readback = ([bool]$runtime.dispatched -and [bool]$runtime.readback_succeeded)
    particle_count = ([int]$runtime.particles -eq 192)
    iteration_budget = ([int]$runtime.iterations -eq 96)
    input_error_present = ($inputError -gt 1.0)
    edge_error_reduced_20_percent = ($outputError -lt $inputError * 0.8)
    finite_metrics = ($invalidValues.Count -eq 0)
    metric_names = (@(Compare-Object $expectedMetrics $actualMetrics).Count -eq 0)
    json_csv_counts_match = ($samples.Count -eq 4 -and $csv.Count -eq 4)
    screenshot_nonempty = ((Get-Item -LiteralPath $screenshotPath).Length -gt 1024)
    map_asset_nonempty = ((Get-Item -LiteralPath $mapPath).Length -gt 1024)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = [int]$runtime.particles
        iterations = [int]$runtime.iterations
        input_maximum_edge_error_cm = $inputError
        output_maximum_edge_error_cm = $outputError
        reduction_percent = (1.0 - $outputError / $inputError) * 100.0
        blocking_wall_milliseconds = [double]$runtime.blocking_wall_milliseconds
        metric_samples = $samples.Count
        screenshot = $screenshotPath
        screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
        map_asset_bytes = (Get-Item -LiteralPath $mapPath).Length
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 GPU 可视化验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_VISUAL_VALIDATION_PASSED report=$reportPath"
