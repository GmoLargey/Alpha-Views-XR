#!/bin/sh
# Builds the Linux layer (x86-64) and makes dist/AlphaViewsXR-<version>-linux.tar.gz: the layer, its
# manifest, install / uninstall scripts, README, licence and pictures. package.ps1 runs this in WSL;
# on Linux run it directly. The Windows packages (and the source zip) come from package.ps1.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^project(AlphaViewsXR VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
[ -n "$version" ] || { echo "No version in CMakeLists.txt"; exit 1; }
echo "Alpha Views XR $version (Linux)"

build=${BUILD_DIR:-$HOME/avxr-build-linux}
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j
so="$build/libXR_APILAYER_GMO_alpha_views_xr.so"
# Only the OpenXR entry point may be exported (the built-in C++ runtime stays hidden).
exports=$(nm -D --defined-only "$so" | awk '{print $3}')
[ "$exports" = "xrNegotiateLoaderApiLayerInterface" ] || { echo "Unexpected exports: $exports"; exit 1; }

name="AlphaViewsXR-$version-linux"
mkdir -p "$root/dist"
stage=$(mktemp -d)
out="$stage/$name"
mkdir -p "$out"
cp "$so" "$root/linux/XR_APILAYER_GMO_alpha_views_xr.json" "$root/packaging/linux/install.sh" "$root/packaging/linux/uninstall.sh" "$out/"
cp "$root/packaging/LICENSE.txt" "$root/packaging/THIRD_PARTY_NOTICES.txt" "$out/"
for p in Gestures Controllers Shapes; do
    [ -f "$root/packaging/gestures/$p.png" ] && cp "$root/packaging/gestures/$p.png" "$out/"
done
sed "s/@VERSION@/$version/g" "$root/packaging/linux/README.txt" > "$out/README.txt"
# Plain permissions (a Windows drive under WSL makes everything 777).
find "$out" -type d -exec chmod 755 {} +
find "$out" -type f -exec chmod 644 {} +
chmod 755 "$out/install.sh" "$out/uninstall.sh" "$out/libXR_APILAYER_GMO_alpha_views_xr.so"
tar -C "$stage" --owner=0 --group=0 --numeric-owner -czf "$root/dist/$name.tar.gz" "$name"
rm -rf "$stage"
echo "Made $root/dist/$name.tar.gz"
echo "Needs glibc $(objdump -T "$so" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 | cut -d_ -f2) or newer"
