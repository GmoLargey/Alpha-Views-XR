#!/bin/sh
# Alpha Views XR (Linux): removes the OpenXR API layer. Saved windows and settings are kept
# (delete ~/.local/share/AlphaViewsXR to remove them too).
data=${XDG_DATA_HOME:-$HOME/.local/share}
name=XR_APILAYER_GMO_alpha_views_xr
rm -f "$data/openxr/1/api_layers/implicit.d/$name.json" "$data/AlphaViewsXR/layer/lib$name.so"
rmdir "$data/AlphaViewsXR/layer" 2>/dev/null || true
echo "Alpha Views XR removed. Saved windows are kept in $data/AlphaViewsXR"
