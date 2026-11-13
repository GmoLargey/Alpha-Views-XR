ALPHA VIEWS XR  v1.4.3
by GmoLargey  -  https://github.com/GmoLargey
If you want to support me: https://ko-fi.com/virtuallyreal
Free, open-source software under the GNU GPL version 3 (see LICENCE at the end).

See your real hands, steering wheel and flight controls inside PC VR games with
added depth support, using your headset's passthrough camera.

Alpha Views XR cuts holes in the game image so your real room shows through:
  - your HANDS, tracked live
  - WINDOWS you place around your wheel, shifter, button box, etc.
    A window can be flat, or a BOX with depth. A box only lets through the parts
    of the game inside it: put one around your wheel and you see your real wheel
    and hands, while the game's dials behind it stay visible. (Boxes need the
    game's depth: without it a box is just a plain cube of passthrough.)

Pictures in this folder (also in the Start menu):
  Gestures.png      hand gestures
  Controllers.png   controller buttons
  Keyboard.png      keyboard shortcuts
  Shapes.png        free shapes, keep boxes and cutout colours


=============================================================================
1. BEFORE YOU START
=============================================================================
You need:
  - Windows 10/11 (64-bit) and a headset with passthrough, streaming PC VR.
  - A game that runs in OpenXR mode with Direct3D 11 (64-bit or 32-bit). Some VR
    mods block other OpenXR layers (e.g. the Dead Space VR mod); those can't work.
    (Direct3D 12: experimental, see section 9. Vulkan / OpenGL: not supported.
    SteamVR-only (OpenVR) games don't work: SteamVR removes the transparency.)

Check these:
  1. The right OpenXR runtime is selected: the one for the app that streams to
     your headset. SteamVR Settings > OpenXR shows it as "Current OpenXR
     Runtime" (SteamVR-Settings.png is an example).
  2. On the same page, under Manage OpenXR API Layers, Alpha Views XR is On.
  3. Passthrough, one of:
     a) ALPHA CHANNEL: if your streamer or PC runtime can pass transparency
        through (an "alpha" option), turn it on. Cutouts are then see-through.
        (For example PICO Connect has an "Alpha" developer option.)
     b) COLOUR KEY: if it can't, use its colour-key passthrough feature
        (e.g. Virtual Desktop), and set Alpha Views XR to paint cutouts that
        colour (Ctrl+Alt+C, see section 6).

Try it first with the test scene (Start menu: "Alpha Views XR test scene"). It
shows a spinning cube just below eye level, within reach: put a hand behind it
to test "hands behind objects". Recentring your headset, or pressing R, puts the
cube back in front of you.


=============================================================================
2. QUICK START
=============================================================================
  1. Start the game. Your hands already show as passthrough.
  2. Open EDIT MODE: picture-frame gesture with both hands, or both controller
     grips + triggers, or Ctrl+Alt+E. A guide panel appears ahead of you.
  3. Make a WINDOW: picture-frame gesture around your wheel, or both grips at
     opposite corners, or Ctrl+Alt+N.
     Up to 32 windows per game; at 32, delete one before making another.
  4. Adjust it with its handles (section 3).
  5. Finish: thumbs up, or grips + triggers again, or Ctrl+Alt+E.
     Windows are saved for each game.


=============================================================================
3. WINDOW HANDLES (edit mode)
=============================================================================
Select a window by grabbing it. Its handles then appear, each with a symbol:

  Blue, four-way arrow (centre)   move, tilt and rotate the window
  Orange, diagonal arrow (corner) resize. Shrink below 2 cm to delete
  Magenta curve (inside a corner) drag to round that corner
  Green box (right of the window, pull towards you to make a box, deeper.
  dotted link)                    Push back flat for a flat window
  Solid box button (beside green) keep box on / off (white while on, section 7)
  Padlock button (above, left)    lock the window's angle: moving it then never
                                  tilts or turns it. Gold while locked
  Purple button (above)           rectangle <-> free shape (section 5)
  Teal ring button (above, beside purple)
                                  rectangle <-> circle. A circle can still be
                                  moved, resized (to an oval), made a box or a
                                  keep box
  Pencil button (above, shapes)   draw the shape's outline (section 5)
  Blue upright button (above,     snap: square the window up, facing you. First
  right)                          press stands a tilted window upright; each
                                  further press: upright facing forward, upright
                                  facing the side, lying level

How you grab a handle depends on what you use: see the next three sections.


=============================================================================
4a. USING HANDS (put your controllers down)
=============================================================================
Grabbing:
  - With eye tracking: LOOK at a handle or window (it lights up), then pinch
    wherever your hand is. Move your hand to drag it.
  - Without eye tracking: reach out and pinch the handle or window itself.

Gestures (hold each one for about a second):
  Picture frame  Both hands make an "L", thumb tips together, index fingers up.
                 Not editing: opens edit mode. Editing: new window in the frame.
  Thumbs up      Edit mode: save and leave edit mode. (Does nothing outside
                 edit mode, so it's safe on the wheel.)
  Thumbs down    Edit mode: delete the selected window.
  Prayer         Palms together: show / hide the hand cutouts.
  V sign         Edit mode: next cutout colour (section 6).

Virtual Desktop: hand gestures need hand tracking passed to PC games. Quest
headsets: in Virtual Desktop's settings turn on forwarding of hand tracking
data (1.30 or newer), then restart the game. PICO headsets: Virtual Desktop
doesn't pass PICO hand tracking on, so use controllers (section 4b).


=============================================================================
4b. USING CONTROLLERS
=============================================================================
Works alongside hand tracking, in games with their own controller support.
The game still gets the buttons too, so pause it while editing.

  Both grips + both triggers, hold 1.2 s   edit mode on / save and leave
In edit mode:
  Trigger                    grab: touch a handle or window with the front of
                             the controller, or look at it, or point the laser
  Both grips, hold 0.6 s     new window between the controllers (hold them at
                             opposite corners)
  Thumbstick up / down       selected window deeper / shallower (box)
  Thumbstick press           next cutout colour (section 6)
  B or Y, hold 0.8 s         delete the selected window

Laser (headsets without eye tracking): point at a handle or window and pull
the trigger. A held window moves with the controller. While holding it:
  Thumbstick up / down       push it away / pull it closer
  Thumbstick left / right    turn it (not while its angle is locked)
To make a box with the laser, point at the green box handle, hold the trigger
and use the thumbstick. The deeper the box, the faster it grows.


=============================================================================
4c. USING THE KEYBOARD (hold Ctrl + Alt, then press)
=============================================================================
  E      edit mode on / off           N      new window in front of you
  [  ]   selected window: box depth -/+ 1 cm
  X      delete all windows           P      windows on / off
  J      hand cutouts on / off        - =    shrink / grow hand cutouts
  O      hands behind game objects    V      tint cutouts to see them
  C      cutout colour (section 6)    G      guide panel on / off
  1 2 3  load preset 1-3              Shift+1 2 3  save preset 1-3 (section 8)
  D      experimental Direct3D 12 (section 9)
  K      force alpha (test only)


=============================================================================
5. FREE-SHAPE WINDOWS
=============================================================================
For any outline other than a rounded rectangle (a wheel rim, a button box):
  - Tap the purple button above the selected window. It shows what a tap turns
    the window into: a free shape, or back to a rectangle.
  - Orange points: drag to move, quick tap to remove (minimum 3).
  - Small "+" on each edge: pinch to add a point there. Up to 32 points.
  - Pencil: tap it (turns red), then pinch and trace the outline. A yellow dot
    marks where you started: come back to it (it turns green) and the shape
    finishes by itself. Letting go early also finishes it, closed straight
    across. Tap the red pencil again to cancel.
    You draw on a flat surface facing you where you start, so you can turn to
    face the spot first; the window then turns to face you there.
  - The green box handle works the same, so shapes can be boxes too.


=============================================================================
6. CUTOUT COLOUR (alpha or colour key)
=============================================================================
  alpha   cutouts are transparent (needs an alpha channel, section 1)
  green / blue / pink   cutouts are painted that colour, for a streamer or
          runtime that swaps a colour for passthrough (colour key)
Change it with Ctrl+Alt+C, a V sign or a thumbstick press (edit mode). Then set
your streamer's colour-key passthrough to the same colour.
  - Saved per runtime, so one can use alpha while another uses a colour.
  - Pick a colour the game doesn't use much, or those parts key out too.
  - Boxes still use the game's depth, so they key out the same way (no depth:
    the box is a plain cube of colour).


=============================================================================
7. BOXES, KEEP BOXES AND HANDS BEHIND OBJECTS
=============================================================================
These need the game to share its depth. The guide panel shows "Game depth:
supported" if it does.
IMPORTANT: a box can only tell what is inside it from the game's depth. If the
game doesn't send depth, objects in the box are not cut out separately: the
whole box just shows as a plain cube of passthrough.

Box: only the game's surfaces INSIDE the box show passthrough. For a wheel,
pull the green box handle until the box just covers the rim and hub, not the
dials behind. Keep the game's own wheel visible and lined up with your real one.
Boxes can be as deep as you like (up to 100 m), e.g. a whole room.

Keep box (the opposite): the box shows your room, but whatever the game draws
inside it stays, e.g. a character standing in your room. Tap the solid box
button to switch. Keep boxes have a gold outline in edit mode. Keep the bottom
of the box just above the floor, or the game's floor stays too.

Hands behind objects (on by default, Ctrl+Alt+O): a game surface closer than
your hand covers it, so the in-game wheel rim stays in front of your fingers.


=============================================================================
8. GUIDE PANEL
=============================================================================
In edit mode a panel floats ahead of you. It shows the gestures, controller
buttons, keys and handle symbols, plus the selected window, the game's depth
support, and the runtime, alpha channel and cutout colour. Ctrl+Alt+G hides it.

Move the panel: grab it like a window (pinch or trigger on it, look at it and
pinch, or point the laser at it). It stays where you put it until you leave
edit mode.

Move ALL windows together: hold the "Hold to move all windows" button at the
top of the panel (pinch or trigger on it) and move your hand. Every window moves
with it, keeping their places relative to each other; turn your hand left or
right to turn them all around where you grabbed. Let go to place them (saved).
Handy when your seat or play area has moved.

PRESETS 1-3: saved layouts of all your windows, kept for each game. Next to the
move-all button on the panel:
  - HOLD a preset button (1.5 s, it fills up) to save your current windows to it.
  - TAP it to put the windows back exactly as saved in that preset.
  Keyboard: Ctrl+Alt+Shift+1/2/3 saves, Ctrl+Alt+1/2/3 loads (works outside
  edit mode too).
Save a preset once everything is right. If a window gets knocked out of place,
tap the preset to get it back. If your seat has moved since, load the preset,
then nudge everything with "move all" and save the preset again.


=============================================================================
9. EXPERIMENTAL: DIRECT3D 12 GAMES
=============================================================================
Many newer games (e.g. Unreal Engine 5) use Direct3D 12. To try them:
  1. Start the game (log.txt says "D3D12 game: layer inactive").
  2. Press Ctrl+Alt+D (Windows plays a sound). Saved for all games.
  3. Restart the game.
Press Ctrl+Alt+D again to switch it off. Untested in many games: if anything
goes wrong, switch it off and send log.txt with the game's name. Boxes use the
game's depth in Direct3D 12 games too, when the game sends it (log.txt says
"Game sends depth"); otherwise they show as plain cubes of passthrough.
UEVR: turn on "Enable Depth-based Latency Reduction". Unreal Engine 5 games may
still send no depth through UEVR.


=============================================================================
10. TROUBLESHOOTING
=============================================================================
log.txt (see FILES) says what happened in the last game.
  - No log at all: the game isn't using OpenXR, or the layer is off. Check the
    OpenXR runtime and the API layer switch (section 1).
  - "has no alpha passthrough, so cutouts are hidden": turn on your streamer's
    alpha channel, or use a colour key (section 6).
  - Cutouts are black: transparency isn't reaching the headset. Check the
    streamer's alpha option, or use a colour key.
  - "D3D12 game: layer inactive": see section 9.
  - "not D3D11 or D3D12": unsupported graphics API.
  - Looking at handles doesn't highlight them: eye tracking must be on and
    calibrated. Touching the handles always works.
  - "System says hand tracking not supported": turn on hand tracking for PC
    games in your streamer (section 4a).
  - Distorted view: restart the game after your streamer has fully connected.
  - Turn the layer off for one launch: set the environment variable
    DISABLE_XR_APILAYER_GMO_alpha_views_xr=1.


=============================================================================
INSTALL, FILES, UNINSTALL
=============================================================================
Install: run AlphaViewsXR-Setup-1.4.3.exe (asks for admin), or for the zip,
extract it somewhere permanent and double-click Install.bat.

Files, in %LOCALAPPDATA%\AlphaViewsXR:
  log.txt               the last game's log (send it when reporting a problem)
  windows_<game>.cfg    saved windows for each game
  windows_<game>_preset1.cfg (2, 3)   that game's presets
  settings.cfg          hand cutout settings
  key_<runtime>.cfg     cutout colour for each runtime

Uninstall: Windows Settings > Apps > Alpha Views XR, or Uninstall.bat for the
zip. Saved windows are kept; delete %LOCALAPPDATA%\AlphaViewsXR to remove them.


=============================================================================
LICENCE
=============================================================================
Alpha Views XR is free, open-source software under the GNU General Public
License version 3 (GPL-3.0). You may use it for anything (including while
streaming or recording), share it, and change it. If you share a changed
version, or the program itself, you must also make its source code available
under the same licence. Full terms: LICENSE.txt.

Source code: https://github.com/GmoLargey, and AlphaViewsXR-<version>-source.zip
published with each release. The test scene includes the OpenXR loader and
JsonCpp under their own licences: THIRD_PARTY_NOTICES.txt.

It comes with NO WARRANTY: use it at your own risk.
- It can fail or show the wrong area at any time (for example if tracking is
  lost or a setting is wrong). Don't rely on it to keep you safe; keep your
  play area clear as you would without it.
- It adds an OpenXR API layer to your games. Some games, online services and
  anti-cheat systems don't allow programs that change what a game shows.
  Check that the games and services you use allow it.


Copyright (C) 2026 GmoLargey  -  https://github.com/GmoLargey
If you want to support me: https://ko-fi.com/virtuallyreal
