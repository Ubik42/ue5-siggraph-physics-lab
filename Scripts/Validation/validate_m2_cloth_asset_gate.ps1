param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$gatePath = Join-Path $ProjectRoot "Docs\Validation\M2\chaos_cloth_asset_gate.json"
$metricsPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_ChaosClothAssetGate.json"
$csvPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M2_ChaosClothAssetGate.csv"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M2\Screenshots\M2_ChaosClothAssetGate.png"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\asset_gate_validation.json"

foreach ($path in @($gatePath, $metricsPath, $csvPath, $screenshotPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 Chaos Cloth 资产门产物：$path"
    }
}

$gate = Get-Content -LiteralPath $gatePath -Raw -Encoding UTF8 | ConvertFrom-Json
$metrics = Get-Content -LiteralPath $metricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @($metrics.samples)
$csv = @(Import-Csv -LiteralPath $csvPath -Encoding UTF8)
$actualMetrics = @($samples.metric | Sort-Object -Unique)
$expectedMetrics = @("ActiveClothActors", "ClothParticleCount", "MaxClothDisplacement", "MeanClothDisplacement")
$stepIndices = @($samples | ForEach-Object { [int64]$_.step_index })
$invalidValues = @($samples | Where-Object {
    $value = [double]$_.value
    [double]::IsNaN($value) -or [double]::IsInfinity($value)
})

$checks = [ordered]@{
    gate_passed = ($gate.result -eq "passed")
    asset_loaded = [bool]$gate.asset_loaded
    clothing_assets_present = ([int]$gate.clothing_assets -gt 0)
    chaos_simulation_created = [bool]$gate.cloth_simulation_created
    runtime_cloth_data = [bool]$gate.runtime_cloth_data
    simulated_particles_present = ([int]$gate.simulated_particles -gt 0)
    dynamic_displacement = ([double]$gate.maximum_observed_displacement_cm -gt 0.01)
    fallback_not_required = (-not [bool]$gate.fallback_required)
    json_csv_counts_match = ($samples.Count -eq $csv.Count -and $samples.Count -eq 844)
    metric_names = (@(Compare-Object $expectedMetrics $actualMetrics).Count -eq 0)
    step_range = (($stepIndices | Measure-Object -Minimum).Minimum -eq 30 -and ($stepIndices | Measure-Object -Maximum).Maximum -eq 240)
    finite_values = ($invalidValues.Count -eq 0)
    screenshot_nonempty = ((Get-Item -LiteralPath $screenshotPath).Length -gt 1024)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        asset = $gate.asset
        clothing_assets = [int]$gate.clothing_assets
        simulated_particles = [int]$gate.simulated_particles
        maximum_observed_displacement_cm = [double]$gate.maximum_observed_displacement_cm
        metric_samples = $samples.Count
        metrics = $actualMetrics
        screenshot = $screenshotPath
        screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 Chaos Cloth 资产运行门验证失败，详见：$reportPath"
}
Write-Output "M2_CLOTH_ASSET_GATE_VALIDATION_PASSED report=$reportPath"
