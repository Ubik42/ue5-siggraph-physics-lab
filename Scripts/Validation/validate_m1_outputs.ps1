param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"

$jsonPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M1_Runtime.json"
$csvPath = Join-Path $ProjectRoot "Saved\PhysicsResearch\M1_Runtime.csv"
$screenshotPath = Join-Path $ProjectRoot "Docs\Validation\M1\Screenshots\M1_Runtime.png"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M1\runtime_validation.json"

foreach ($requiredPath in @($jsonPath, $csvPath, $screenshotPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "缺少 M1 运行产物：$requiredPath"
    }
}

$json = Get-Content -LiteralPath $jsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$csv = @(Import-Csv -LiteralPath $csvPath -Encoding UTF8)
$samples = @($json.samples)
$expectedMetrics = @("EnergyProxy", "OrbitRadiusError")
$actualMetrics = @($samples.metric | Sort-Object -Unique)
$stepIndices = @($samples | ForEach-Object { [int64]$_.step_index })
$invalidValues = @($samples | Where-Object {
    $value = [double]$_.value
    [double]::IsNaN($value) -or [double]::IsInfinity($value)
})

$checks = [ordered]@{
    deterministic_seed = ([int64]$json.deterministic_seed -eq 20260829)
    backend = ($json.backend -eq "ChaosBaseline")
    json_sample_count = ($samples.Count -eq 360)
    csv_sample_count = ($csv.Count -eq 360)
    metric_names = (@(Compare-Object $expectedMetrics $actualMetrics).Count -eq 0)
    minimum_step = (($stepIndices | Measure-Object -Minimum).Minimum -eq 1)
    maximum_step = (($stepIndices | Measure-Object -Maximum).Maximum -eq 180)
    finite_values = ($invalidValues.Count -eq 0)
    screenshot_nonempty = ((Get-Item -LiteralPath $screenshotPath).Length -gt 1024)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        json = $jsonPath
        csv = $csvPath
        screenshot = $screenshotPath
        samples = $samples.Count
        steps = 180
        metrics = $actualMetrics
        screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8

if ($report.result -ne "passed") {
    throw "M1 运行产物验证失败，详见：$reportPath"
}

Write-Output "M1_OUTPUT_VALIDATION_PASSED report=$reportPath"
