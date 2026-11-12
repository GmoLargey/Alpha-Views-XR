# Alpha Views XR (zip version): registers the layer in this folder for all users.
# Run via Install.bat. Needs admin; asks for it if needed.
$ErrorActionPreference = 'Stop'
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}
try {
    $json = Join-Path $PSScriptRoot 'XR_APILAYER_GMO_alpha_views_xr.json'
    if (-not (Test-Path $json)) { throw "Can't find $json. Extract the whole zip first." }
    $key = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
    # Only create the key if missing: recreating it would remove other installed layers.
    if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    New-ItemProperty -Path $key -Name $json -Value 0 -PropertyType DWord -Force | Out-Null
    # 32-bit layer (x86 folder) for 32-bit games: the 32-bit OpenXR loader reads WOW6432Node.
    $json32 = Join-Path $PSScriptRoot 'x86\XR_APILAYER_GMO_alpha_views_xr.json'
    if (Test-Path $json32) {
        $key32 = 'HKLM:\SOFTWARE\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit'
        if (-not (Test-Path $key32)) { New-Item -Path $key32 -Force | Out-Null }
        New-ItemProperty -Path $key32 -Name $json32 -Value 0 -PropertyType DWord -Force | Out-Null
    }
    Write-Host "Alpha Views XR installed from:"
    Write-Host "  $PSScriptRoot"
    Write-Host "Keep this folder where it is. Run Uninstall.bat before moving or deleting it."
} catch {
    Write-Host "Install failed: $_" -ForegroundColor Red
}
Read-Host "Press Enter to close"
