# Runs the test harness (build with -DAVXR_HARNESS=ON) against the built layer, with D3D11 and Vulkan,
# and compares the two pictures. Everything goes to tests\out (ALPHA_VIEWS_DIR points the layer there),
# so no real game's windows, settings or log are touched.
param([string]$Build = "$PSScriptRoot\..\build\Release", [int]$Frames = 12, [switch]$Edit, [string]$Apis = 'd3d11,vulkan')
$ErrorActionPreference = 'Stop'
$out = "$PSScriptRoot\out"
$data = "$out\data"
New-Item -ItemType Directory -Force $data | Out-Null
Copy-Item "$PSScriptRoot\harness_windows.cfg" "$data\windows_AVXR_Harness.cfg" -Force
Set-Content "$data\settings.cfg" "1.0 1 0 0 1 1" -Encoding ascii   # hands on, guide on, hands behind objects on
$env:ALPHA_VIEWS_DIR = $data
$sdk = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { (Get-ChildItem 'C:\VulkanSDK' -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName }
$env:VK_LAYER_PATH = "$sdk\Bin"
$layer = "$Build\XR_APILAYER_GMO_alpha_views_xr.dll"
$tag = if ($Edit) { '_edit' } else { '' }
foreach ($api in $Apis.Split(',')) {
    $a = @($layer, $api, "$out\$api$tag.png", $Frames)
    if ($Edit) { $a += 'edit' }
    & "$Build\xr_harness.exe" @a
    Write-Host "$api exit code $LASTEXITCODE"
    Copy-Item "$data\log.txt" "$out\log_$api$tag.txt" -Force
}
if ($Apis -eq 'd3d11,vulkan') {
    $x = [IO.File]::ReadAllBytes("$out\d3d11$tag.png.rgba"); $y = [IO.File]::ReadAllBytes("$out\vulkan$tag.png.rgba")
    $n = 0; for ($i = 0; $i -lt $x.Length; $i += 4) { if ($x[$i] -ne $y[$i] -or $x[$i+1] -ne $y[$i+1] -or $x[$i+2] -ne $y[$i+2] -or $x[$i+3] -ne $y[$i+3]) { $n++ } }
    Write-Host "D3D11 vs Vulkan: $n of $($x.Length / 4) pixels differ"
}
