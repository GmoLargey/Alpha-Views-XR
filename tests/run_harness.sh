#!/bin/sh
# Linux: runs the test harness (cmake -DAVXR_HARNESS=ON) against the built layer with Vulkan.
# Everything goes to tests/out (ALPHA_VIEWS_DIR points the layer there), so no real windows,
# settings or log are touched. Usage: tests/run_harness.sh <build folder> [frames] [edit]
# Without a GPU, Mesa's lavapipe (mesa-vulkan-drivers) works.
set -e
here=$(cd "$(dirname "$0")" && pwd)
build=${1:-$here/../build}
frames=${2:-12}
edit=$3
out=$here/out
data=$out/data
mkdir -p "$data"
cp "$here/harness_windows.cfg" "$data/windows_AVXR_Harness.cfg"
printf '1.0 1 0 0 1 1\r\n' > "$data/settings.cfg"   # hands on, guide on, hands behind objects on
export ALPHA_VIEWS_DIR="$data"
tag=""
[ "$edit" = "edit" ] && tag="_edit"
"$build/xr_harness" "$build/libXR_APILAYER_GMO_alpha_views_xr.so" vulkan "$out/linux$tag.png" "$frames" $edit
echo "exit code $?"
cp "$data/log.txt" "$out/log_linux$tag.txt"
