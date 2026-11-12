# Registers the layer for the current user (no admin). Add -Machine to register for all users (needs admin;
# use this if a game ignores the per-user registration).
param([switch]$Machine)
$json = Join-Path $PSScriptRoot 'build\Release\XR_APILAYER_GMO_alpha_views_xr.json'
$root = if ($Machine) { 'HKLM:' } else { 'HKCU:' }
$key = "$root\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"
if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }  # never recreate: would wipe other layers
New-ItemProperty -Path $key -Name $json -Value 0 -PropertyType DWord -Force | Out-Null
"Installed: $json ($root)"
