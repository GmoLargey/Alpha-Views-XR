# Draws the gesture pictures shown on the installer's "How to use" page.
# Output: packaging\gestures\<name>_<px>.bmp, one size per Windows display scale.
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot 'gestures'
New-Item -ItemType Directory -Force $out | Out-Null

$skin = [Drawing.Color]::FromArgb(242, 200, 166)
$line = [Drawing.Color]::FromArgb(90, 62, 43)
$OL = 2.2   # outline width, in drawing units (canvas is 100 x 100)

function Pen($color, [float]$w) {
    $p = New-Object Drawing.Pen($color, $w)
    $p.StartCap = 'Round'; $p.EndCap = 'Round'; $p.LineJoin = 'Round'
    $p
}
function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); $p
}

# A hand = rounded-rect blocks @(x,y,w,h,r) + capsule segments @(x1,y1,x2,y2,width).
# Outlines are drawn for everything first, then skin, so the hand reads as one shape.
function Hand($g, [float]$cx, [float]$cy, [float]$s, [float]$rot, [bool]$mirror, $blocks, $segs) {
    $st = $g.Save()
    $g.TranslateTransform($cx, $cy); $g.RotateTransform($rot)
    $g.ScaleTransform($(if ($mirror) { -$s } else { $s }), $s)
    foreach ($pass in 0, 1) {
        $c = if ($pass -eq 0) { $line } else { $skin }
        $extra = if ($pass -eq 0) { 2 * $OL } else { 0 }
        foreach ($b in $blocks) {
            $path = RoundRect $b[0] $b[1] $b[2] $b[3] $b[4]
            if ($pass -eq 0) { $g.DrawPath((Pen $c $extra), $path) }
            $g.FillPath((New-Object Drawing.SolidBrush($c)), $path)
        }
        foreach ($q in $segs) { $g.DrawLine((Pen $c ($q[4] + $extra)), [float]$q[0], [float]$q[1], [float]$q[2], [float]$q[3]) }
    }
    $g.Restore($st)
}

$palm = ,@(-18, -18, 36, 36, 8)
# Palm facing you, thumb on the left. Fingers 7 wide at x = -13.5, -4.5, 4.5, 13.5.
$frameSegs = @(@(-13.5, -16, -13.5, -46, 7.5), @(-16, 4, -42, 4, 9),           # index up, thumb out
               @(-4.5, -16, -4.5, -20, 7.5), @(4.5, -16, 4.5, -20, 7.5), @(13.5, -16, 13.5, -19, 7.5))
$pinchSegs = @(@(4.5, -16, 5, -46, 7.5), @(13.5, -16, 15, -40, 7.5), @(-4.5, -16, -5, -45, 7.5),
               @(-13.5, -16, -26, -33, 7.5), @(-16, 4, -27, -31, 9))             # index + thumb tips touch
$fist = ,@(-16, -12, 30, 34, 8)
$fistSegs = @(@(-1, -8, 16, -8, 7.5), @(-1, 0, 16, 0, 7.5), @(-1, 8, 16, 8, 7.5), @(-1, 16, 15, 16, 7.5),
              @(-9, -10, -9, -38, 10))                                            # curled fingers + thumb

function Dot($g, [float]$x, [float]$y, [float]$r, $color) {
    $g.FillEllipse((New-Object Drawing.SolidBrush($color)), $x - $r, $y - $r, 2 * $r, 2 * $r)
    $g.DrawEllipse((Pen ([Drawing.Color]::FromArgb(60, 60, 60)) 1.2), $x - $r, $y - $r, 2 * $r, 2 * $r)
}
function Ring($g, [float]$x, [float]$y, [float]$r, $color) {
    $g.DrawEllipse((Pen $color 2), $x - $r, $y - $r, 2 * $r, 2 * $r)
}
$blue = [Drawing.Color]::FromArgb(40, 120, 255)
$orange = [Drawing.Color]::FromArgb(255, 150, 30)
$magenta = [Drawing.Color]::FromArgb(230, 50, 200)
$gold = [Drawing.Color]::FromArgb(255, 200, 0)

# Edit-mode handles, drawn like the headset draws them (psIcon in shaders.hlsl): a disc with a
# dark rim and a symbol for what it does. $kind: move, resize, round, depth, keep, to_shape,
# to_circle, to_rect, draw, point, add. $angle turns the symbol (degrees, anticlockwise); $sx / $sy mirror it
# (round: the curve faces +x +y, i.e. the top-right corner). Symbol units: disc radius 1, y up.
function HCol([float]$r, [float]$g, [float]$b) { [Drawing.Color]::FromArgb([int](255 * $r), [int](255 * $g), [int](255 * $b)) }
$hMove = HCol 0.15 0.45 1; $hResize = HCol 1 0.45 0; $hRound = HCol 1 0.2 1; $hRoundSel = HCol 0.2 1 1
$hDepth = HCol 0.1 0.68 0.22; $hKeepOn = HCol 0.95 0.95 0.95; $hKeepGlyph = HCol 0.1 0.68 0.22
$hKeepOff = HCol 0.42 0.42 0.42; $hKeepOffGlyph = HCol 0.16 0.55 0.26; $hShape = HCol 0.4 0.2 0.8
$hDraw = HCol 0.92 0.92 0.92; $hDrawArmed = HCol 0.95 0.2 0.2; $hPoint = HCol 1 0.45 0; $hAdd = HCol 1 0.78 0.55
$hCircle = HCol 0.05 0.62 0.7
function PF([float]$x, [float]$y) { New-Object Drawing.PointF($x, $y) }
function Handle($g, [float]$x, [float]$y, [float]$r, [string]$kind, $col, $glyphCol = $null, [float]$angle = 0, [float]$sx = 1, [float]$sy = 1) {
    $st = $g.Save()
    $g.TranslateTransform($x, $y); $g.RotateTransform(-$angle); $g.ScaleTransform($r * $sx, -$r * $sy)
    $g.FillEllipse((New-Object Drawing.SolidBrush($col)), -1, -1, 2, 2)
    $dark = [Drawing.Color]::FromArgb([int]($col.R * 0.3), [int]($col.G * 0.3), [int]($col.B * 0.3))
    $g.DrawEllipse((New-Object Drawing.Pen($dark, 0.13)), -0.935, -0.935, 1.87, 1.87)
    if (-not $glyphCol) {
        $lum = (0.3 * $col.R + 0.59 * $col.G + 0.11 * $col.B) / 255
        $glyphCol = if ($lum -gt 0.74) { [Drawing.Color]::FromArgb(20, 23, 28) } else { [Drawing.Color]::White }
    }
    $gb = New-Object Drawing.SolidBrush($glyphCol)
    function Ln([float]$w, $c, [float]$x1, [float]$y1, [float]$x2, [float]$y2) { $g.DrawLine((Pen $c $w), $x1, $y1, $x2, $y2) }
    function Chev([float]$w, [float]$tx, [float]$ty, [float]$dx, [float]$dy, [float]$h) {
        Ln $w $glyphCol $tx $ty ($tx - ($dx + $dy) * $h) ($ty - ($dy - $dx) * $h)
        Ln $w $glyphCol $tx $ty ($tx - ($dx - $dy) * $h) ($ty - ($dy + $dx) * $h)
    }
    switch ($kind) {
        'move' {
            Ln 0.15 $glyphCol -0.6 0 0.6 0; Ln 0.15 $glyphCol 0 -0.6 0 0.6
            Chev 0.15 0.6 0 1 0 0.2; Chev 0.15 -0.6 0 -1 0 0.2; Chev 0.15 0 0.6 0 1 0.2; Chev 0.15 0 -0.6 0 -1 0.2
        }
        'resize' { Ln 0.17 $glyphCol -0.55 0 0.55 0; Chev 0.17 0.55 0 1 0 0.24; Chev 0.17 -0.55 0 -1 0 0.24 }
        'round' {
            $p = Pen $glyphCol 0.17
            $path = New-Object Drawing.Drawing2D.GraphicsPath
            $path.AddLine(0.4, -0.5, 0.4, -0.04)
            $path.AddArc(-0.48, -0.48, 0.88, 0.88, 0, 90)
            $path.AddLine(-0.04, 0.4, -0.5, 0.4)
            $g.DrawPath($p, $path)
        }
        'depth' {
            $g.DrawRectangle((Pen $glyphCol 0.13), -0.42, -0.42, 0.6, 0.6)
            foreach ($s in @(-0.18, 0.42, 0.42, 0.42), @(0.42, -0.18, 0.42, 0.42), @(-0.42, 0.18, -0.18, 0.42), @(0.18, 0.18, 0.42, 0.42), @(0.18, -0.42, 0.42, -0.18)) {
                Ln 0.13 $glyphCol $s[0] $s[1] $s[2] $s[3]
            }
        }
        'keep' {
            $hex = [Drawing.PointF[]]@((PF -0.45 -0.45), (PF 0.2 -0.45), (PF 0.45 -0.2), (PF 0.45 0.45), (PF -0.2 0.45), (PF -0.45 0.2))
            $g.FillPolygon($gb, $hex)
            foreach ($s in @(-0.45, 0.18, 0.18, 0.18), @(0.18, 0.18, 0.18, -0.45), @(0.18, 0.18, 0.45, 0.45)) { Ln 0.09 $col $s[0] $s[1] $s[2] $s[3] }
        }
        'to_shape' {
            $q = [Drawing.PointF[]]@((PF -0.52 -0.28), (PF 0.12 -0.52), (PF 0.52 0.02), (PF 0.08 0.5), (PF -0.46 0.3))
            $g.DrawPolygon((Pen $glyphCol 0.11), $q)
            foreach ($v in $q) { $g.FillEllipse($gb, $v.X - 0.13, $v.Y - 0.13, 0.26, 0.26) }
        }
        'to_rect' { $g.DrawPath((Pen $glyphCol 0.14), (RoundRect -0.52 -0.36 1.04 0.72 0.16)) }
        'to_circle' { $g.DrawEllipse((Pen $glyphCol 0.16), -0.48, -0.48, 0.96, 0.96) }
        'draw' {
            # pencil along the diagonal, tip at the bottom left; (u, v) = along, across
            $k = 0.70710678
            function UV([float]$u, [float]$v) { PF (($u - $v) * $k) (($u + $v) * $k) }
            $g.FillPolygon($gb, [Drawing.PointF[]]@((UV -0.58 0), (UV -0.16 -0.15), (UV 0.56 -0.15), (UV 0.56 0.15), (UV -0.16 0.15)))
            foreach ($u in -0.12, 0.36) { $a = UV $u -0.2; $b = UV $u 0.2; $g.DrawLine((New-Object Drawing.Pen($col, 0.06)), $a, $b) }
        }
        'point' { $g.FillEllipse($gb, -0.32, -0.32, 0.64, 0.64) }
        'add' { Ln 0.24 $glyphCol -0.5 0 0.5 0; Ln 0.24 $glyphCol 0 -0.5 0 0.5 }
    }
    $g.Restore($st)
}

# A generic VR controller, side view, front (trigger side) facing left. $hi lists the parts to
# highlight: trigger, grip, stick, button. Local units: about 30 wide, 62 tall, centred on the handle.
# The grab point ("tip") is at local (-15, -15).
$ctlBody = [Drawing.Color]::FromArgb(78, 84, 98)
$ctlLine = [Drawing.Color]::FromArgb(30, 33, 40)
$ctlPart = [Drawing.Color]::FromArgb(120, 128, 144)
$hiCol = [Drawing.Color]::FromArgb(255, 196, 0)
function Ctl($g, [float]$cx, [float]$cy, [float]$s, [bool]$mirror, $hi) {
    $st = $g.Save()
    $g.TranslateTransform($cx, $cy)
    $g.ScaleTransform($(if ($mirror) { -$s } else { $s }), $s)
    $body = New-Object Drawing.SolidBrush($ctlBody)
    $edge = Pen $ctlLine 2
    function Part($path, $name) {
        $c = if ($hi -contains $name) { $hiCol } else { $ctlPart }
        $g.FillPath((New-Object Drawing.SolidBrush($c)), $path); $g.DrawPath($edge, $path)
    }
    # trigger (under the front of the head), drawn first so the head overlaps it
    Part (RoundRect -19 -9 10 12 4) 'trigger'
    # handle and head
    $handle = RoundRect -8 -10 15 40 7
    $head = RoundRect -14 -24 26 18 8
    foreach ($p in $handle, $head) { $g.DrawPath((Pen $ctlLine 4), $p) }
    foreach ($p in $handle, $head) { $g.FillPath($body, $p) }
    # grip button on the handle, face button and thumbstick on top
    Part (RoundRect -10 4 6 18 3) 'grip'
    Part (RoundRect -10 -28 8 5 2.5) 'button'
    $stem = New-Object Drawing.Drawing2D.GraphicsPath; $stem.AddRectangle((New-Object Drawing.RectangleF(2, -30, 4, 7)))
    Part $stem 'stick'
    Part (RoundRect -2 -35 12 6 3) 'stick'
    $g.Restore($st)
    # gold rings around the highlighted parts (drawn unscaled-thick, in canvas space)
    $sx = if ($mirror) { -$s } else { $s }
    $at = @{ trigger = @(-14, -3); grip = @(-7, 13); button = @(-6, -25.5); stick = @(4, -31) }
    foreach ($h in $hi) {
        $c = $at[$h]
        Ring $g ($cx + $sx * $c[0]) ($cy + $s * $c[1]) (8 * $s) $hiCol
    }
}
function Tip([float]$cx, [float]$cy, [float]$s, [bool]$mirror) {
    $sx = if ($mirror) { -$s } else { $s }
    @(($cx + $sx * -15), ($cy + $s * -15))
}
function Label($g, $text, [float]$x, [float]$y, [float]$size) {
    $f = New-Object Drawing.Font('Segoe UI', $size, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $w = $g.MeasureString($text, $f).Width
    $g.DrawString($text, $f, (New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(70, 70, 70))), ($x - $w / 2), $y)
}
function Arrow($g, [float]$x1, [float]$y1, [float]$x2, [float]$y2, $color) {
    $p = Pen $color 3
    $p.CustomEndCap = New-Object Drawing.Drawing2D.AdjustableArrowCap(3, 3)
    $g.DrawLine($p, $x1, $y1, $x2, $y2)
}
function Eye($g, [float]$x, [float]$y) {
    $eye = New-Object Drawing.Drawing2D.GraphicsPath
    $eye.AddBezier(($x - 14), $y, ($x - 8), ($y - 12), ($x + 8), ($y - 12), ($x + 14), $y)
    $eye.AddBezier(($x + 14), $y, ($x + 8), ($y + 12), ($x - 8), ($y + 12), ($x - 14), $y)
    $g.FillPath([Drawing.Brushes]::White, $eye); $g.DrawPath((Pen $line 2.2), $eye)
    Dot $g $x $y 6 $blue; Dot $g $x $y 2.5 ([Drawing.Color]::Black)
}
$green = [Drawing.Color]::FromArgb(40, 190, 90)
$red = [Drawing.Color]::FromArgb(220, 40, 40)
$frameGrey = [Drawing.Color]::FromArgb(140, 140, 140)
$purple = [Drawing.Color]::FromArgb(150, 70, 255)
$paleOrange = [Drawing.Color]::FromArgb(255, 195, 130)
# V sign: index and middle up and spread, ring and little folded, thumb across the palm.
$vSegs = @(@(-13.5, -16, -22, -46, 7.5), @(-4.5, -16, 2, -47, 7.5),
           @(4.5, -16, 4.5, -19, 7.5), @(13.5, -16, 13.5, -18, 7.5), @(-16, 4, -3, -6, 9))
# The four cutout colours in a 2 x 2 grid (top-left alpha = checkerboard), centred at (x, y).
function Swatches($g, [float]$x, [float]$y, [float]$s) {
    $cols = @($null, [Drawing.Color]::FromArgb(0, 230, 0), [Drawing.Color]::FromArgb(0, 0, 255), [Drawing.Color]::FromArgb(255, 0, 255))
    for ($i = 0; $i -lt 4; $i++) {
        $sx = $x - $s - 1 + ($i % 2) * ($s + 2); $sy = $y - $s - 1 + [math]::Floor($i / 2) * ($s + 2)
        if ($cols[$i]) { $g.FillRectangle((New-Object Drawing.SolidBrush($cols[$i])), $sx, $sy, $s, $s) }
        else {
            $g.FillRectangle([Drawing.Brushes]::White, $sx, $sy, $s, $s)
            $h = $s / 2
            $g.FillRectangle([Drawing.Brushes]::LightGray, $sx, $sy, $h, $h); $g.FillRectangle([Drawing.Brushes]::LightGray, ($sx + $h), ($sy + $h), $h, $h)
        }
        $g.DrawRectangle((Pen ([Drawing.Color]::FromArgb(90, 90, 90)) 0.8), $sx, $sy, $s, $s)
    }
}

# Keyboard picture: simplified layout, Ctrl + Alt and the shortcut keys coloured by group.
$keyGroups = [ordered]@{
    hold   = @{ col = [Drawing.Color]::FromArgb(40, 170, 90);   name = 'Hold Ctrl + Alt' }
    edit   = @{ col = [Drawing.Color]::FromArgb(50, 120, 240);  name = 'Windows' }
    view   = @{ col = [Drawing.Color]::FromArgb(240, 140, 30);  name = 'Show / hide' }
    hands  = @{ col = [Drawing.Color]::FromArgb(210, 60, 190);  name = 'Hands' }
    adv    = @{ col = [Drawing.Color]::FromArgb(130, 110, 190); name = 'Advanced' }
}
$keyOf = @{ '1' = 'edit'; '2' = 'edit'; '3' = 'edit'; 'E' = 'edit'; 'N' = 'edit'; 'X' = 'edit'; '[' = 'edit'; ']' = 'edit'
            'P' = 'view'; 'J' = 'view'; 'V' = 'view'; 'G' = 'view'; 'C' = 'view'
            '-' = 'hands'; '=' = 'hands'; 'O' = 'hands'; 'D' = 'adv'; 'K' = 'adv' }
$kbRows = @(
    @(@('`', 1), @('1', 1), @('2', 1), @('3', 1), @('4', 1), @('5', 1), @('6', 1), @('7', 1), @('8', 1), @('9', 1), @('0', 1), @('-', 1), @('=', 1), @('Back', 2)),
    @(@('Tab', 1.5), @('Q', 1), @('W', 1), @('E', 1), @('R', 1), @('T', 1), @('Y', 1), @('U', 1), @('I', 1), @('O', 1), @('P', 1), @('[', 1), @(']', 1), @('\', 1.5)),
    @(@('Caps', 1.75), @('A', 1), @('S', 1), @('D', 1), @('F', 1), @('G', 1), @('H', 1), @('J', 1), @('K', 1), @('L', 1), @(';', 1), @("'", 1), @('Enter', 2.25)),
    @(@('Shift', 2.25), @('Z', 1), @('X', 1), @('C', 1), @('V', 1), @('B', 1), @('N', 1), @('M', 1), @(',', 1), @('.', 1), @('/', 1), @('Shift', 2.75)),
    @(@('Ctrl', 1.5, 'hold'), @('Win', 1.25), @('Alt', 1.25, 'hold'), @('', 5.75), @('Alt', 1.25), @('Win', 1.25), @('Menu', 1.25), @('Ctrl', 1.5))
)
# Draws the keyboard with its top-left at (x, y), u pixels per key. Size: 15.6u x 5.6u.
function Keyboard($g, [float]$x, [float]$y, [float]$u) {
    $base = RoundRect $x $y (15.6 * $u) (5.6 * $u) (0.35 * $u)
    $g.FillPath((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(205, 209, 216))), $base)
    $fBig = New-Object Drawing.Font('Segoe UI', (0.42 * $u), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $fSmall = New-Object Drawing.Font('Segoe UI', (0.27 * $u), [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $gap = 0.08 * $u
    for ($r = 0; $r -lt $kbRows.Count; $r++) {
        $kx = $x + 0.3 * $u
        foreach ($k in $kbRows[$r]) {
            $w = $k[1] * $u
            $grp = if ($k.Count -gt 2) { $k[2] } elseif ($keyOf.ContainsKey($k[0]) -and $k[0].Length -eq 1) { $keyOf[$k[0]] } else { $null }
            $rect = New-Object Drawing.RectangleF(($kx + $gap), ($y + 0.3 * $u + $r * $u + $gap), ($w - 2 * $gap), ($u - 2 * $gap))
            $path = RoundRect $rect.X $rect.Y $rect.Width $rect.Height (0.15 * $u)
            if ($grp) {
                $g.FillPath((New-Object Drawing.SolidBrush($keyGroups[$grp].col)), $path)
                $g.DrawString($k[0], $(if ($k[0].Length -gt 1) { $fSmall } else { $fBig }), [Drawing.Brushes]::White, $rect, $fmt)
            } else {
                $g.FillPath((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(250, 250, 252))), $path)
                $g.DrawPath((Pen ([Drawing.Color]::FromArgb(175, 180, 190)) ([Math]::Max(1, 0.03 * $u))), $path)
                $g.DrawString($k[0], $fSmall, (New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(150, 155, 165))), $rect, $fmt)
            }
            $kx += $w
        }
    }
}

$icons = [ordered]@{
    frame = {
        param($g)
        $p = Pen ([Drawing.Color]::FromArgb(120, 120, 120)) 2.5; $p.DashStyle = 'Dash'
        # Thumb tips close together (touching or a small gap), index fingers up; the window is the area they frame.
        $s = 0.62
        $g.DrawRectangle($p, (46 - 28.5 * $s), 22, (8 + 57 * $s), (78 - 22 - 4 * $s))
        Hand $g (46 - 42 * $s) (80 - 4 * $s) $s 0 $true $palm $frameSegs
        Hand $g (54 + 42 * $s) (80 - 4 * $s) $s 0 $false $palm $frameSegs
    }
    gaze = {
        param($g)
        # Eye looking at a dot, hand pinching somewhere else.
        $eye = New-Object Drawing.Drawing2D.GraphicsPath
        $eye.AddBezier(6, 24, 12, 12, 28, 12, 34, 24); $eye.AddBezier(34, 24, 28, 36, 12, 36, 6, 24)
        $g.FillPath([Drawing.Brushes]::White, $eye); $g.DrawPath((Pen $line 2.2), $eye)
        Dot $g 20 24 6 $blue; Dot $g 20 24 2.5 ([Drawing.Color]::Black)
        $p = Pen ([Drawing.Color]::FromArgb(110, 110, 110)) 1.8; $p.DashStyle = 'Dot'
        $g.DrawLine($p, 36, 24, 74, 24)
        Ring $g 82 24 10.5 $gold; Handle $g 82 24 8 'move' $hMove
        Hand $g 52 76 0.5 0 $false $palm $pinchSegs
        Ring $g (52 - 26.5 * 0.5) (76 - 32 * 0.5) 4.5 $gold
    }
    dots = {
        param($g)
        # A selected window and its handles: move (centre), resize (corners), round (inside the
        # corners), depth (just outside the right edge, dotted link).
        $g.DrawPath((Pen ([Drawing.Color]::FromArgb(140, 140, 140)) 3), (RoundRect 8 14 68 72 4))
        Handle $g 42 50 9.5 'move' $hMove
        # x, y, inward x / y (screen), outward angle
        foreach ($c in @(8, 14, 1, 1, 135), @(76, 14, -1, 1, 45), @(76, 86, -1, -1, -45), @(8, 86, 1, -1, -135)) {
            Handle $g $c[0] $c[1] 7.5 'resize' $hResize $null $c[4]
            Handle $g ($c[0] + 15 * $c[2]) ($c[1] + 15 * $c[3]) 6 'round' $hRound $null 0 (-$c[2]) $c[3]
        }
        foreach ($x in 80, 84) { $g.FillEllipse((New-Object Drawing.SolidBrush($hDepth)), $x - 1, 49, 2, 2) }
        Handle $g 91 50 6.5 'depth' $hDepth
    }
    thumbsup = { param($g) Hand $g 48 60 1.0 0 $false $fist $fistSegs }
    thumbsdown = { param($g) Hand $g 52 40 1.0 180 $false $fist $fistSegs }
    prayer = {
        param($g)
        # Two flat hands, palms together, fingers up.
        $flat = ,@(-17, -2, 16, 34, 7)
        $flatSegs = @(@(-7.5, 0, -6, -40, 11), @(-14, 16, -8, 2, 7))
        Hand $g 49 56 1.0 5 $false $flat $flatSegs
        Hand $g 51 56 1.0 -5 $true $flat $flatSegs
    }
    ctl_edit = {
        param($g)
        # Both controllers: grips + triggers held.
        Ctl $g 22 54 1.0 $true @('trigger', 'grip')
        Ctl $g 78 54 1.0 $false @('trigger', 'grip')
        Label $g '1.2 s' 50 85 13
    }
    ctl_grab = {
        param($g)
        # Controller tip on the window's blue dot, trigger pulled.
        $g.DrawPath((Pen $frameGrey 2.5), (RoundRect 4 8 40 34 7))
        Handle $g 24 25 8 'move' $hMove
        $t = Tip 68 60 0.95 $false
        $p = Pen ([Drawing.Color]::FromArgb(110, 110, 110)) 1.8; $p.DashStyle = 'Dot'
        $g.DrawLine($p, 30, 29, $t[0], $t[1])
        Ctl $g 68 60 0.95 $false @('trigger')
    }
    ctl_gaze = {
        param($g)
        # Look at a dot, pull the trigger wherever the controller is.
        Eye $g 20 22
        $p = Pen ([Drawing.Color]::FromArgb(110, 110, 110)) 1.8; $p.DashStyle = 'Dot'
        $g.DrawLine($p, 36, 22, 72, 22)
        Ring $g 82 22 10.5 $gold; Handle $g 82 22 8 'move' $hMove
        Ctl $g 58 70 0.72 $false @('trigger')
    }
    ctl_laser = {
        param($g)
        # No eye tracking: a laser from the controller tip. Trigger grabs what it points at;
        # holding a window, the thumbstick pushes it away or pulls it closer.
        $g.FillPath((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(225, 240, 225))), (RoundRect 4 6 42 28 4))
        $g.DrawPath((Pen $frameGrey 2.5), (RoundRect 4 6 42 28 4))
        $lp = Pen ([Drawing.Color]::FromArgb(70, 190, 240)) 2.6; $lp.DashStyle = 'Dot'
        $g.DrawLine($lp, 53, 63, 30, 22)
        Ring $g 30 22 6.5 $gold; Dot $g 30 22 3.2 ([Drawing.Color]::FromArgb(70, 190, 240))
        Arrow $g 49.3 38.1 44.4 29.4 $green
        Arrow $g 49.3 38.1 54.2 46.8 $green
        Ctl $g 64 74 0.72 $false @('trigger', 'stick')
    }
    ctl_new = {
        param($g)
        # Both grips: a new window between the two tips (opposite corners).
        $a = Tip 24 38 0.6 $true; $b = Tip 78 74 0.6 $false
        $p = Pen ([Drawing.Color]::FromArgb(120, 120, 120)) 2.5; $p.DashStyle = 'Dash'
        $g.DrawRectangle($p, $a[0], $a[1], ($b[0] - $a[0]), ($b[1] - $a[1]))
        Ctl $g 24 38 0.6 $true @('grip')
        Ctl $g 78 74 0.6 $false @('grip')
        Label $g '0.6 s' 50 4 13
    }
    ctl_depth = {
        param($g)
        # Thumbstick up / down: box deeper / shallower.
        Ctl $g 30 62 0.9 $false @('stick')
        Arrow $g 50 30 50 12 $green
        Arrow $g 50 40 50 58 $green
        # a box: front and back faces joined at the corners
        $f = @(56, 50, 24, 22); $o = 9
        $fill = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(70, 40, 190, 90))
        $g.FillRectangle($fill, ($f[0] + $o), ($f[1] - $o), $f[2], $f[3])
        $g.FillRectangle($fill, $f[0], $f[1], $f[2], $f[3])
        $pb = Pen $green 2
        $g.DrawRectangle($pb, ($f[0] + $o), ($f[1] - $o), $f[2], $f[3])
        $g.DrawRectangle($pb, $f[0], $f[1], $f[2], $f[3])
        foreach ($c in @(0, 0), @(1, 0), @(0, 1), @(1, 1)) {
            $x = $f[0] + $c[0] * $f[2]; $y = $f[1] + $c[1] * $f[3]
            $g.DrawLine($pb, $x, $y, ($x + $o), ($y - $o))
        }
        Handle $g 93 61 6 'depth' $hDepth
    }
    ctl_delete = {
        param($g)
        # Hold B / Y: delete the selected window.
        Ctl $g 32 62 0.9 $false @('button')
        $g.DrawPath((Pen $frameGrey 2.5), (RoundRect 58 22 34 28 6))
        $px = Pen $red 4
        $g.DrawLine($px, 66, 28, 84, 44); $g.DrawLine($px, 84, 28, 66, 44)
        Label $g '0.8 s' 75 58 13
    }
    # --- 1.4.0: free shapes and key colours
    vsign = {
        param($g)
        # V sign: index and middle up and apart, the rest folded. Swatches: the colours it cycles.
        Hand $g 40 72 0.95 0 $false $palm $vSegs
        Swatches $g 76 24 10
        Label $g '0.8 s' 80 70 13
    }
    ctl_colour = {
        param($g)
        # Thumbstick press: next cutout colour.
        Ctl $g 32 62 0.9 $false @('stick')
        Swatches $g 74 30 11
    }
    shape_type = {
        param($g)
        # Purple button above the window (shows a free shape): rectangle -> free shape. Afterwards the
        # button shows a rectangle (tap again to go back).
        $g.DrawPath((Pen $frameGrey 2.5), (RoundRect 6 34 34 30 4))
        Ring $g 16 20 10.5 $gold; Handle $g 16 20 8 'to_shape' $hShape
        Handle $g 35 20 6.5 'to_circle' $hCircle
        Arrow $g 44 50 56 50 ([Drawing.Color]::FromArgb(110, 110, 110))
        $poly = [Drawing.PointF[]]@((New-Object Drawing.PointF(62, 40)), (New-Object Drawing.PointF(80, 32)), (New-Object Drawing.PointF(96, 46)),
                                    (New-Object Drawing.PointF(88, 68)), (New-Object Drawing.PointF(70, 62)))
        $g.DrawPolygon((Pen $frameGrey 2.5), $poly)
        foreach ($p in $poly) { Handle $g $p.X $p.Y 4.5 'point' $hPoint }
        Handle $g 79 16 7.5 'to_rect' $hShape
    }
    shape_points = {
        param($g)
        # Shape points: drag an orange point, the small + on each edge adds a point.
        $pts = @(@(14, 30), @(50, 14), @(86, 34), @(80, 80), @(30, 74))
        $poly = [Drawing.PointF[]]@($pts | ForEach-Object { New-Object Drawing.PointF($_[0], $_[1]) })
        $g.DrawPolygon((Pen $frameGrey 2.5), $poly)
        for ($i = 0; $i -lt $pts.Count; $i++) {
            $a = $pts[$i]; $b = $pts[($i + 1) % $pts.Count]
            Handle $g (($a[0] + $b[0]) / 2) (($a[1] + $b[1]) / 2) 4.5 'add' $hAdd
        }
        foreach ($p in $pts) { Handle $g $p[0] $p[1] 6.5 'point' $hPoint }
        Ring $g 86 34 10 $gold
        Arrow $g 88 38 94 52 $gold
    }
    shape_draw = {
        param($g)
        # Draw: tap the pencil (red while armed), then pinch and trace the outline.
        Handle $g 16 15 9 'draw' $hDrawArmed
        Ring $g 16 15 11.5 $gold
        $path = New-Object Drawing.Drawing2D.GraphicsPath
        $path.AddBezier(40, 30, 62, 8, 96, 22, 88, 50)
        $path.AddBezier(88, 50, 82, 74, 52, 78, 40, 62)
        $path.AddBezier(40, 62, 30, 50, 34, 40, 44, 32)
        $p = Pen ([Drawing.Color]::FromArgb(90, 90, 90)) 2.5; $p.DashStyle = 'Dash'
        $g.DrawPath($p, $path)
        $g.FillEllipse((New-Object Drawing.SolidBrush($gold)), 35, 25, 10, 10)   # yellow start dot: finish here
        Hand $g 30 84 0.42 0 $false $palm $pinchSegs
        Ring $g (30 - 26.5 * 0.42) (84 - 32 * 0.42) 4 $gold
    }
    keep_box = {
        param($g)
        # Keep box: inside the box you see your room (beige), but the game's character in it stays.
        $g.FillRectangle((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(150, 175, 210))), 2, 6, 96, 88)   # the game
        $f = @(12, 34, 50, 50); $o = 12
        $room = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(222, 205, 180))
        $pts = [Drawing.PointF[]]@((New-Object Drawing.PointF($f[0], $f[1])), (New-Object Drawing.PointF(($f[0] + $o), ($f[1] - $o))),
                                   (New-Object Drawing.PointF(($f[0] + $f[2] + $o), ($f[1] - $o))), (New-Object Drawing.PointF(($f[0] + $f[2] + $o), ($f[1] + $f[3] - $o))),
                                   (New-Object Drawing.PointF(($f[0] + $f[2]), ($f[1] + $f[3]))), (New-Object Drawing.PointF($f[0], ($f[1] + $f[3]))))
        $g.FillPolygon($room, $pts)
        # a hint of the room: floor line and a picture on the wall
        $rp = Pen ([Drawing.Color]::FromArgb(170, 150, 120)) 2
        $g.DrawLine($rp, $f[0], ($f[1] + 36), ($f[0] + $f[2] + $o), ($f[1] + 36 - $o))
        $g.DrawRectangle($rp, 48, 30, 14, 10)
        # the character the game keeps
        $body = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(40, 80, 200))
        $g.FillEllipse($body, 29, 40, 13, 13)
        $g.FillPath($body, (RoundRect 26 54 19 30 6))
        # box outline (gold, like the keep box in the headset), the green depth handle and the keep
        # button beside it (white with a solid box while on)
        $pb = Pen $gold 2
        $g.DrawRectangle($pb, ($f[0] + $o), ($f[1] - $o), $f[2], $f[3])
        $g.DrawRectangle($pb, $f[0], $f[1], $f[2], $f[3])
        foreach ($c in @(0, 0), @(1, 0), @(0, 1), @(1, 1)) {
            $x = $f[0] + $c[0] * $f[2]; $y = $f[1] + $c[1] * $f[3]
            $g.DrawLine($pb, $x, $y, ($x + $o), ($y - $o))
        }
        $g.FillEllipse((New-Object Drawing.SolidBrush($hDepth)), 65, 58, 2, 2)
        Handle $g 73 59 5.5 'depth' $hDepth
        Handle $g 89 59 7 'keep' $hKeepOn $hKeepGlyph
        Ring $g 89 59 9.5 $gold
    }
    keycolour = {
        param($g)
        # Key colour: the cutout is painted green, the streamer swaps green for the real view.
        $g.FillPath((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(0, 230, 0))), (RoundRect 4 22 38 38 6))
        Arrow $g 46 41 58 41 ([Drawing.Color]::FromArgb(110, 110, 110))
        $g.FillPath((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(205, 190, 170))), (RoundRect 60 22 38 38 6))
        $wp = Pen ([Drawing.Color]::FromArgb(50, 50, 55)) 3.5
        $g.DrawEllipse($wp, 66, 28, 26, 26)
        $g.DrawLine($wp, 66, 41, 92, 41); $g.DrawLine($wp, 79, 41, 79, 54)
        Label $g 'key out green' 50 66 12
    }
}

foreach ($px in 80, 100, 120, 160) {
    foreach ($name in $icons.Keys) {
        $bmp = New-Object Drawing.Bitmap($px, $px, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'
        $g.Clear([Drawing.Color]::White)
        $g.ScaleTransform($px / 100.0, $px / 100.0)
        & $icons[$name] $g
        $g.Dispose()
        $bmp.Save((Join-Path $out "${name}_$px.bmp"), [Drawing.Imaging.ImageFormat]::Bmp)
        $bmp.Dispose()
    }
}

# The "How to use" pages. Each list feeds both an installer page and a picture for the install
# folder / zip: Gestures.png (hands), Controllers.png, Keyboard.png.
$credit = 'Alpha Views XR by GmoLargey  -  github.com/GmoLargey/Alpha-Views-XR'
$tip = 'If you want to support me: ko-fi.com/virtuallyreal'
$tipPages = @(
    @{
        png = 'Gestures.png'
        title = 'How to use Alpha Views XR: hands'
        subtitle = 'Put your controllers down and use hand tracking. Hold each gesture for about a second.'
        footer = 'Guide panel (edit mode): grab it to move it. Hold "move all" to move every window. Presets: tap loads, hold saves.'
        tips = @(
            @('frame', 'Picture frame', 'Both hands make an "L", thumb tips close together, index fingers up. First time: opens edit mode. In edit mode: creates a window in the frame.'),
            @('gaze', 'Look + pinch', 'In edit mode, look at a handle or window until it lights up, then pinch wherever your hand is. Move your hand to drag it.'),
            @('dots', 'Window handles', 'Each shows what it does. Arrows in the middle: move, tilt, rotate. Corner arrows: resize. Curves: drag to round a corner. Green box: pull out to make a box.'),
            @('thumbsup', 'Thumbs up', "Saves your edits and leaves edit mode. Does nothing outside edit mode, so it's safe on the wheel."),
            @('thumbsdown', 'Thumbs down', 'In edit mode: deletes the selected window.'),
            @('prayer', 'Prayer', 'Palms together: shows or hides your hands.')
        )
    },
    @{
        png = 'Controllers.png'
        title = 'How to use Alpha Views XR: controllers'
        subtitle = 'Controllers work alongside hand tracking, in games with controller support. The game also gets these buttons, so pause it while editing.'
        footer = 'Gold = the button to use. The grab point is just in front of the controller.'
        tips = @(
            @('ctl_edit', 'Grips + triggers, hold 1.2 s', 'Both controllers at once. Turns edit mode on, or saves your edits and leaves edit mode.'),
            @('ctl_grab', 'Trigger = grab', 'In edit mode, touch a handle or window with the front of the controller, pull the trigger and move. Same handles as with hands.'),
            @('ctl_laser', 'Laser or look + trigger', 'No eye tracking: point the laser and pull the trigger. A held window moves and tilts with the controller; the stick pushes it away or pulls it closer. With eye tracking: look, then pull.'),
            @('ctl_new', 'Both grips, hold 0.6 s', 'In edit mode: a new window between the two controllers. Hold them at opposite corners.'),
            @('ctl_depth', 'Thumbstick', 'Up / down: the selected window becomes a deeper box, or back to flat (boxes need the game''s depth). Press: change the cutout colour.'),
            @('ctl_delete', 'Hold B or Y, 0.8 s', 'In edit mode: deletes the selected window.')
        )
    },
    @{
        png = 'Shapes.png'
        title = 'How to use Alpha Views XR: shapes and boxes'
        subtitle = 'Free-shape windows, keep boxes, and key colours for streamers such as Virtual Desktop. These work in edit mode.'
        footer = 'The cutout colour is saved per runtime: one runtime can use alpha while another uses green.'
        tips = @(
            @('shape_type', 'Purple: shape. Teal: circle', 'Buttons above the selected window. Purple: free shape or back to a rectangle. Teal ring: circle or back to a rectangle; it can still be resized and made a box.'),
            @('shape_points', 'Shape points', 'Drag an orange point to move it. Pinch a small + on an edge to add a point there. Tap a point to remove it.'),
            @('shape_draw', 'Draw a shape', 'Tap the pencil (it turns red), then pinch and trace the outline. Come back to the yellow start dot to finish. Shapes can be boxes too.'),
            @('keep_box', 'Solid box button: keep box', "Beside the green box handle; white while on. Shows your room, but what the game draws inside (a character) stays. Needs game depth, or it's a plain cube."),
            @('vsign', 'V sign, hold 0.8 s', 'Changes the cutout colour, for streamers such as Virtual Desktop that swap a colour for your room. A stick press does the same.'),
            @('keycolour', 'Key colour', "No alpha channel in your streamer or runtime? Paint cutouts a colour and turn on the streamer's colour-key passthrough. Boxes still use the game's depth.")
        )
    }
)
$kbPage = @{
    png = 'Keyboard.png'
    title = 'How to use Alpha Views XR: keyboard'
    subtitle = 'Hold Ctrl + Alt on the PC keyboard and press a key. Works while a game is running.'
}
$keys = @(
    @('E', 'edit', 'Edit mode on / off'),
    @('N', 'edit', 'New window in front of you'),
    @('[  ]', 'edit', 'Selected window: box depth - / + 1 cm'),
    @('X', 'edit', 'Delete all windows'),
    @('1 2 3', 'edit', 'Load preset 1-3 (+ Shift: save it)'),
    @('G', 'view', 'Guide panel in edit mode on / off'),
    @('P', 'view', 'Windows on / off'),
    @('J', 'view', 'Hand cutouts on / off'),
    @('V', 'view', 'Tint cutouts green to see them'),
    @('C', 'view', 'Change the cutout colour'),
    @('-  =', 'hands', 'Shrink / grow hand cutouts'),
    @('O', 'hands', 'Hands behind game objects'),
    @('D', 'adv', 'Experimental Direct3D 12 (restart the game)'),
    @('K', 'adv', 'Force alpha, SteamVR test only')
)

# Installer: tips.iss is #included into installer.iss's [Code] section (InitializeWizard).
function PasStr($s) { "'" + $s.Replace("'", "''") + "'" }
function PasColor($c) { '$00{0:X2}{1:X2}{2:X2}' -f $c.B, $c.G, $c.R }
$iss = @('// Generated by gen_gestures.ps1. Edit the text there, not here.')
$after = 'wpWelcome'
foreach ($pg in $tipPages) {
    $iss += "  Page := CreateCustomPage($after, $(PasStr $pg.title), $(PasStr $pg.subtitle));"
    for ($i = 0; $i -lt $pg.tips.Count; $i++) {
        $t = $pg.tips[$i]
        $iss += "  AddTip(Page, $($i % 2), $([math]::Floor($i / 2)), $(PasStr $t[0]), $(PasStr $t[1]), $(PasStr $t[2]));"
    }
    $iss += "  AddFooter(Page, $(PasStr $pg.footer));"
    $after = 'Page.ID'
}
$iss += "  Page := CreateCustomPage(Page.ID, $(PasStr $kbPage.title), $(PasStr $kbPage.subtitle));"
$iss += "  AddKeyboard(Page);"
$half = [math]::Ceiling($keys.Count / 2)
for ($i = 0; $i -lt $keys.Count; $i++) {
    $k = $keys[$i]
    $iss += "  AddKey(Page, $([math]::Floor($i / $half)), $($i % $half), $(PasStr $k[0]), $(PasStr $k[2]), $(PasColor $keyGroups[$k[1]].col));"
}
Set-Content (Join-Path $out 'tips.iss') $iss -Encoding ASCII

# Keyboard pictures for the installer page, one height per display scale.
foreach ($h in 120, 150, 180, 240) {
    $u = $h / 5.6
    $bmp = New-Object Drawing.Bitmap([int][math]::Ceiling(15.6 * $u), $h, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([Drawing.Color]::White)
    Keyboard $g 0 0 $u
    $g.Dispose()
    $bmp.Save((Join-Path $out "keyboard_$h.bmp"), [Drawing.Imaging.ImageFormat]::Bmp); $bmp.Dispose()
}

# Pictures for the install folder / zip.
$fTitle = New-Object Drawing.Font('Segoe UI', 22, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
$fHead = New-Object Drawing.Font('Segoe UI', 19, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
$fBody = New-Object Drawing.Font('Segoe UI', 17, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$ink = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(30, 30, 30))
$grey = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(90, 90, 90))
function Sheet($W, $H, $title, $subtitle) {
    $png = New-Object Drawing.Bitmap($W, $H)
    $g = [Drawing.Graphics]::FromImage($png)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([Drawing.Color]::White)
    $g.DrawString($title, $fTitle, $ink, 30, 24)
    $g.DrawString($credit, $fBody, $grey, ($W - 30 - $g.MeasureString($credit, $fBody).Width), 28)
    $tipInk = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(20, 120, 200))
    $g.DrawString($tip, $fBody, $tipInk, ($W - 30 - $g.MeasureString($tip, $fBody).Width), 50)
    $g.DrawString($subtitle, $fBody, $grey, (New-Object Drawing.RectangleF(32, 82, ($W - 64), 46)))
    @($png, $g)
}
foreach ($pg in $tipPages) {
    $W = 1100; $cellH = 175; $top = 140; $H = $top + 3 * $cellH + 50; $icon = 120
    $png, $g = Sheet $W $H $pg.title $pg.subtitle
    for ($i = 0; $i -lt $pg.tips.Count; $i++) {
        $t = $pg.tips[$i]
        $x = 30 + ($i % 2) * ($W / 2); $y = $top + [math]::Floor($i / 2) * $cellH
        $st = $g.Save()
        $g.TranslateTransform($x, $y); $g.ScaleTransform($icon / 100.0, $icon / 100.0)
        & $icons[$t[0]] $g
        $g.Restore($st)
        $tx = $x + $icon + 16; $tw = $W / 2 - $icon - 60
        $g.DrawString($t[1], $fHead, $ink, $tx, $y + 4)
        $g.DrawString($t[2], $fBody, $ink, (New-Object Drawing.RectangleF($tx, ($y + 32), $tw, ($cellH - 36))))
    }
    $g.DrawString($pg.footer, $fBody, $grey, 32, ($H - 40))
    $g.Dispose()
    $png.Save((Join-Path $out $pg.png), [Drawing.Imaging.ImageFormat]::Png); $png.Dispose()
}
# Keyboard.png: keyboard picture, colour key, then the shortcut list.
$W = 1100; $u = 1040 / 15.6; $kbTop = 130; $legendY = $kbTop + 5.6 * $u + 22; $listY = $legendY + 44; $rowH = 36
$H = [int]($listY + $half * $rowH + 30)
$png, $g = Sheet $W $H $kbPage.title $kbPage.subtitle
Keyboard $g 30 $kbTop $u
$lx = 32
foreach ($grp in $keyGroups.Keys) {
    $c = $keyGroups[$grp]
    $g.FillPath((New-Object Drawing.SolidBrush($c.col)), (RoundRect $lx ($legendY + 2) 20 20 4))
    $g.DrawString($c.name, $fBody, $ink, ($lx + 26), $legendY)
    $lx += 26 + $g.MeasureString($c.name, $fBody).Width + 30
}
$fKey = New-Object Drawing.Font('Segoe UI', 18, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
$fmt = New-Object Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
for ($i = 0; $i -lt $keys.Count; $i++) {
    $k = $keys[$i]
    $x = 32 + [math]::Floor($i / $half) * ($W / 2); $y = $listY + ($i % $half) * $rowH
    $badge = New-Object Drawing.RectangleF($x, $y, 64, 28)
    $g.FillPath((New-Object Drawing.SolidBrush($keyGroups[$k[1]].col)), (RoundRect $x $y 64 28 6))
    $g.DrawString($k[0], $fKey, [Drawing.Brushes]::White, $badge, $fmt)
    $g.DrawString($k[2], $fBody, $ink, ($x + 76), ($y + 3))
}
$g.Dispose()
$png.Save((Join-Path $out $kbPage.png), [Drawing.Imaging.ImageFormat]::Png); $png.Dispose()

# SteamVR settings screenshot, resized for the installer's "Before you play" page.
$shot = [Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'SteamVR-Settings.png'))
foreach ($h in 200, 250, 300, 400) {
    $w = [int]($shot.Width * $h / $shot.Height)
    $bmp = New-Object Drawing.Bitmap($w, $h, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.DrawImage($shot, 0, 0, $w, $h); $g.Dispose()
    $bmp.Save((Join-Path $out "steamvr_$h.bmp"), [Drawing.Imaging.ImageFormat]::Bmp); $bmp.Dispose()
}
$shot.Dispose()

# Preview sheet for checking the drawings (not shipped).
$names = @($icons.Keys)
$sheet = New-Object Drawing.Bitmap((128 * $names.Count), 128)
$g = [Drawing.Graphics]::FromImage($sheet)
for ($i = 0; $i -lt $names.Count; $i++) {
    $im = [Drawing.Image]::FromFile((Join-Path $out "$($names[$i])_160.bmp"))
    $g.DrawImage($im, 128 * $i, 0, 128, 128); $im.Dispose()
}
$g.Dispose()
$sheet.Save((Join-Path $PSScriptRoot 'gestures_preview.png')); $sheet.Dispose()
