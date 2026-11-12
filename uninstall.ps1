$json = Join-Path $PSScriptRoot 'build\Release\XR_APILAYER_GMO_alpha_views_xr.json'
foreach ($root in 'HKCU:', 'HKLM:') {
  $key = "$root\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"
  if (Get-ItemProperty -Path $key -Name $json -ErrorAction SilentlyContinue) {
    Remove-ItemProperty -Path $key -Name $json; "Removed from $root"
  }
}
