# Builds Alpha Views XR: produces dist\AlphaViewsXR-Setup-<ver>.exe, dist\AlphaViewsXR-<ver>.zip
# and the source package dist\AlphaViewsXR-<ver>-source\ (also zipped).
# SPDX-License-Identifier: GPL-3.0-only - Copyright (C) 2026 GmoLargey
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
# The version lives only in CMakeLists.txt; everything else is stamped from it here.
$ver = [regex]::Match((Get-Content "$root\CMakeLists.txt" -Raw), 'project\(AlphaViewsXR VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $ver) { throw "can't read the version from CMakeLists.txt" }
foreach ($f in "$root\packaging\README.txt", "$root\packaging\installer_info.txt") {
    $t = Get-Content $f -Raw
    $t = [regex]::Replace($t, '(ALPHA VIEWS XR\s+v)\d+\.\d+\.\d+', "`${1}$ver")
    $t = [regex]::Replace($t, 'AlphaViewsXR-Setup-\d+\.\d+\.\d+\.exe', "AlphaViewsXR-Setup-$ver.exe")
    [IO.File]::WriteAllText($f, $t)
}
Write-Host "Packaging Alpha Views XR $ver"
# Test scene: test_scene\ inside the source package, or the separate AlphaPassthroughTest project beside this one.
$test = if (Test-Path "$root\test_scene\main.cpp") { Join-Path $root 'test_scene' } else { Join-Path (Split-Path $root) 'AlphaPassthroughTest' }
$iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (-not (Test-Path $iscc)) { throw "Inno Setup 6 not found ($iscc)" }
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'

# The OpenXR loader silently skips a layer whose manifest doesn't parse, so check it first.
$manifest = Get-Content "$root\XR_APILAYER_GMO_alpha_views_xr.json" -Raw | ConvertFrom-Json
if ($manifest.api_layer.library_path -ne '.\XR_APILAYER_GMO_alpha_views_xr.dll') { throw 'layer manifest is wrong' }

# Pictures first: installer pages, Gestures/Controllers/Keyboard/Shapes.png, and the edit-mode
# guide panel icons, which are built into the DLL (touch the .rc so they're re-embedded).
& "$root\packaging\gen_gestures.ps1"
(Get-Item "$root\guide_icons.rc").LastWriteTime = Get-Date
if (-not (Test-Path "$root\build\CMakeCache.txt")) { cmake -S $root -B (Join-Path $root 'build') -A x64; if ($LASTEXITCODE) { throw '64-bit configure failed' } }
cmake --build (Join-Path $root 'build') --config Release
if ($LASTEXITCODE) { throw 'layer build failed' }
$dllVer = (Get-Item "$root\build\Release\XR_APILAYER_GMO_alpha_views_xr.dll").VersionInfo.ProductVersion
if ($dllVer -ne $ver) { throw "DLL version is '$dllVer' but CMakeLists.txt says $ver (re-run cmake?)" }
# Keep the prebuilt SPIR-V shaders (spirv\, for builds without dxc) in step with shaders.hlsl.
$spv = Get-ChildItem "$root\build\shaders\*.spv.h" -ErrorAction SilentlyContinue
if ($spv) { New-Item -ItemType Directory -Force "$root\spirv" | Out-Null; Copy-Item $spv.FullName "$root\spirv" -Force }
elseif (-not (Test-Path "$root\spirv\psBox.spv.h")) { throw 'no SPIR-V shaders: install the Vulkan SDK (dxc)' }
# 32-bit layer, for 32-bit OpenXR games. Same code, own build folder.
$b32 = Join-Path $root 'build32'
if (-not (Test-Path "$b32\CMakeCache.txt")) { cmake -S $root -B $b32 -A Win32; if ($LASTEXITCODE) { throw '32-bit configure failed' } }
cmake --build $b32 --config Release
if ($LASTEXITCODE) { throw '32-bit layer build failed' }
if ((Get-Item "$b32\Release\XR_APILAYER_GMO_alpha_views_xr.dll").VersionInfo.ProductVersion -ne $ver) { throw '32-bit DLL version mismatch (re-run cmake on build32)' }
if (-not (Test-Path "$test\build-static\CMakeCache.txt")) { cmake -S $test -B (Join-Path $test 'build-static') -A x64; if ($LASTEXITCODE) { throw 'test scene configure failed' } }
cmake --build (Join-Path $test 'build-static') --config Release --target AlphaPassthroughTest
if ($LASTEXITCODE) { throw 'test scene build failed' }

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$root\build\Release\XR_APILAYER_GMO_alpha_views_xr.dll" $stage
Copy-Item "$root\XR_APILAYER_GMO_alpha_views_xr.json" $stage
New-Item -ItemType Directory -Force "$stage\x86" | Out-Null
Copy-Item "$b32\Release\XR_APILAYER_GMO_alpha_views_xr.dll", "$root\XR_APILAYER_GMO_alpha_views_xr.json" "$stage\x86"
Copy-Item "$test\build-static\Release\AlphaPassthroughTest.exe" "$stage\AlphaViewsXRTest.exe"
Copy-Item "$root\packaging\README.txt" $stage
Copy-Item "$root\packaging\LICENSE.txt", "$root\packaging\THIRD_PARTY_NOTICES.txt" $stage

# Zip: stage + install scripts
$zipDir = Join-Path $dist "AlphaViewsXR-$ver"
New-Item -ItemType Directory -Force $zipDir | Out-Null
Copy-Item "$stage\*" $zipDir -Recurse
Copy-Item "$root\packaging\Install.bat", "$root\packaging\Uninstall.bat", "$root\packaging\install.ps1", "$root\packaging\uninstall.ps1" $zipDir
Copy-Item "$root\packaging\gestures\Gestures.png", "$root\packaging\gestures\Controllers.png", "$root\packaging\gestures\Keyboard.png", "$root\packaging\gestures\Shapes.png", "$root\packaging\SteamVR-Settings.png" $zipDir
Compress-Archive -Path $zipDir -DestinationPath (Join-Path $dist "AlphaViewsXR-$ver.zip")
Remove-Item $zipDir -Recurse -Force

# Installer
& $iscc /Q "/DAppVer=$ver" "$root\packaging\installer.iss"
if ($LASTEXITCODE) { throw 'installer build failed' }
Remove-Item $stage -Recurse -Force

# Linux tarball, built in WSL (skipped without it; on Linux run packaging/package.sh).
if (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
    $wroot = (wsl.exe wslpath -a ($root -replace '\\', '/')).Trim()
    wsl.exe -e sh "$wroot/packaging/package.sh"
    if ($LASTEXITCODE) { throw 'Linux build failed' }
} else { Write-Host 'WSL not found: no Linux tarball' }

# Source package (GPL-3.0): everything needed to build the layer, test scene, zip and installer.
# Build output, generated pictures, debug dumps and personal window files are left out.
$src = Join-Path $dist "AlphaViewsXR-$ver-source"
New-Item -ItemType Directory -Force "$src\packaging\source", "$src\test_scene" | Out-Null
Copy-Item "$root\layer.cpp", "$root\render_vk.h", "$root\guide_font.h", "$root\shaders.hlsl", "$root\CMakeLists.txt", "$root\version.rc", "$root\guide_icons.rc",
    "$root\XR_APILAYER_GMO_alpha_views_xr.json", "$root\package.ps1", "$root\install.ps1", "$root\uninstall.ps1" $src
# Vulkan shaders (prebuilt), Linux build files, test harness.
New-Item -ItemType Directory -Force "$src\spirv", "$src\linux", "$src\tests", "$src\packaging\linux", "$src\packaging\gestures" | Out-Null
Copy-Item "$root\spirv\*.spv.h" "$src\spirv"
Copy-Item "$root\linux\embed_bmps.cmake", "$root\linux\sal.h", "$root\linux\exports.map", "$root\linux\XR_APILAYER_GMO_alpha_views_xr.json" "$src\linux"
Copy-Item "$root\tests\xr_harness.cpp", "$root\tests\run_harness.ps1", "$root\tests\run_harness.sh", "$root\tests\harness_windows.cfg" "$src\tests"
Copy-Item "$root\packaging\linux\install.sh", "$root\packaging\linux\uninstall.sh", "$root\packaging\linux\README.txt" "$src\packaging\linux"
Copy-Item "$root\packaging\package.sh" "$src\packaging"
# The guide panel pictures (made by gen_gestures.ps1, which needs Windows), so Linux can build too.
Copy-Item "$root\packaging\gestures\*_120.bmp" "$src\packaging\gestures"
Copy-Item "$root\packaging\source\README.md" "$src\README.md"
Copy-Item "$root\packaging\source\gitignore.txt" "$src\.gitignore"
# Plain GPL text (no header) so GitHub recognises the licence.
$gpl = Get-Content "$root\packaging\LICENSE.txt" -Raw
$at = $gpl.IndexOf('                    GNU GENERAL PUBLIC LICENSE')
if ($at -lt 0) { throw 'GPL text not found in LICENSE.txt' }
[IO.File]::WriteAllText("$src\LICENSE", $gpl.Substring($at))
Copy-Item "$root\packaging\Install.bat", "$root\packaging\Uninstall.bat", "$root\packaging\install.ps1", "$root\packaging\uninstall.ps1",
    "$root\packaging\installer.iss", "$root\packaging\installer_info.txt", "$root\packaging\README.txt", "$root\packaging\LICENSE.txt",
    "$root\packaging\THIRD_PARTY_NOTICES.txt", "$root\packaging\SteamVR-Settings.png", "$root\packaging\gen_gestures.ps1" "$src\packaging"
Copy-Item "$root\packaging\source\README.md", "$root\packaging\source\gitignore.txt" "$src\packaging\source"
Copy-Item "$test\CMakeLists.txt", "$test\main.cpp", "$test\listext.cpp" "$src\test_scene"
# Pictures shown on the GitHub page (README.md).
New-Item -ItemType Directory -Force "$src\docs\images" | Out-Null
Copy-Item "$root\packaging\gestures\Gestures.png", "$root\packaging\gestures\Controllers.png", "$root\packaging\gestures\Keyboard.png", "$root\packaging\gestures\Shapes.png" "$src\docs\images"
# Windows' tar makes the zip: standard "/" paths, so it also unpacks properly on Linux (Compress-Archive
# in Windows PowerShell writes "\").
& "$env:SystemRoot\System32\tar.exe" -a -c -f "$src.zip" -C $dist (Split-Path $src -Leaf)
if ($LASTEXITCODE) { throw 'source zip failed' }

Get-ChildItem $dist | Select-Object Name, Length
Get-ChildItem $dist | Select-Object Name, Length
