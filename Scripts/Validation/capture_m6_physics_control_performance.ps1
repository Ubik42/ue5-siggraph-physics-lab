param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')),
    [string]$UnrealEditor = 'C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe',
    [int]$Trials = 3
)

$ErrorActionPreference = 'Stop'
$project = Join-Path $ProjectRoot 'SIGGRAPHPhysicsLab.uproject'
$savedRoot = Join-Path $ProjectRoot 'Saved\PhysicsResearch'
$evidenceRoot = Join-Path $ProjectRoot 'Docs\Validation\M6'
$records = @()

for ($trial = 1; $trial -le $Trials; ++$trial) {
    $otherEditors = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') })
    if ($otherEditors.Count -gt 0) {
        throw "性能采样要求没有并发 UnrealEditor；检测到 PID: $($otherEditors.ProcessId -join ', ')。未结束这些进程。"
    }

    $startedAt = Get-Date
    $arguments = @(
        ('"{0}"' -f $project), '/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter',
        '-game', '-unattended', '-nosplash', '-nop4', '-nullrhi', '-nosound',
        '-BENCHMARK', '-FPS=60', '-PhysicsDrivenCharacterAuto', '-PhysicsDrivenCharacterPerformance')
    $process = Start-Process -FilePath $UnrealEditor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Wait-Process -Id $process.Id -Timeout 60 -ErrorAction Stop

    $jsonSource = Join-Path $savedRoot 'DrivenCharacterPerformance.json'
    $csvSource = Join-Path $savedRoot 'DrivenCharacterPerformance.csv'
    if (-not (Test-Path -LiteralPath $jsonSource) -or -not (Test-Path -LiteralPath $csvSource)) {
        throw "Trial $trial did not produce both performance artifacts."
    }
    if ((Get-Item -LiteralPath $jsonSource).LastWriteTime -lt $startedAt.AddSeconds(-1)) {
        throw "Trial $trial performance JSON is stale."
    }

    $suffix = $trial.ToString('00')
    $jsonDestination = Join-Path $evidenceRoot "DrivenCharacterPerformance_Trial$suffix.json"
    $csvDestination = Join-Path $evidenceRoot "DrivenCharacterPerformance_Trial$suffix.csv"
    Copy-Item -LiteralPath $jsonSource -Destination $jsonDestination -Force
    Copy-Item -LiteralPath $csvSource -Destination $csvDestination -Force
    $runtime = Get-Content -LiteralPath $jsonDestination -Raw -Encoding UTF8 | ConvertFrom-Json
    $records += [ordered]@{
        trial = $trial
        owned_pid = $process.Id
        started_at = $startedAt.ToString('o')
        finished_at = (Get-Date).ToString('o')
        samples = $runtime.sample_count
        frame_wall_p95_ms = $runtime.frame_wall_p95_ms
        frame_wall_p99_ms = $runtime.frame_wall_p99_ms
        physics_control_update_p95_ms = $runtime.physics_control_update_p95_ms
        physics_control_update_p99_ms = $runtime.physics_control_update_p99_ms
    }
}

$manifest = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    protocol = 'Three isolated UE 5.4 game processes; NullRHI; -BENCHMARK -FPS=60; 300 warmup + 1500 measured frames per trial.'
    concurrent_editor_policy = 'Fail without terminating any existing UnrealEditor process.'
    trials = $records
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'physics_control_performance_capture.json') -Encoding UTF8
$manifest | ConvertTo-Json -Depth 6
