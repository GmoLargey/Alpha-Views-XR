#!/bin/sh
# Alpha Views XR (Linux): installs the OpenXR API layer for this user (no root needed).
#   ./install.sh            install (or update)
# The layer goes to ~/.local/share/AlphaViewsXR/layer/, and its manifest to
# ~/.local/share/openxr/1/api_layers/implicit.d/, where the OpenXR loader finds it for every game.
set -e
here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
dir="$data/AlphaViewsXR/layer"
manifests="$data/openxr/1/api_layers/implicit.d"
name=XR_APILAYER_GMO_alpha_views_xr
[ -f "$here/lib$name.so" ] || { echo "lib$name.so not found next to install.sh"; exit 1; }
mkdir -p "$dir" "$manifests"
cp "$here/lib$name.so" "$dir/"
chmod 755 "$dir/lib$name.so"
# The manifest, pointing at the installed library (full path).
sed "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$dir/lib$name.so\"|" "$here/$name.json" > "$manifests/$name.json"
chmod 644 "$manifests/$name.json"
echo "Alpha Views XR installed:"
echo "  $dir/lib$name.so"
echo "  $manifests/$name.json"
echo "Start a Vulkan OpenXR game; the log is $data/AlphaViewsXR/log.txt"
echo "Flatpak runtimes / games: see README.txt (section INSTALL)."
