param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\ChaosClothSkinningAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\chaos_cloth_skinning_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 Chaos Cloth 蒙皮报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.ChaosClothSkinningTargets" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Chaos cloth skinning targets:*" })[0]
$pattern = "assets=(?<assets>\d+) vertices=(?<vertices>\d+) fixed=(?<fixed>\d+) dynamic=(?<dynamic>\d+) used_bones=(?<bones>\d+) total_influences=(?<total>\d+) max_influences=(?<maxinfluences>\d+) min_weight_sum=(?<minweight>[0-9.eE+-]+) max_weight_sum=(?<maxweight>[0-9.eE+-]+) reference_error_cm=(?<referror>[0-9.eE+-]+) component_reference_error_cm=(?<componenterror>[0-9.eE+-]+) uniform_transform_error_cm=(?<transformerror>[0-9.eE+-]+) min_fixed_target_motion_cm=(?<minmotion>[0-9.eE+-]+) max_fixed_target_motion_cm=(?<maxmotion>[0-9.eE+-]+)"
$match = [regex]::Match($message, $pattern)
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = ($automation.devices[0].rHI -eq "DirectX 12 (SM6)")
    metrics_present = $match.Success
    exact_topology_partition = ($match.Success -and [int]$match.Groups["assets"].Value -eq 2 -and [int]$match.Groups["vertices"].Value -eq 2011 -and [int]$match.Groups["fixed"].Value -eq 1307 -and [int]$match.Groups["dynamic"].Value -eq 704)
    complete_bone_contract = ($match.Success -and [int]$match.Groups["bones"].Value -gt 0 -and [int]$match.Groups["total"].Value -ge 2011 -and [int]$match.Groups["maxinfluences"].Value -gt 0 -and [int]$match.Groups["maxinfluences"].Value -le 12)
    normalized_weights = ($match.Success -and [math]::Abs((Metric "minweight") - 1.0) -lt 1.e-5 -and [math]::Abs((Metric "maxweight") - 1.0) -lt 1.e-5)
    reference_pose_matches_rest_mesh = ($match.Success -and (Metric "referror") -lt 1.e-4)
    registered_component_pose_matches_rest_mesh = ($match.Success -and (Metric "componenterror") -lt 1.e-3)
    uniform_pose_transform_matches_truth = ($match.Success -and (Metric "transformerror") -lt 1.e-3)
    all_fixed_targets_move = ($match.Success -and (Metric "minmotion") -gt 1.0 -and (Metric "maxmotion") -lt 30.0)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        asset = "/Game/Assassin/Mesh/SKM_Assassin_Skin1"
        used_bone_entries = if ($match.Success) { [int]$match.Groups["bones"].Value } else { $null }
        total_bone_influences = if ($match.Success) { [int]$match.Groups["total"].Value } else { $null }
        maximum_bone_influences = if ($match.Success) { [int]$match.Groups["maxinfluences"].Value } else { $null }
        minimum_weight_sum = if ($match.Success) { Metric "minweight" } else { $null }
        maximum_weight_sum = if ($match.Success) { Metric "maxweight" } else { $null }
        reference_pose_error_cm = if ($match.Success) { Metric "referror" } else { $null }
        registered_component_reference_pose_error_cm = if ($match.Success) { Metric "componenterror" } else { $null }
        uniform_transform_error_cm = if ($match.Success) { Metric "transformerror" } else { $null }
        failed_attempt = "An unregistered transient SkeletalMeshComponent produced 186.448151 cm error because its pose cache was not initialized; registering it in a transient World and initializing animation reduced the error to the value reported above"
        scope = "UE ClothingMeshUtils skinning target truth gate; live animated sequence and same-step Chaos comparison remain pending"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "Chaos Cloth 蒙皮验证失败：$reportPath" }
Write-Output "M2_CHAOS_CLOTH_SKINNING_VALIDATION_PASSED report=$reportPath"
