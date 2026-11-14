ALPHA VIEWS XR  v@VERSION@  (Linux)
by GmoLargey  -  https://github.com/GmoLargey/Alpha-Views-XR
If you want to support me: https://ko-fi.com/virtuallyreal
Free, open-source software under the GNU GPL version 3 (see LICENCE at the end).

See your real hands, steering wheel and flight controls inside VR games with
added depth support, using your headset's passthrough camera.

Alpha Views XR is an OpenXR API layer: it cuts holes in the game image so your
real room shows through, around your HANDS (tracked live) and in WINDOWS you
place around your wheel, shifter, button box, etc. A window can be flat, or a
BOX with depth that only lets through the parts of the game inside it.

The full guide (gestures, controller buttons, handles, boxes, box faces, keep
boxes, presets) is the Windows README, which applies here too: see
https://github.com/GmoLargey/Alpha-Views-XR. The pictures in this folder show
the gestures, controller buttons and shapes. The edit-mode guide panel in the
headset shows the same.


=============================================================================
NEEDS
=============================================================================
  - Linux x86-64 with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36,
    SteamOS 3, Arch and newer).
  - An OpenXR runtime with passthrough by alpha blending (environment blend
    mode ALPHA_BLEND), e.g. WiVRn or Monado with a passthrough-capable
    headset, or a colour-key passthrough streamer (cutout colour, below).
  - A game that runs in OpenXR mode with Vulkan (native Linux games, or
    Windows games through Proton whose OpenXR goes to the Linux runtime).
    OpenGL games: not supported.


=============================================================================
INSTALL
=============================================================================
  ./install.sh
Installs for your user only (no root): the layer goes to
~/.local/share/AlphaViewsXR/layer/ and its manifest to
~/.local/share/openxr/1/api_layers/implicit.d/, so every OpenXR game loads it.
Run it again to update. Remove with ./uninstall.sh.

Flatpak (e.g. a Flatpak Steam): the sandbox must see the layer and the
manifest. Install inside the same home folder and allow access, for example:
  flatpak override --user --filesystem=xdg-data/openxr:ro \
      --filesystem=xdg-data/AlphaViewsXR com.valvesoftware.Steam

Turn the layer off for one launch: set the environment variable
DISABLE_XR_APILAYER_GMO_alpha_views_xr=1 (Steam: launch options
"DISABLE_XR_APILAYER_GMO_alpha_views_xr=1 %command%").


=============================================================================
USING IT
=============================================================================
Exactly as on Windows, with hands or controllers. There are no Ctrl+Alt
keyboard shortcuts on Linux (a layer can't watch the keyboard there):
  Edit mode            picture-frame gesture with both hands, or both grips +
                       triggers held 1.2 s
  New window           picture frame (editing), or both grips held 0.6 s
  Save and finish      thumbs up, or grips + triggers again
  Delete window        thumbs down, or hold B or Y
  Cutout colour        V sign, or thumbstick press (editing): alpha / green /
                       blue / pink
  Hands on / off       prayer (palms together)
  Box depth            green box handle, or thumbstick up / down
  Box faces            the box faces button beside the keep box button: whole
                       box, top + far wall, far wall + floor, all but front +
                       floor
  Presets 1-3          on the guide panel: hold to save, tap to load


=============================================================================
FILES
=============================================================================
In ~/.local/share/AlphaViewsXR (or $XDG_DATA_HOME/AlphaViewsXR):
  log.txt               the last game's log (send it when reporting a problem)
  windows_<game>.cfg    saved windows for each game (same format as Windows:
                        you can copy them across)
  windows_<game>_preset1.cfg (2, 3)   that game's presets
  settings.cfg          hand cutout settings
  key_<runtime>.cfg     cutout colour for each runtime

The guide panel's text uses your desktop's sans-serif font (fontconfig), or
DejaVu / Noto / Liberation Sans. With none installed the panel has no text:
install fonts-dejavu (or similar).


=============================================================================
TROUBLESHOOTING
=============================================================================
  - No log.txt at all: the game isn't using OpenXR, or the loader didn't find
    the layer. Check that the manifest exists (INSTALL); for Flatpak see above.
    XR_LOADER_DEBUG=all in the game's environment shows what the loader does.
  - "has no alpha passthrough, so cutouts are hidden": the runtime doesn't offer
    ALPHA_BLEND. Use a cutout colour (V sign or thumbstick press in edit mode)
    with a colour-key passthrough, if your setup has one.
  - "not D3D11, D3D12 or Vulkan": the game uses OpenGL, which isn't supported.


=============================================================================
LICENCE
=============================================================================
Alpha Views XR is free, open-source software under the GNU General Public
License version 3 (GPL-3.0). You may use it for anything (including while
streaming or recording), share it, and change it. If you share a changed
version, or the program itself, you must also make its source code available
under the same licence. Full terms: LICENSE.txt. Built-in parts under their
own licences (stb_truetype, DirectXMath, Vulkan and OpenXR headers):
THIRD_PARTY_NOTICES.txt.

Source code: https://github.com/GmoLargey/Alpha-Views-XR, and
AlphaViewsXR-<version>-source.zip published with each release.

It comes with NO WARRANTY: use it at your own risk.
- It can fail or show the wrong area at any time (for example if tracking is
  lost or a setting is wrong). Don't rely on it to keep you safe; keep your
  play area clear as you would without it.
- It adds an OpenXR API layer to your games. Some games, online services and
  anti-cheat systems don't allow programs that change what a game shows.
  Check that the games and services you use allow it.


Copyright (C) 2026 GmoLargey  -  https://github.com/GmoLargey/Alpha-Views-XR
If you want to support me: https://ko-fi.com/virtuallyreal
