// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GmoLargey. Part of Alpha Views XR (GNU GPL version 3, see LICENSE.txt).

// Compiled at build time by fxc (see CMakeLists.txt) into headers included by layer.cpp.
cbuffer C : register(b0) { float4x4 mvp; float4 col; float4 camR; float4 camU; };   // camR / camU: this eye's right / up
struct VO { float4 pos : SV_POSITION; float4 col : COLOR; };

// Fullscreen triangle.
float4 vsFull(uint id : SV_VertexID) : SV_POSITION {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Window cutout triangles (already in world space).
float4 vsMesh(float3 p : POSITION) : SV_POSITION { return mul(float4(p, 1), mvp); }

// Instanced spheres: inst = centre + radius, c = colour.
VO vsSphere(float3 p : POSITION, float4 inst : TEXCOORD0, float4 c : TEXCOORD1) {
    VO o; o.pos = mul(float4(inst.xyz + p * inst.w, 1), mvp); o.col = c; return o;
}

float4 psConst(float4 p : SV_POSITION) : SV_TARGET { return col; }
float4 psColor(VO i) : SV_TARGET { return i.col; }

// Windows with depth (boxes, keep boxes): every window's triangles are drawn with psBox, and
// each pixel looks at ALL windows at once, so overlapping windows combine instead of one
// simply adding its passthrough on top of the other. For each pixel, rebuild the 3D point the
// game drew there (from the game's depth buffer), then:
//   - The smallest box (normal or keep) that contains that point decides it ("inner box wins"):
//     a normal box cuts it to passthrough, a keep box keeps the game's surface. So a keep box
//     inside a normal box keeps a game object, and a normal box inside a keep box cuts one.
//   - A point no box contains is passthrough if the eye sees it through a flat window, or
//     through a keep box (the room around a kept character). Otherwise the game stays.
// Example: a normal box around the steering wheel shows the real wheel and hands, while the
// game's dials behind the box stay visible.
cbuffer B : register(b1) {
    float4 rectC;          // colour image rect: x, y, width, height (pixels)
    float4 rectD;          // depth image rect: x, y, width, height (texels)
    float4 tanLRDU;        // tan of the eye's fov angles: left, right, down, up
    float4 depthMap;       // minDepth, 1 / (maxDepth - minDepth), 1 / nearZ, 1 / farZ
    float4 info;           // x = 1: game depth available
    float4 boxCol;         // output colour (alpha 0 = passthrough, or a solid key colour)
};
#define MAX_WIN 32         // kMaxWindows in layer.cpp
struct WinData {
    float4x4 toLocal;      // eye view space (metres) -> window local space
    float4 dim;            // half width, half height, half depth, kind: 0 flat, 1 box, 2 keep box
    float4 radii;          // corner radii (corner order as in layer.cpp)
    float4 shape;          // x = free shape point count (0 = rounded rectangle), y = 1: circle, z = box volume
    float4 pts[16];        // free shape points (local x, y), two per float4
};
cbuffer W : register(b2) { float4 winCount; WinData wins[MAX_WIN]; };
Texture2D<float> gDepth : register(t0);

float2 ShapePt(int w, int i) { float4 v = wins[w].pts[i >> 1]; return (i & 1) ? v.zw : v.xy; }
// Even-odd point-in-polygon test against window w's free shape.
bool InShape(int w, float2 q) {
    int n = (int)wins[w].shape.x;
    bool c = false;
    [loop] for (int i = 0, j = n - 1; i < n; j = i++) {
        float2 a = ShapePt(w, i), b = ShapePt(w, j);
        if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) c = !c;
    }
    return c;
}

// Inside window w's outline (local x, y): free shape, circle, or rounded rectangle.
bool InOutline(int w, float2 q) {
    float2 h = wins[w].dim.xy;
    bool inside;
    if (wins[w].shape.x > 2.5) {
        inside = InShape(w, q);
    } else if (wins[w].shape.y > 0.5) {   // circle: an ellipse filling the window
        float2 e = q / h;
        inside = dot(e, e) <= 1;
    } else {
        float4 rd = wins[w].radii;
        float2 a = abs(q);
        float r = q.x < 0 ? (q.y < 0 ? rd.x : rd.w) : (q.y < 0 ? rd.y : rd.z);
        float2 c = a - (h - r);
        inside = a.x <= h.x && a.y <= h.y && !(c.x > 0 && c.y > 0 && length(c) > r);
    }
    return inside;
}

// Does the eye -> surface segment (e -> l, window w's local space) pass through window w's box?
// Clip it to the box's bounds (slabs), then sample the clipped part against the outline.
bool Through(int w, float3 e, float3 l) {
    float3 d = l - e;
    d = (abs(d) < 1e-6) ? 1e-6 : d;
    float3 ext = float3(wins[w].dim.xy, max(wins[w].dim.z, 0.0005));
    float3 t0 = (-ext - e) / d, t1 = (ext - e) / d;
    float3 tn = min(t0, t1), tf = max(t0, t1);
    float a = max(max(tn.x, tn.y), max(tn.z, 0)), b = min(min(tf.x, tf.y), min(tf.z, 1));
    bool hit = false;
    if (a <= b) {                    // else: in front of the box (or beside it)
        [loop] for (int i = 0; i <= 12 && !hit; ++i) {
            float3 q = e + d * lerp(a, b, i / 12.0);
            hit = InOutline(w, q.xy);
        }
    }
    return hit;
}

float4 psBox(float4 p : SV_POSITION) : SV_TARGET {
    if (info.x < 0.5) return boxCol; // no game depth: every window shape shows passthrough
    float2 uv = (p.xy - rectC.xy) / rectC.zw;
    int2 t = int2(rectD.xy + saturate(uv) * (rectD.zw - 1));
    float z01 = saturate((gDepth.Load(int3(t, 0)) - depthMap.x) * depthMap.y);
    // Standard perspective depth: 1/distance is linear in the stored value. Works for reversed
    // and infinite depth too (1/inf = 0). Sky / nothing drawn: 10 km away, beyond any box.
    float inv = (1 - z01) * depthMap.z + z01 * depthMap.w;
    float dist = inv <= 1e-4 ? 1e4 : 1 / inv;
    float3 v = float3(lerp(tanLRDU.x, tanLRDU.y, uv.x) * dist, lerp(tanLRDU.w, tanLRDU.z, uv.y) * dist, -dist);
    int n = (int)winCount.x;
    int best = -1;
    float bestVol = 1e30;
    bool seen = false;               // seen through a flat window or a keep box
    [loop] for (int w = 0; w < n; ++w) {
        float kind = wins[w].dim.w;
        float3 l = mul(float4(v, 1), wins[w].toLocal).xyz;
        float3 e = mul(float4(0, 0, 0, 1), wins[w].toLocal).xyz;
        if (kind < 0.5) {            // flat window: everything seen through its outline
            float dz = l.z - e.z;
            if (!seen && abs(dz) > 1e-7) {
                float s = -e.z / dz;
                if (s > 0 && InOutline(w, (e + (l - e) * s).xy)) seen = true;
            }
            continue;
        }
        if (abs(l.z) <= wins[w].dim.z && InOutline(w, l.xy)) {
            if (wins[w].shape.z < bestVol) { bestVol = wins[w].shape.z; best = w; }
        } else if (kind > 1.5 && !seen && Through(w, e, l)) {
            seen = true;
        }
    }
    if (best >= 0) {
        if (wins[best].dim.w > 1.5) discard;   // inside a keep box (its innermost box): the game stays
        return boxCol;
    }
    if (seen) return boxCol;
    discard;
    return boxCol;
}

// Hands behind game objects (Ctrl+Alt+O): the hand cutout spheres, but a pixel is only cut where
// the hand is in front of what the game drew there. Where the game's surface is closer (the
// in-game wheel rim in front of your fingers), the game stays on top. Uses cbuffer B's
// rectC, rectD and depthMap, and the game depth texture, like psBox.
struct VOD { float4 pos : SV_POSITION; float4 col : COLOR; float vz : TEXCOORD0; };
VOD vsSphereD(float3 p : POSITION, float4 inst : TEXCOORD0, float4 c : TEXCOORD1) {
    VOD o; o.pos = mul(float4(inst.xyz + p * inst.w, 1), mvp); o.col = c;
    o.vz = o.pos.w;   // distance along the view direction (perspective w)
    return o;
}
float4 psHandD(VOD i) : SV_TARGET {
    float2 uv = (i.pos.xy - rectC.xy) / rectC.zw;
    int2 t = int2(rectD.xy + saturate(uv) * (rectD.zw - 1));
    float z01 = saturate((gDepth.Load(int3(t, 0)) - depthMap.x) * depthMap.y);
    float inv = (1 - z01) * depthMap.z + z01 * depthMap.w;
    // 1.5 cm of slack, so a hand resting on a game surface doesn't flicker.
    if (inv > 1e-5 && 1 / inv < i.vz - 0.015) discard;
    return i.col;
}

// Edit-mode handles: smooth discs with a dark rim and a symbol for what the handle does.
// One quad per handle. inst = centre + radius, c = disc colour, a.xyz / b.xyz = the quad's right /
// up (the window's plane; zero = face the eye), a.w = symbol (ICON_* in layer.cpp), g = symbol
// colour (alpha 0 = automatic: white, or dark on light discs).
// Glyph() is mirrored in layer.cpp (IconGlyph) for the guide panel's legend: keep them in step.
struct IVO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; float4 gcol : COLOR1; nointerpolation float icon : TEXCOORD1; };
static const float kIconPad = 1.15;   // quad a little bigger than the disc, for its soft edge
IVO vsIcon(float2 q : POSITION, float4 inst : TEXCOORD0, float4 c : TEXCOORD1, float4 a : TEXCOORD2, float4 b : TEXCOORD3, float4 g : TEXCOORD4) {
    float3 ax = a.xyz, ay = b.xyz;
    if (dot(ax, ax) < 1e-6) { ax = camR.xyz; ay = camU.xyz; }
    IVO o;
    o.uv = q * kIconPad;
    o.pos = mul(float4(inst.xyz + (ax * o.uv.x + ay * o.uv.y) * inst.w, 1), mvp);
    o.col = c; o.gcol = g; o.icon = a.w;
    return o;
}

float Seg(float2 p, float2 a, float2 b) {
    float2 pa = p - a, ba = b - a;
    return length(pa - ba * saturate(dot(pa, ba) / dot(ba, ba)));
}
float Chevron(float2 p, float2 tip, float2 dir, float h) {   // arrow head at tip, pointing along dir
    float2 n = float2(-dir.y, dir.x);
    return min(Seg(p, tip, tip - (dir - n) * h), Seg(p, tip, tip - (dir + n) * h));
}
float Arrow2(float2 p, float2 a, float2 b, float h) {        // double-headed arrow
    float2 d = normalize(b - a);
    return min(Seg(p, a, b), min(Chevron(p, b, d, h), Chevron(p, a, -d, h)));
}
float Box(float2 p, float2 c, float2 h) { float2 d = abs(p - c) - h; return length(max(d, 0)) + min(max(d.x, d.y), 0); }
float Side(float2 p, float2 a, float2 b) {                   // > 0 outside a counter-clockwise polygon edge
    float2 e = normalize(b - a), v = p - a; return v.x * e.y - v.y * e.x;
}

// Signed distance to the symbol (< 0 inside), in disc units (disc radius 1).
float Glyph(int id, float2 p) {
    float g = 1e3;
    if (id == 1) {          // move: four-way arrow
        g = min(Arrow2(p, float2(-0.6, 0), float2(0.6, 0), 0.2), Arrow2(p, float2(0, -0.6), float2(0, 0.6), 0.2)) - 0.075;
    }
    if (id == 2) {          // resize: diagonal arrow (the quad is turned to point at its corner)
        g = Arrow2(p, float2(-0.55, 0), float2(0.55, 0), 0.24) - 0.085;
    }
    if (id == 3) {          // round: a rounded corner (corner towards +x +y)
        float2 q = p - float2(-0.04, -0.04);
        float arc = (q.x > 0 && q.y > 0) ? abs(length(q) - 0.44) : 1e3;
        g = min(arc, min(Seg(p, float2(0.4, -0.5), float2(0.4, -0.04)), Seg(p, float2(-0.04, 0.4), float2(-0.5, 0.4)))) - 0.085;
    }
    if (id == 4) {          // depth: wire box
        float d = abs(Box(p, float2(-0.12, -0.12), float2(0.3, 0.3)));
        d = min(d, Seg(p, float2(-0.18, 0.42), float2(0.42, 0.42)));
        d = min(d, Seg(p, float2(0.42, -0.18), float2(0.42, 0.42)));
        d = min(d, Seg(p, float2(-0.42, 0.18), float2(-0.18, 0.42)));
        d = min(d, Seg(p, float2(0.18, 0.18), float2(0.42, 0.42)));
        d = min(d, Seg(p, float2(0.18, -0.42), float2(0.42, -0.18)));
        g = d - 0.065;
    }
    if (id == 5) {          // keep box: solid box (the inverse of the wire box)
        float2 A = float2(-0.42, -0.42), B = float2(0.18, -0.42), C = float2(0.42, -0.18),
               D = float2(0.42, 0.42), E = float2(-0.18, 0.42), F = float2(-0.42, 0.18);
        float hex = max(max(max(Side(p, A, B), Side(p, B, C)), max(Side(p, C, D), Side(p, D, E))), max(Side(p, E, F), Side(p, F, A)));
        float2 K = float2(0.18, 0.18);
        float lines = min(min(Seg(p, F, K), Seg(p, K, B)), Seg(p, K, D)) - 0.045;
        g = max(hex - 0.03, -lines);
    }
    if (id == 6) {          // to free shape: a polygon with its points
        float2 P0 = float2(-0.52, -0.28), P1 = float2(0.12, -0.52), P2 = float2(0.52, 0.02), P3 = float2(0.08, 0.5), P4 = float2(-0.46, 0.3);
        float d = min(min(min(Seg(p, P0, P1), Seg(p, P1, P2)), min(Seg(p, P2, P3), Seg(p, P3, P4))), Seg(p, P4, P0)) - 0.055;
        float dots = min(min(min(length(p - P0), length(p - P1)), min(length(p - P2), length(p - P3))), length(p - P4)) - 0.13;
        g = min(d, dots);
    }
    if (id == 7) {          // to rectangle: rounded rectangle
        g = abs(Box(p, float2(0, 0), float2(0.36, 0.2)) - 0.16) - 0.07;
    }
    if (id == 8) {          // draw: pencil, tip at the bottom left
        float2 r = float2(p.x + p.y, p.y - p.x) * 0.70710678;
        float body = Box(r, float2(0.2, 0), float2(0.36, 0.15));
        float2 A = float2(-0.58, 0), B = float2(-0.16, -0.15), C = float2(-0.16, 0.15);
        float tip = max(max(Side(r, A, B), Side(r, B, C)), Side(r, C, A));
        float gaps = min(abs(r.x + 0.12), abs(r.x - 0.36)) - 0.03;
        g = max(min(body, tip), -gaps);
    }
    if (id == 9) {          // shape point: a dot in the dot
        g = length(p) - 0.32;
    }
    if (id == 11) {         // to circle: a ring
        g = abs(length(p) - 0.48) - 0.08;
    }
    if (id == 10) {         // add a point: plus
        g = min(Seg(p, float2(-0.5, 0), float2(0.5, 0)), Seg(p, float2(0, -0.5), float2(0, 0.5))) - 0.12;
    }
    return g;
}

float4 psIcon(IVO i) : SV_TARGET {
    float2 p = i.uv;
    float aa = max(max(fwidth(p.x), fwidth(p.y)), 1e-4);   // one pixel, in disc units
    float r = length(p);
    float disc = saturate((1 - r) / aa + 0.5);
    if (disc <= 0) discard;
    int id = (int)(i.icon + 0.5);
    float3 base = i.col.rgb;
    float3 c = base * (1.05 - 0.15 * r * r);                 // a little lighter in the middle
    float rimW = max(id == 0 ? 0.2 : 0.12, aa);              // dark rim: shows on any background
    c = lerp(c, base * 0.3, saturate((r - (1 - rimW)) / aa + 0.5));
    if (id > 0) {
        float lum = dot(base, float3(0.3, 0.59, 0.11));
        float3 gc = i.gcol.a > 0 ? i.gcol.rgb : (lum > 0.74 ? float3(0.08, 0.09, 0.11) : float3(1, 1, 1));
        c = lerp(c, gc, saturate(0.5 - Glyph(id, p) / aa));
    }
    return float4(c, disc * i.col.a);
}

// Edit-mode guide panel: a textured quad (text drawn by GDI), always fully opaque (alpha 1 = no passthrough).
struct PVI { float3 p : POSITION; float2 uv : TEXCOORD0; };
struct PVO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
PVO vsPanel(PVI i) { PVO o; o.pos = mul(float4(i.p, 1), mvp); o.uv = i.uv; return o; }
Texture2D gPanel : register(t1);
SamplerState gSamp : register(s0);
float4 psPanel(PVO i) : SV_TARGET { return float4(gPanel.Sample(gSamp, i.uv).rgb, 1); }
