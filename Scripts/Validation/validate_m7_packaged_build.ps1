param(
    [Parameter(Mandatory = $true)][string]$ArchiveRoot,
    [Parameter(Mandatory = $true)][string]$HubLog,
    [Parameter(Mandatory = $true)][string]$RouteLog,
    [Parameter(Mandatory = $true)][string]$HubScreenshot,
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..'))
)

$ErrorActionPreference = 'Stop'

$executables = @(Get-ChildItem -LiteralPath $ArchiveRoot -Filter '*.exe' -File -Recurse | Where-Object {
    $_.Name -notmatch 'CrashReportClient|EpicWebHelper|UnrealCEFSubProcess'
})
$gameExecutable = @($executables | Sort-Object @{ Expression = { $_.Name -match 'SIGGRAPHPhysicsLab|LyraGame' }; Descending = $true }, Length -Descending | Select-Object -First 1)
$pakFiles = @(Get-ChildItem -LiteralPath $ArchiveRoot -Filter 'pakchunk*-Windows.pak' -File -Recurse)
$utocFiles = @(Get-ChildItem -LiteralPath $ArchiveRoot -Filter '*.utoc' -File -Recurse)
$ucasFiles = @(Get-ChildItem -LiteralPath $ArchiveRoot -Filter '*.ucas' -File -Recurse)
$hubLogText = Get-Content -LiteralPath $HubLog -Raw -Encoding UTF8
$routeLogText = Get-Content -LiteralPath $RouteLog -Raw -Encoding UTF8

Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile($HubScreenshot)
$imageWidth = $image.Width
$imageHeight = $image.Height
$image.Dispose()

$checks = [ordered]@{
    archived_game_executable_exists = [bool]($gameExecutable.Count -eq 1 -and $gameExecutable[0].Length -gt 1MB)
    pak_container_exists = [bool]($pakFiles.Count -gt 0 -and ($pakFiles | Measure-Object Length -Sum).Sum -gt 1MB)
    iostore_toc_exists = [bool]($utocFiles.Count -gt 0 -and ($utocFiles | Measure-Object Length -Sum).Sum -gt 1KB)
    iostore_data_exists = [bool]($ucasFiles.Count -gt 0 -and ($ucasFiles | Measure-Object Length -Sum).Sum -gt 1MB)
    packaged_hub_started_and_exited_cleanly = [bool]($hubLogText -match 'M1_RUNTIME_STARTED' -and $hubLogText -match 'M1_SCREENSHOT_REQUESTED' -and $hubLogText -match 'M1_AUTO_EXIT')
    packaged_hub_is_default_non_editor_mode = [bool]($hubLogText -match 'M1_SCREENSHOT_REQUESTED.*M7_PortfolioHub')
    packaged_hub_screenshot_is_1600x900 = [bool]($imageWidth -eq 1600 -and $imageHeight -eq 900 -and (Get-Item -LiteralPath $HubScreenshot).Length -gt 150000)
    packaged_hub_routes_to_case05 = [bool]($routeLogText -match 'M7_HUB_OPEN case=5 map=/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter')
    packaged_case05_map_finishes_loading = [bool]($routeLogText -match 'Load map complete /PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter')
    smoke_logs_have_no_fatal_error = [bool]($hubLogText -notmatch 'Fatal error:|Assertion failed:|Unhandled Exception:' -and $routeLogText -notmatch 'Fatal error:|Assertion failed:|Unhandled Exception:')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    scope = 'M7 Win64 Development archived package, default Chinese hub, rendered screenshot and packaged Hub-to-Case05 route smoke test.'
    archive = [ordered]@{
        root = (Resolve-Path -LiteralPath $ArchiveRoot).Path
        executable = if ($gameExecutable.Count) { $gameExecutable[0].FullName } else { $null }
        executable_bytes = if ($gameExecutable.Count) { $gameExecutable[0].Length } else { 0 }
        pak_count = $pakFiles.Count
        pak_bytes = [long](($pakFiles | Measure-Object Length -Sum).Sum)
        utoc_count = $utocFiles.Count
        utoc_bytes = [long](($utocFiles | Measure-Object Length -Sum).Sum)
        ucas_count = $ucasFiles.Count
        ucas_bytes = [long](($ucasFiles | Measure-Object Length -Sum).Sum)
    }
    screenshot = [ordered]@{
        path = $HubScreenshot
        width = $imageWidth
        height = $imageHeight
        bytes = (Get-Item -LiteralPath $HubScreenshot).Length
    }
    checks = $checks
    failed_checks = $failed
}

$reportPath = Join-Path $ProjectRoot 'Docs\Validation\M7\packaged_build_validation.json'
$result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$result | ConvertTo-Json -Depth 7
if ($failed.Count -gt 0) { exit 1 }
