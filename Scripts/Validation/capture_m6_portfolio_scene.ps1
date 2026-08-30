param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')),
    [string]$UnrealEditor = 'C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe'
)

$ErrorActionPreference = 'Stop'
$otherEditors = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') })
if ($otherEditors.Count -gt 0) {
    throw "最终场景采集要求没有并发 UnrealEditor；检测到 PID: $($otherEditors.ProcessId -join ', ')。未结束这些进程。"
}

$project = Join-Path $ProjectRoot 'SIGGRAPHPhysicsLab.uproject'
$startedAt = Get-Date
$arguments = @(
    ('"{0}"' -f $project), '/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter',
    '-game', '-unattended', '-nosplash', '-nop4', '-nosound', '-BENCHMARK', '-FPS=60',
    '-PhysicsDrivenCharacterAuto', '-PhysicsDrivenCharacterPortfolio', '-windowed', '-ResX=1600', '-ResY=900', '-ForceRes')
$process = Start-Process -FilePath $UnrealEditor -ArgumentList $arguments -PassThru
Wait-Process -Id $process.Id -Timeout 60 -ErrorAction Stop

$screenshot = Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Saved\Screenshots') -Recurse -Filter '*.png' |
    Where-Object { $_.LastWriteTime -ge $startedAt.AddSeconds(-1) } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $screenshot) { throw 'Portfolio process did not produce a fresh screenshot.' }

$evidenceRoot = Join-Path $ProjectRoot 'Docs\Validation\M6'
$screenshotDestination = Join-Path $evidenceRoot 'Screenshots\Case05_PhysicsControl_Portfolio.png'
Copy-Item -LiteralPath $screenshot.FullName -Destination $screenshotDestination -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'Saved\PhysicsResearch\DrivenCharacterRealtime.json') -Destination (Join-Path $evidenceRoot 'DrivenCharacterPortfolio.json') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'Saved\PhysicsResearch\DrivenCharacterRealtime.csv') -Destination (Join-Path $evidenceRoot 'DrivenCharacterPortfolio.csv') -Force

$manifest = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    owned_pid = $process.Id
    engine = 'UE 5.4.4 CL 35576357'
    mode = 'D3D12 rendered game window, 1600x900, fixed 60 Hz, Portfolio scene'
    screenshot = 'Docs/Validation/M6/Screenshots/Case05_PhysicsControl_Portfolio.png'
    screenshot_bytes = (Get-Item -LiteralPath $screenshotDestination).Length
    runtime_json = 'Docs/Validation/M6/DrivenCharacterPortfolio.json'
    runtime_csv = 'Docs/Validation/M6/DrivenCharacterPortfolio.csv'
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'portfolio_scene_capture.json') -Encoding UTF8
$manifest | ConvertTo-Json -Depth 5
