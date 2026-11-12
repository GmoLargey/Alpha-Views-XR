# Alpha Views XR (zip version): removes the layer registration for this folder.
# Run via Uninstall.bat. Needs admin; asks for it if needed.
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}
$json = Join-Path $PSScriptRoot 'XR_APILAYER_GMO_alpha_views_xr.json'
$json32 = Join-Path $PSScriptRoot 'x86\XR_APILAYER_GMO_alpha_views_xr.json'
$removed = $false
# 64-bit and 32-bit (WOW6432Node) registrations
foreach ($pair in @(@('HKLM:\SOFTWARE', $json), @('HKCU:\SOFTWARE', $json),
                    @('HKLM:\SOFTWARE\WOW6432Node', $json32), @('HKCU:\SOFTWARE\WOW6432Node', $json32))) {
    $key = "$($pair[0])\Khronos\OpenXR\1\ApiLayers\Implicit"
    if (Get-ItemProperty -Path $key -Name $pair[1] -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $key -Name $pair[1]
        $removed = $true
    }
}
if ($removed) { Write-Host "Alpha Views XR uninstalled. You can delete this folder now." }
else { Write-Host "Alpha Views XR was not registered from this folder." }
Write-Host "Saved windows and logs are in $env:LOCALAPPDATA\AlphaViewsXR (delete to remove)."
Read-Host "Press Enter to close"
