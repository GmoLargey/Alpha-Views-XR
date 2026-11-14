// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GmoLargey. Alpha Views XR is free software under the GNU General Public
// License version 3; see LICENSE.txt. It comes with ABSOLUTELY NO WARRANTY.
//
// Alpha Views XR - OpenXR API layer by GmoLargey (https://github.com/GmoLargey/Alpha-Views-XR).
//
// Makes parts of the game's image transparent (alpha 0) and asks the runtime for
// ALPHA_BLEND, so the headset's passthrough shows the real world there:
//   - your tracked hands (tight to the hand, no forearm)
//   - "windows": rounded rectangles you create and edit with hand gestures or controllers.
//     A window can be flat (everything inside its outline is passthrough) or a BOX with depth:
//     then only the parts of the game that lie inside the box turn into passthrough, using the
//     depth buffer the game sends (XR_KHR_composition_layer_depth). Example: a box around the
//     steering wheel shows the real wheel and hands, but the game's dials behind it stay visible.
//     Games that send no depth: a box acts like a solid block (its whole outline is passthrough).
//     Overlapping windows combine, and the innermost box wins: where the game's surface lies inside
//     more than one box, the smallest of them decides (normal box = passthrough, keep box = the
//     game stays). So a keep box inside a normal box (or a flat window) keeps one game object, and
//     a normal box inside a keep box cuts part of the kept object to passthrough.
// D3D11 games. D3D12 games: experimental, off by default (Ctrl+Alt+D, via D3D11On12); the game's
// D3D12 depth images are wrapped too, so boxes use depth there as well.
//
// Gestures (hand tracking, put controllers down):
//   Picture frame    both hands make an L, thumb tips together or a small gap apart, index fingers up (or L's at opposite corners), hold ~0.6 s.
//                    Not in edit mode -> opens edit mode (no window made).
//                    In edit mode     -> new window between your hands, selected.
//   Thumbs up        hold ~0.8 s in edit mode -> lock in edits (edit mode off, saves).
//                    Never turns edit mode on (a thumb off the wheel rim looks the same).
//   Thumbs down      hold ~0.8 s in edit mode -> delete the selected window
//   Prayer           palms together, fingers pointing the same way, hold ~0.8 s
//                    -> show/hide hand cutouts (saved)
//   V sign           index + middle up and apart, hold ~0.8 s in edit mode -> next cutout colour
//                    (alpha / green / blue / pink). Edit mode only, so it can't fire while driving.
//   In edit mode, pinch (the headset's own pinch, XR_EXT_hand_interaction):
//     Eye tracking headsets (XR_EXT_eye_gaze_interaction): LOOK at a dot or window
//     (it highlights) and pinch wherever your hand is. Hand motion then drives it,
//     scaled up for far targets. No hand rays.
//     Without eye tracking: pinch with your hand at the dot / window itself.
//     Handles are discs with a symbol for what they do (vsIcon / psIcon):
//     window / centre (blue, 4-way arrow)  select it (stays selected) and grab: move, tilt, rotate
//     corner (orange, diagonal arrow)  selected window only: resize; shrink until tiny to delete
//     inside corner (magenta, curve)   selected window only: drag to round that corner.
//                          (Not shown on circles.)
//     circle (teal, ring)  above the window, beside the purple button: rectangle <-> circle /
//                          ellipse. Size, box depth and keep all still work.
//     depth (green, wire box)  selected window only, on its right edge: pull it towards you to
//                          turn the window into a box and make it deeper; push it back flat.
//     keep (solid green box on white; grey while off)  beside the depth handle: keep box, the
//                          inverse of a box. The window shows passthrough, but the game's
//                          surfaces inside the box (or in front of it) stay, e.g. a character
//                          standing in it. Needs the game's depth.
//     box faces (boxes only, past the keep button; its colour is the mode): which faces of the box
//                          are passthrough, as in Alpha Views VR. Green: the whole box (as above).
//                          Orange: top + far wall. Blue: far wall + floor. Purple: all but the front
//                          and the floor. The chosen faces act as walls: a pixel is passthrough where
//                          your view of the game crosses one, so game surfaces in front of a wall or
//                          inside the box stay (keep doesn't apply). Without game depth the walls
//                          are drawn as if the game were far away. Saved with the window.
//     type (purple)        above the selected window: rounded rectangle <-> free shape (shows
//                          the outline it switches to).
//     lock (padlock, left of the purple button; gold while locked, grey while not): lock the
//                          window's angle. Moving it then never tilts or turns it (it follows the
//                          point you grabbed); only the snap button turns it. Saved with the window.
//     snap (pale blue, upright post, right end of the row above the window): square the window
//                          up with the game world, facing you. First press: whichever is nearest of
//                          upright facing forward / back (Z), upright facing the side (X), lying
//                          level (Y). Each further press: the next one of the three.
//     Boxes can be as deep as you like (up to 100 m; the thumbstick speeds up on deep boxes).
//     Free shapes (up to 32 points, flat or box): orange point handles drag (tap = remove),
//     small "+" handles on the edges add a point, the pencil arms tracing (red): the next pinch
//     draws the outline, release to finish.
//
// Controllers (work alongside hand tracking; only when the game itself has controller bindings,
// or has no input at all):
//   Both grips + both triggers, hold ~1.2 s   edit mode on / lock in edits (saves)
//   In edit mode:
//     Trigger                 = pinch: at a dot/window with the controller tip, or look + trigger
//     Both grips, hold ~0.6 s = new window between the two controller tips (opposite corners)
//     Thumbstick up / down    = selected window deeper / shallower (box depth)
//     No eye tracking: each controller shows a laser. Trigger grabs what it points at; while
//     holding a window with it, thumbstick up / down pushes / pulls it, left / right turns it.
//   Ctrl+Alt+O: hands behind game objects (on by default). Hand cutout pixels are tested against the game's
//   depth (vsSphereD / psHandD), so closer game surfaces cover the hand.
//   Runtimes that turn tracked hands into pretend controllers (Virtual Desktop): while a hand's
//   joints are tracked, that side's controller input is ignored (on SteamVR only when we got
//   XR_EXT_hand_tracking_data_source and asked for real hands only; otherwise its hand
//   joints come from the controllers).
//     Thumbstick press        = next cutout colour
//     B or Y, hold ~0.8 s     = delete the selected window
//
// Hotkeys (Ctrl+Alt + key): E edit mode, P windows on/off, J hand cutouts on/off,
//                           V tint cutouts to see them, X delete all windows,
//                           N new window in front of you, G edit-mode guide panel on/off (saved),
//                           - / = shrink / grow hand cutouts (saved),
//                           [ / ] selected window's box depth -/+ 1 cm (saved),
//                           D experimental D3D12 support on/off (saved, next game launch),
//                           K force alpha on/off, for runtimes that pass alpha on without offering ALPHA_BLEND (saved),
//                           C cutout colour: alpha / green / blue / pink (saved per runtime). A key
//                             colour paints cutouts solid for streamers that chroma-key it to their
//                             passthrough (e.g. Virtual Desktop); boxes still use the game's depth.
// Runtimes without ALPHA_BLEND: the layer stays active (edit mode works) but draws no cutouts
// until a key colour or force alpha is chosen.
//
// Windows are saved per game in %LOCALAPPDATA%\AlphaViewsXR\.

// Graphics: D3D11, D3D12 (Windows) and Vulkan (Windows and Linux, 1.5.0: render_vk.h). On Linux the
// layer is Vulkan only; the guide panel's text is drawn with stb_truetype there (guide_font.h) instead
// of GDI, and Ctrl+Alt hotkeys don't exist (everything is in the headset).
#ifdef __linux__
// The classic scanf / strtoul, not glibc 2.38's C23 versions, so the layer also loads on older distros.
#include <features.h>
#undef __GLIBC_USE_C2X_STRTOL
#define __GLIBC_USE_C2X_STRTOL 0
#undef __GLIBC_USE_C23_STRTOL
#define __GLIBC_USE_C23_STRTOL 0
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#ifdef _WIN32
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <d3d11on12.h>
#define PATH_SEP "\\"
#define LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <cwchar>
typedef uint32_t UINT;
// The guide panel's few GDI types (guide_font.h draws it on Linux).
struct RECT { int left, top, right, bottom; };
typedef uint32_t COLORREF;
#define RGB(r, g, b) ((COLORREF)(((r) & 255) | (((g) & 255) << 8) | (((b) & 255) << 16)))
enum : UINT { DT_LEFT = 0, DT_CENTER = 1, DT_RIGHT = 2, DT_VCENTER = 4, DT_WORDBREAK = 0x10, DT_SINGLELINE = 0x20,
              DT_END_ELLIPSIS = 0x8000 };
#define PATH_SEP "/"
#define LAYER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#include <DirectXMath.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace DirectX;
#ifndef _WIN32
template <size_t N> static void strcpy_s(char (&d)[N], const char* s) { snprintf(d, N, "%s", s); }
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifdef __linux__
// The built-in C++ runtime (static libstdc++, built against a new glibc) asks for these two; our own
// copies (hidden, linux/exports.map) let the layer load on glibc 2.35+ instead of 2.38+.
#include <sys/random.h>
extern "C" unsigned long __isoc23_strtoul(const char* s, char** end, int base) { return strtoul(s, end, base); }
extern "C" uint32_t arc4random(void) {
    uint32_t v = 0;
    if (getrandom(&v, sizeof v, 0) != (ssize_t)sizeof v) { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); v = (uint32_t)t.tv_nsec ^ (uint32_t)getpid(); }
    return v;
}
#endif

#define LAYER_NAME "XR_APILAYER_GMO_alpha_views_xr"

// ================================================================ logging
static std::string g_dir;
static FILE* g_log = nullptr;
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fflush(g_log);
}
static bool FileExists(const std::string& p);
static std::string EnvVar(const char* name);
// Our folder: %LOCALAPPDATA%\AlphaViewsXR (Windows), $XDG_DATA_HOME/AlphaViewsXR or
// ~/.local/share/AlphaViewsXR (Linux).
static void InitDirAndLog() {
    if (g_log) return;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    g_dir = (n ? std::string(buf) : std::string(".")) + "\\AlphaViewsXR";
    CreateDirectoryA(g_dir.c_str(), nullptr);
#else
    const char* x = getenv("XDG_DATA_HOME");
    const char* h = getenv("HOME");
    std::string base = (x && *x) ? std::string(x) : (h && *h) ? std::string(h) + "/.local/share" : std::string(".");
    mkdir(base.c_str(), 0755);
    g_dir = base + "/AlphaViewsXR";
    mkdir(g_dir.c_str(), 0755);
#endif
    // Dev / tests only: ALPHA_VIEWS_DIR=<folder> keeps everything (log, windows, settings) there instead.
    std::string over = EnvVar("ALPHA_VIEWS_DIR");
    if (!over.empty()) g_dir = over;
    g_log = fopen((g_dir + PATH_SEP "log.txt").c_str(), "w");
}

static double Now() {
#ifdef _WIN32
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(f.QuadPart);
#else
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return double(t.tv_sec) + double(t.tv_nsec) * 1e-9;
#endif
}
static bool FileExists(const std::string& p) {
#ifdef _WIN32
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st; return stat(p.c_str(), &st) == 0;
#endif
}
// Environment variable set (and not empty)? Its value, else "".
static std::string EnvVar(const char* name) {
#ifdef _WIN32
    char v[256] = {};
    DWORD n = GetEnvironmentVariableA(name, v, sizeof v);
    return n && n < sizeof v ? std::string(v) : (n ? std::string("1") : std::string());
#else
    const char* v = getenv(name); return v ? std::string(v) : std::string();
#endif
}

// ================================================================ next-layer functions
static PFN_xrGetInstanceProcAddr nGetInstanceProcAddr;
static PFN_xrDestroyInstance nDestroyInstance;
static PFN_xrCreateSession nCreateSession;
static PFN_xrDestroySession nDestroySession;
static PFN_xrCreateSwapchain nCreateSwapchain;
static PFN_xrDestroySwapchain nDestroySwapchain;
static PFN_xrEnumerateSwapchainImages nEnumerateSwapchainImages;
static PFN_xrAcquireSwapchainImage nAcquireSwapchainImage;
static PFN_xrReleaseSwapchainImage nReleaseSwapchainImage;
static PFN_xrLocateViews nLocateViews;
static PFN_xrEndFrame nEndFrame;
static PFN_xrEnumerateEnvironmentBlendModes nEnumerateEnvironmentBlendModes;
static PFN_xrGetSystemProperties nGetSystemProperties;
static PFN_xrCreateHandTrackerEXT nCreateHandTrackerEXT;
static PFN_xrDestroyHandTrackerEXT nDestroyHandTrackerEXT;
static PFN_xrLocateHandJointsEXT nLocateHandJointsEXT;
static PFN_xrStringToPath nStringToPath;
static PFN_xrCreateActionSet nCreateActionSet;
static PFN_xrCreateAction nCreateAction;
static PFN_xrSuggestInteractionProfileBindings nSuggestInteractionProfileBindings;
static PFN_xrAttachSessionActionSets nAttachSessionActionSets;
static PFN_xrSyncActions nSyncActions;
static PFN_xrGetActionStateFloat nGetActionStateFloat;
static PFN_xrCreateActionSpace nCreateActionSpace;
static PFN_xrLocateSpace nLocateSpace;
static PFN_xrDestroySpace nDestroySpace;
static PFN_xrDestroyActionSet nDestroyActionSet;
static PFN_xrPathToString nPathToString;
static PFN_xrGetCurrentInteractionProfile nGetCurrentInteractionProfile;
static PFN_xrGetActionStateBoolean nGetActionStateBoolean;
static PFN_xrGetActionStateVector2f nGetActionStateVector2f;

// ================================================================ vector helpers
static XMVECTOR V(const XrVector3f& v) { return XMVectorSet(v.x, v.y, v.z, 0); }
static XMVECTOR V(const XMFLOAT3& v) { return XMLoadFloat3(&v); }
static XMVECTOR Q(const XrQuaternionf& q) { return XMVectorSet(q.x, q.y, q.z, q.w); }
static float Dist(XMVECTOR a, XMVECTOR b) { return XMVectorGetX(XMVector3Length(a - b)); }

// ================================================================ windows
// Corner order (local x, y signs): 0 (-,-) bottom-left, 1 (+,-), 2 (+,+), 3 (-,+).
static const float kCS[4][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
static const float kRadiusHandleInset = 0.045f;  // radius dot sits this far inside the corner
static const float kCornerHandleR = 0.016f, kCornerHit = 0.04f;   // visible size / grab distance
static const float kRadiusHandleR = 0.012f, kRadiusHit = 0.03f;
static const float kCenterHandleR = 0.016f, kCenterHit = 0.04f;   // centre grab dot
static const float kArcK = 1.0f - 0.70710678f;    // corner-to-arc-midpoint factor

struct Window {
    XMFLOAT3 pos{ 0, -0.3f, -0.5f };
    XMFLOAT4 rot{ 0, 0, 0, 1 };   // local +Z faces the driver
    float hw = 0.2f, hh = 0.15f;  // half width / height, metres
    float radius[4] = {};
    float hd = 0;                 // half depth: 0 = flat window, > 0 = box from -hd to +hd along local z
    // Free shape (1.4.0): outline points in local x, y, counter-clockwise. Empty = rounded rectangle.
    // For shapes, hw / hh are the outline's half extents (it's kept centred on the window).
    std::vector<XMFLOAT2> pts;
    // Keep box (1.4.0): the inverse of a box. The window shows passthrough, but the game's own
    // surfaces inside the box (or in front of it) stay, so a character standing in it stays visible.
    bool keep = false;
    // Circle (1.4.0): an ellipse filling the window's width and height (a circle when square).
    // Not for free shapes. Corners, move, depth and keep all still work.
    bool ellipse = false;
    // Lock (1.4.3): moving the window never changes its angle (no tilting while you push it about).
    // Only the snap button turns a locked window.
    bool lock = false;
    // Box faces (1.5.0, as in Alpha Views VR): which faces of a box are passthrough. 0 the whole box
    // (as before), 1 top + far wall, 2 far wall + floor, 3 all but the front and the floor. In modes
    // 1-3 the faces are walls: the game's surfaces in front of them (or inside the box) stay.
    int boxMode = 0;
};
static const char* kBoxModeName[4] = { "whole box", "top and far wall", "far wall and floor", "all but front and floor" };
static const wchar_t* kBoxModeNameW[4] = { L"whole box", L"top + far wall", L"far wall + floor", L"all but front + floor" };
static const float kBoxModeCol[4][3] = { { 0.1f, 0.55f, 0.25f }, { 0.95f, 0.55f, 0.1f }, { 0.1f, 0.6f, 0.9f }, { 0.6f, 0.3f, 0.95f } };
// Box half depth limit: big enough for any box you'd want (a whole room or more), small enough to keep
// the numbers sane. (Was 0.5 m, i.e. 1 m of total depth, before 1.4.3.)
static const float kMaxHalfDepth = 50.0f;
static const int kMaxShapePts = 32;   // must match the shader's pts[16] (two points per float4)
static bool IsShape(const Window& w) { return w.pts.size() >= 3; }
static const float kDepthHandleR = 0.014f, kDepthHit = 0.035f;

static XMMATRIX WinMatrix(const Window& w) {
    return XMMatrixRotationQuaternion(XMLoadFloat4(&w.rot)) * XMMatrixTranslation(w.pos.x, w.pos.y, w.pos.z);
}
static XMVECTOR WinToWorld(const Window& w, float x, float y, float z = 0) {
    return XMVector3TransformCoord(XMVectorSet(x, y, z, 1), WinMatrix(w));
}
// Depth handle: outside the right edge, on the window's centre line (the middle of the box, where every
// other handle is, so it stays put however deep the box gets). Far enough out to keep clear of the corner
// handles, even on a short window.
static const float kDepthOut = 0.075f;
// Edit-mode handles grow with distance (FarScale, up to 3x), so far windows' handles stay visible. The
// handles beside and above a window spread out by the same factor, or on a far window the bigger
// discs would crowd together and overlap. Spread: that factor at the window's right / top edge.
static float FarScale(XMVECTOR p);
static float SpreadSide(const Window& w) { return FarScale(WinToWorld(w, w.hw, 0, 0)); }
static float SpreadTop(const Window& w) { return FarScale(WinToWorld(w, 0, w.hh, 0)); }
static float DepthOut(const Window& w) { return kDepthOut * SpreadSide(w); }
static XMVECTOR DepthHandle(const Window& w) { return WinToWorld(w, w.hw + DepthOut(w), 0, 0); }
static XMFLOAT3 WorldToWin(const Window& w, XMVECTOR p) {
    XMFLOAT3 r; XMStoreFloat3(&r, XMVector3TransformCoord(p, XMMatrixInverse(nullptr, WinMatrix(w))));
    return r;
}
static void ClampRadii(Window& w) {
    float m = std::min(w.hw, w.hh);
    for (float& r : w.radius) r = std::clamp(r, 0.0f, m);
}
static XMFLOAT2 RadiusHandleLocal(const Window& w, int k) {
    float d = w.radius[k] * kArcK + kRadiusHandleInset;
    return { kCS[k][0] * (w.hw - d), kCS[k][1] * (w.hh - d) };
}
// Outline of the window, counter-clockwise, local 2D: the shape's points, or the rounded rectangle.
static void Perimeter(const Window& w, std::vector<XMFLOAT2>& out) {
    out.clear();
    if (IsShape(w)) { out = w.pts; return; }
    if (w.ellipse) {
        for (int i = 0; i < 48; ++i) {
            float a = XM_2PI * i / 48;
            out.push_back({ cosf(a) * w.hw, sinf(a) * w.hh });
        }
        return;
    }
    const float startDeg[4] = { 180, 270, 0, 90 };
    for (int k = 0; k < 4; ++k) {
        float r = w.radius[k];
        float cx = kCS[k][0] * (w.hw - r), cy = kCS[k][1] * (w.hh - r);
        int segs = r > 0.001f ? 10 : 0;
        for (int i = 0; i <= segs; ++i) {
            float a = XMConvertToRadians(startDeg[k] + 90.0f * (segs ? float(i) / segs : 0.5f));
            out.push_back({ cx + cosf(a) * r, cy + sinf(a) * r });
            if (!segs) break;
        }
    }
}

// ---------------------------------------------------------------- free shapes
static float SignedArea(const std::vector<XMFLOAT2>& p) {
    float a = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += p[j].x * p[i].y - p[i].x * p[j].y;
    return a * 0.5f;
}
// Shape half extents from its points (the points stay where they are).
static void UpdateShapeBounds(Window& w) {
    float mx = 0.005f, my = 0.005f;
    for (auto& p : w.pts) { mx = std::max(mx, fabsf(p.x)); my = std::max(my, fabsf(p.y)); }
    w.hw = mx; w.hh = my;
}
// Move the window's centre to the middle of its points, so the dots and box stay centred.
static void RecentreShape(Window& w) {
    if (!IsShape(w)) return;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (auto& p : w.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    XMStoreFloat3(&w.pos, WinToWorld(w, cx, cy));
    for (auto& p : w.pts) { p.x -= cx; p.y -= cy; }
    if (SignedArea(w.pts) < 0) std::reverse(w.pts.begin(), w.pts.end());
    UpdateShapeBounds(w);
}
static float SegDist(XMFLOAT2 p, XMFLOAT2 a, XMFLOAT2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-12f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0;
    float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return sqrtf(ex * ex + ey * ey);
}
// Douglas-Peucker on a closed outline: drop points that sit within tol of the line between
// their kept neighbours. Raises tol until at most maxN points remain.
static std::vector<XMFLOAT2> SimplifyClosed(const std::vector<XMFLOAT2>& in, size_t maxN) {
    if (in.size() <= 3) return in;
    // Split at the point farthest from point 0 so both halves are open polylines.
    size_t farIdx = 0; float best = -1;
    for (size_t i = 1; i < in.size(); ++i) {
        float dx = in[i].x - in[0].x, dy = in[i].y - in[0].y, d = dx * dx + dy * dy;
        if (d > best) { best = d; farIdx = i; }
    }
    for (float tol = 0.002f; ; tol *= 1.4f) {
        std::vector<bool> keep(in.size(), false);
        keep[0] = keep[farIdx] = true;
        auto run = [&](size_t a, size_t b) {   // a..b along the outline (wrapping), endpoints kept
            std::vector<std::pair<size_t, size_t>> st{ { a, b } };
            while (!st.empty()) {
                auto [s, e] = st.back(); st.pop_back();
                size_t n = (e + in.size() - s) % in.size();
                if (n < 2) continue;
                float md = -1; size_t mi = s;
                for (size_t k = 1; k < n; ++k) {
                    size_t i = (s + k) % in.size();
                    float d = SegDist(in[i], in[s], in[e]);
                    if (d > md) { md = d; mi = i; }
                }
                if (md > tol) { keep[mi] = true; st.push_back({ s, mi }); st.push_back({ mi, e }); }
            }
        };
        run(0, farIdx); run(farIdx, 0);
        std::vector<XMFLOAT2> out;
        for (size_t i = 0; i < in.size(); ++i) if (keep[i]) out.push_back(in[i]);
        if (out.size() <= maxN || tol > 1.0f) return out;
    }
}
static bool SegHit(XMFLOAT2 a, XMFLOAT2 b, XMFLOAT2 c, XMFLOAT2 d, XMFLOAT2& at) {
    float rx = b.x - a.x, ry = b.y - a.y, sx = d.x - c.x, sy = d.y - c.y;
    float den = rx * sy - ry * sx;
    if (fabsf(den) < 1e-12f) return false;
    float t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / den, u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / den;
    if (t < 0 || t > 1 || u < 0 || u > 1) return false;   // touching counts (callers skip neighbours)
    at = { a.x + rx * t, a.y + ry * t };
    return true;
}
// Tracing closes on the yellow start dot: come back within this distance of where you began
// (after going at least kTraceAway from it) and the outline finishes there.
static const float kTraceClose = 0.03f, kTraceAway = 0.06f;
static bool TraceCanClose(const std::vector<XMFLOAT2>& path) {
    if (path.size() < 8) return false;
    float far2 = 0;
    for (auto& q : path) far2 = std::max(far2, (q.x - path[0].x) * (q.x - path[0].x) + (q.y - path[0].y) * (q.y - path[0].y));
    return far2 > kTraceAway * kTraceAway;
}
static bool TraceAtStart(const std::vector<XMFLOAT2>& path) {
    if (!TraceCanClose(path)) return false;
    float dx = path.back().x - path[0].x, dy = path.back().y - path[0].y;
    return dx * dx + dy * dy < kTraceClose * kTraceClose;
}
// A traced outline. Back at the start dot: the whole path. Otherwise, if the path crosses itself,
// the biggest closed loop it makes (so a wobble near the start can't win over the real outline),
// unless that loop is much smaller than the whole path, which is then closed straight across.
// Then thin it to kMaxShapePts points.
static std::vector<XMFLOAT2> TraceToShape(const std::vector<XMFLOAT2>& path) {
    std::vector<XMFLOAT2> loop = path;
    if (!TraceAtStart(path)) {
        std::vector<XMFLOAT2> best;
        float bestA = 0;
        for (size_t i = 2; i < path.size(); ++i)
            for (size_t j = 0; j + 2 < i; ++j) {
                XMFLOAT2 x;
                if (!SegHit(path[j], path[j + 1], path[i - 1], path[i], x)) continue;
                std::vector<XMFLOAT2> l(path.begin() + j + 1, path.begin() + i);
                l.insert(l.begin(), x);
                float a = fabsf(SignedArea(l));
                if (l.size() >= 3 && a > bestA) { bestA = a; best = std::move(l); }
            }
        if (!best.empty() && bestA >= 0.5f * fabsf(SignedArea(path))) loop = std::move(best);
    }
    if (loop.size() < 3) return {};
    std::vector<XMFLOAT2> s = SimplifyClosed(loop, kMaxShapePts);
    if (s.size() < 3 || fabsf(SignedArea(s)) < 0.0004f) return {};   // under ~2 x 2 cm: ignore
    return s;
}
// Ear clipping: triangles (index triples) covering a simple outline of any shape, convex or not.
// Outlines that cross themselves still get triangles, just not always exactly the right ones.
static void Triangulate(const std::vector<XMFLOAT2>& p, std::vector<int>& tris) {
    tris.clear();
    std::vector<int> idx(p.size());
    for (size_t i = 0; i < p.size(); ++i) idx[i] = (int)i;
    if (SignedArea(p) < 0) std::reverse(idx.begin(), idx.end());
    auto cross = [&](int a, int b, int c) {
        return (p[b].x - p[a].x) * (p[c].y - p[a].y) - (p[b].y - p[a].y) * (p[c].x - p[a].x);
    };
    size_t guard = 0;
    while (idx.size() > 3 && guard++ < 4 * p.size()) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cross(a, b, c) <= 1e-9f) continue;   // reflex corner
            bool empty = true;
            for (int q : idx) {
                if (q == a || q == b || q == c) continue;
                if (cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0) { empty = false; break; }
            }
            if (!empty) continue;
            tris.insert(tris.end(), { a, b, c });
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {   // crossed outline: no clean ear left, clip one anyway
            tris.insert(tris.end(), { idx[idx.size() - 1], idx[0], idx[1] });
            idx.erase(idx.begin());
        }
    }
    if (idx.size() == 3) tris.insert(tris.end(), { idx[0], idx[1], idx[2] });
}
// Rectangle -> shape: start from its outline (rounded corners thinned to fit kMaxShapePts).
static void MakeShape(Window& w) {
    std::vector<XMFLOAT2> per;
    Perimeter(w, per);
    w.pts = SimplifyClosed(per, kMaxShapePts);
    if (w.pts.size() < 3) w.pts = { { -w.hw, -w.hh }, { w.hw, -w.hh }, { w.hw, w.hh }, { -w.hw, w.hh } };
    w.ellipse = false;
    RecentreShape(w);
}
// Shape -> rectangle: the shape's bounding box, square corners.
static void MakeRect(Window& w) {
    RecentreShape(w);
    w.pts.clear();
    w.ellipse = false;
    for (float& r : w.radius) r = 0;
}

// ================================================================ state
#ifdef _WIN32
// Copy of one slice of a game depth image that our shader can read.
struct DepthCopy { ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; bool failed = false; };
#endif
// A Vulkan game's swapchain (render_vk.h).
struct VkSwapchainData {
    std::vector<VkImage> images;
    uint32_t width = 0, height = 0, layers = 1, samples = 1;
    bool sampled = false;                      // depth: created with the SAMPLED usage bit (we added it)
    bool failed = false;                       // depth: can't be read (logged once)
    std::map<uint64_t, VkImageView> views;     // key: image index << 32 | array slice
    std::map<uint64_t, VkFramebuffer> fbs;     // colour: same key
};

struct SwapchainInfo {
    int64_t format = 0;                        // DXGI_FORMAT (D3D) or VkFormat (Vulkan)
#ifdef _WIN32
    std::vector<ID3D11Texture2D*> images;   // D3D12 games: our wrapped copies (we own them)
    std::map<uint64_t, ID3D11RenderTargetView*> rtvs; // key: image index << 32 | array slice
    std::map<uint32_t, DepthCopy> depthCopies; // key: array slice
#endif
    uint32_t lastAcquired = 0;
    bool depth = false;                       // a game depth swapchain (read only)
    VkSwapchainData vk;
};

// Sphere instance (hand cutouts), or an edit-mode handle disc (vsIcon / psIcon in shaders.hlsl).
// Handles: ax / bx = the disc's right / up (zero: faces the eye), icon = ICON_*, g = symbol colour
// (ga 0 = automatic: white, or dark on light discs).
struct Inst {
    float x, y, z, r, cr, cg, cb, ca;
    float ax = 0, ay = 0, az = 0, icon = 0;
    float bx = 0, by = 0, bz = 0, pad = 0;
    float gr = 0, gg = 0, gb = 0, ga = 0;
};
enum IconId { ICON_DOT, ICON_MOVE, ICON_RESIZE, ICON_ROUND, ICON_DEPTH, ICON_KEEP, ICON_TO_SHAPE, ICON_TO_RECT,
              ICON_DRAW, ICON_POINT, ICON_ADD, ICON_TO_CIRCLE, ICON_LOCK, ICON_UNLOCK, ICON_SNAP, ICON_FACES };

// CPU copy of Glyph() in shaders.hlsl, for the guide panel's legend. Keep them in step.
namespace glyph {
struct P { float x, y; };
static P operator-(P a, P b) { return { a.x - b.x, a.y - b.y }; }
static P operator+(P a, P b) { return { a.x + b.x, a.y + b.y }; }
static P operator*(P a, float s) { return { a.x * s, a.y * s }; }
static float Dot(P a, P b) { return a.x * b.x + a.y * b.y; }
static float Len(P a) { return sqrtf(Dot(a, a)); }
static P Norm(P a) { float l = Len(a); return { a.x / l, a.y / l }; }
static float Seg(P p, P a, P b) { P pa = p - a, ba = b - a; return Len(pa - ba * std::clamp(Dot(pa, ba) / Dot(ba, ba), 0.0f, 1.0f)); }
static float Chevron(P p, P tip, P dir, float h) {
    P n{ -dir.y, dir.x };
    return std::min(Seg(p, tip, tip - (dir - n) * h), Seg(p, tip, tip - (dir + n) * h));
}
static float Arrow2(P p, P a, P b, float h) {
    P d = Norm(b - a);
    return std::min(Seg(p, a, b), std::min(Chevron(p, b, d, h), Chevron(p, a, d * -1, h)));
}
static float Box(P p, P c, P h) {
    float dx = fabsf(p.x - c.x) - h.x, dy = fabsf(p.y - c.y) - h.y;
    return Len({ std::max(dx, 0.0f), std::max(dy, 0.0f) }) + std::min(std::max(dx, dy), 0.0f);
}
static float Side(P p, P a, P b) { P e = Norm(b - a), v = p - a; return v.x * e.y - v.y * e.x; }
static float Glyph(int id, P p) {
    using std::min; using std::max;
    if (id == ICON_MOVE) return min(Arrow2(p, { -0.6f, 0 }, { 0.6f, 0 }, 0.2f), Arrow2(p, { 0, -0.6f }, { 0, 0.6f }, 0.2f)) - 0.075f;
    if (id == ICON_RESIZE) return Arrow2(p, { -0.55f, 0 }, { 0.55f, 0 }, 0.24f) - 0.085f;
    if (id == ICON_ROUND) {
        P q = p - P{ -0.04f, -0.04f };
        float arc = (q.x > 0 && q.y > 0) ? fabsf(Len(q) - 0.44f) : 1e3f;
        return min(arc, min(Seg(p, { 0.4f, -0.5f }, { 0.4f, -0.04f }), Seg(p, { -0.04f, 0.4f }, { -0.5f, 0.4f }))) - 0.085f;
    }
    if (id == ICON_DEPTH) {
        float d = fabsf(Box(p, { -0.12f, -0.12f }, { 0.3f, 0.3f }));
        d = min(d, Seg(p, { -0.18f, 0.42f }, { 0.42f, 0.42f }));
        d = min(d, Seg(p, { 0.42f, -0.18f }, { 0.42f, 0.42f }));
        d = min(d, Seg(p, { -0.42f, 0.18f }, { -0.18f, 0.42f }));
        d = min(d, Seg(p, { 0.18f, 0.18f }, { 0.42f, 0.42f }));
        d = min(d, Seg(p, { 0.18f, -0.42f }, { 0.42f, -0.18f }));
        return d - 0.065f;
    }
    if (id == ICON_KEEP) {
        P A{ -0.42f, -0.42f }, B{ 0.18f, -0.42f }, C{ 0.42f, -0.18f }, D{ 0.42f, 0.42f }, E{ -0.18f, 0.42f }, F{ -0.42f, 0.18f };
        float hex = max(max(max(Side(p, A, B), Side(p, B, C)), max(Side(p, C, D), Side(p, D, E))), max(Side(p, E, F), Side(p, F, A)));
        P K{ 0.18f, 0.18f };
        float lines = min(min(Seg(p, F, K), Seg(p, K, B)), Seg(p, K, D)) - 0.045f;
        return max(hex - 0.03f, -lines);
    }
    if (id == ICON_TO_SHAPE) {
        P Q[5] = { { -0.52f, -0.28f }, { 0.12f, -0.52f }, { 0.52f, 0.02f }, { 0.08f, 0.5f }, { -0.46f, 0.3f } };
        float d = 1e3f, dots = 1e3f;
        for (int i = 0; i < 5; ++i) { d = min(d, Seg(p, Q[i], Q[(i + 1) % 5])); dots = min(dots, Len(p - Q[i])); }
        return min(d - 0.055f, dots - 0.13f);
    }
    if (id == ICON_TO_RECT) return fabsf(Box(p, { 0, 0 }, { 0.36f, 0.2f }) - 0.16f) - 0.07f;
    if (id == ICON_DRAW) {
        P r{ (p.x + p.y) * 0.70710678f, (p.y - p.x) * 0.70710678f };
        float body = Box(r, { 0.2f, 0 }, { 0.36f, 0.15f });
        P A{ -0.58f, 0 }, B{ -0.16f, -0.15f }, C{ -0.16f, 0.15f };
        float tip = max(max(Side(r, A, B), Side(r, B, C)), Side(r, C, A));
        float gaps = min(fabsf(r.x + 0.12f), fabsf(r.x - 0.36f)) - 0.03f;
        return max(min(body, tip), -gaps);
    }
    if (id == ICON_POINT) return Len(p) - 0.32f;
    if (id == ICON_ADD) return min(Seg(p, { -0.5f, 0 }, { 0.5f, 0 }), Seg(p, { 0, -0.5f }, { 0, 0.5f })) - 0.12f;
    if (id == ICON_TO_CIRCLE) return fabsf(Len(p) - 0.48f) - 0.08f;
    if (id == ICON_LOCK || id == ICON_UNLOCK) {
        // Padlock: body with a keyhole, shackle closed (lock) or lifted open on the right (unlock).
        float body = max(Box(p, { 0, -0.2f }, { 0.36f, 0.27f }), -(Len(p - P{ 0, -0.17f }) - 0.08f));
        P c{ 0, id == ICON_LOCK ? 0.07f : 0.2f };
        P q = p - c;
        float arc = q.y >= 0 ? fabsf(Len(q) - 0.22f) : 1e3f;
        float legs = Seg(p, { -0.22f, c.y }, { -0.22f, 0.07f });
        if (id == ICON_LOCK) legs = min(legs, Seg(p, { 0.22f, c.y }, { 0.22f, 0.07f }));
        return min(body, min(arc, legs) - 0.07f);
    }
    if (id == ICON_SNAP) {
        // Upright: a post standing square on a base line, with a right-angle mark.
        float d = min(Seg(p, { -0.5f, -0.4f }, { 0.5f, -0.4f }), Seg(p, { 0, -0.4f }, { 0, 0.52f }));
        d = min(d, min(Seg(p, { 0.2f, -0.4f }, { 0.2f, -0.2f }), Seg(p, { 0.2f, -0.2f }, { 0, -0.2f })));
        return d - 0.07f;
    }
    if (id == ICON_FACES) {
        // Box faces: a box with a solid far wall behind its open front.
        float front = Box(p, { -0.12f, -0.12f }, { 0.3f, 0.3f });
        float back = max(Box(p, { 0.12f, 0.12f }, { 0.3f, 0.3f }), -(front - 0.03f));
        float d = fabsf(front) - 0.065f;
        d = min(d, Seg(p, { -0.42f, 0.18f }, { -0.18f, 0.42f }) - 0.065f);
        d = min(d, Seg(p, { 0.18f, -0.42f }, { 0.42f, -0.18f }) - 0.065f);
        return min(d, back);
    }
    return 1e3f;
}
}  // namespace glyph

// GRAB_POINT: a shape point (corner = its index). GRAB_ADDPT: a shape edge's middle dot (becomes a
// new point). GRAB_TYPE / GRAB_DRAWBTN: the buttons above the window (act on pinch).
// GRAB_TRACE: drawing a new outline. GRAB_KEEPBTN: the keep-box button next to the depth dot.
enum GrabMode { GRAB_NONE, GRAB_MOVE, GRAB_CORNER, GRAB_RADIUS, GRAB_DEPTH,
                GRAB_POINT, GRAB_ADDPT, GRAB_TYPE, GRAB_DRAWBTN, GRAB_TRACE, GRAB_KEEPBTN, GRAB_CIRCLEBTN,
                GRAB_LOCKBTN,    // lock button above the window: angle locked while moving, on / off
                GRAB_SNAPBTN,    // snap button above the window: stand it upright / lay it level (cycles)
                GRAB_GUIDE,      // the guide panel: move it
                GRAB_ALL,        // the guide's "move all windows" button: all windows move together
                GRAB_PRESET,     // a guide preset button (corner = preset 0..2): tap loads, hold saves
                GRAB_FACESBTN }; // box faces button, past the keep button (boxes only): next face mode

// What a ray (eye gaze or controller laser) is on: a dot of the selected window, or a window.
struct GazeTarget { GrabMode mode = GRAB_NONE; int win = -1, corner = -1; };

// One "pointer": a tracked hand (g_hand[0..1]) or a controller (g_hand[2..3]).
// Grabs work the same for both: pinchPt is the grab point, grabRot its orientation.
struct HandState {
    XrHandTrackerEXT tracker = XR_NULL_HANDLE;
    bool isCtl = false;
    bool active = false;
    XrHandJointLocationEXT j[XR_HAND_JOINT_COUNT_EXT]{};
    XMFLOAT4 grabRot{ 0, 0, 0, 1 };
    // controller buttons
    float trigger = 0, squeeze = 0, stickX = 0, stickY = 0;
    bool stickSeen = false;           // logged the first thumbstick movement
    bool trigSeen = false, gripSeen = false;
    XrPath profile = XR_NULL_PATH;    // current interaction profile (logged when it changes)
    double profileCheck = -100;
    bool rawActive = false;           // controller pose + trigger active, before ignoring emulated ones
    int jointSource = 0;              // hand joints from: 0 = unknown, 1 = tracked hand, 2 = a controller
    // laser (controllers, no eye tracking): aim ray, what it points at, how far
    XMFLOAT3 aimO{}, aimD{ 0, 0, -1 };
    GazeTarget ray;
    float rayLen = 0;
    bool laser = false;               // this grab was made with the laser
    float grabDist = 0;               // laser grab: distance along the ray to the grabbed point
    XMFLOAT3 laserStart{};            // laser grab: the grabbed point at pinch start
    XMFLOAT3 dragOffset{};            // handle grab: handle minus hand (or ray) point when dragging began
    float push = 0, turn = 0;         // laser window grab: thumbstick push / pull (m) and turn (radians)
    bool emuIgnoredLogged = false;
    bool stickClick = false, stickClickWas = false;   // thumbstick press: next cutout colour
    bool button = false;              // B / Y (or menu)
    double buttonSince = -1;
    bool buttonLatched = false;
    // pinch
    bool pinching = false;
    XMFLOAT3 pinchPt{};
    double pinchStart = 0;
    XMFLOAT3 pinchStartPt{};
    XMFLOAT3 pinchAnchor{};           // hands: pinch point in palm space, fixed while pinching
    bool dragging = false;            // hands: moved past the small dead zone since the pinch began
    // grab
    GrabMode mode = GRAB_NONE;
    int win = -1, corner = -1;
    bool newPoint = false;   // GRAB_POINT made from an edge dot: a tap keeps it (doesn't delete it)
    XMFLOAT4X4 rel{};   // window relative to hand (direct move)
    // indirect (eye-targeted) grab: hand motion since pinch start drives the target
    bool indirect = false;
    float gain = 1;
    XMFLOAT3 targetStart{};          // grabbed dot's world position at pinch start
    XMFLOAT3 winStartPos{};
    XMFLOAT4 winStartRot{}, handStartRot{};
    float winStartHw = 0, winStartHh = 0, winStartHd = 0;   // size when the grab / drag began
    // gestures
    double thumbsUpSince = -1, thumbsDownSince = -1;
    bool thumbsLatched = false, thumbsDownLatched = false;
    double vSince = -1;      // V sign (edit mode): next cutout colour
    bool vLatched = false;
    bool sysPinchLogged = false;
};

// System pinch via XR_EXT_hand_interaction actions. Our action set is added to the game's
// xrAttachSessionActionSets / xrSyncActions calls. If the game never attaches any actions
// (like the test app), we attach and sync our own set after a few seconds.
struct Actions {
    XrActionSet set = XR_NULL_HANDLE;
    XrAction pinchValue = XR_NULL_HANDLE, pinchPose = XR_NULL_HANDLE;
    XrAction eyeGaze = XR_NULL_HANDLE;   // XR_EXT_eye_gaze_interaction
    XrPath hand[2] = {};
    XrSpace space[2] = {};
    XrSpace eyeSpace = XR_NULL_HANDLE;
    bool attached = false, selfAttached = false;
    int frames = 0;
};

// Controller input for edit mode. Created with the instance, because games suggest their
// bindings before they create a session. Our bindings are only ADDED to the game's own
// suggestion for the same controller (a separate suggestion for a profile would replace the
// game's). Apps without any input get our own suggestions instead.
struct CtlActions {
    XrActionSet set = XR_NULL_HANDLE;
    XrAction pose = XR_NULL_HANDLE, trigger = XR_NULL_HANDLE, squeeze = XR_NULL_HANDLE;
    XrAction button = XR_NULL_HANDLE, stick = XR_NULL_HANDLE, stickClick = XR_NULL_HANDLE;
    XrPath hand[2] = {};
    XrSpace space[2] = {};
    bool merged = false;      // our bindings went in with at least one game suggestion
    bool attached = false;
};
static CtlActions g_ctl;

static std::mutex g_mutex;
static std::string g_appName = "unknown";
static XrSession g_session = XR_NULL_HANDLE;
static bool g_alphaSupported = false;
static std::map<XrSwapchain, SwapchainInfo> g_swapchains;

static std::vector<Window> g_windows;
static bool g_windowsEnabled = true;
static bool g_handsEnabled = true;
static bool g_edit = false;
static bool g_visualize = false;
static bool g_handExtEnabled = false;
static bool g_handInteractionEnabled = false;
static bool g_handSourceEnabled = false;   // XR_EXT_hand_tracking_data_source: hand-tracked or controller-made joints
static bool g_eyeExtEnabled = false;
static XrInstance g_instance = XR_NULL_HANDLE;

// Eye gaze (edit mode): what you're looking at. Pinch anywhere grabs it.
static bool g_gazeValid = false;
static double g_gazeLastValid = -100;   // when eye gaze was last valid (blinks drop it briefly)
static XMFLOAT3 g_gazeOrigin{}, g_gazeDir{};
static GazeTarget g_gazeTarget;
static bool g_gazeLogged = false;
static XMVECTOR g_head = XMVectorZero();
// Edit-mode markers grow with distance past 80 cm, so they look the same size out to 8 m (10x; was 3x,
// i.e. 2.4 m, before boxes could be big): far windows' handles stay easy to see and aim at.
static float FarScale(XMVECTOR p) { return std::clamp(XMVectorGetX(XMVector3Length(p - g_head)) / 0.8f, 1.0f, 10.0f); }
static Actions g_act;
static const int kMaxWindows = 32;    // per game: plenty, and guards against a damaged file loading thousands
static double g_windowsFullUntil = 0; // tried to make one too many: say so on the guide panel until then
static int g_selected = -1;          // selected window in edit mode (sticks until another is picked)
static int g_drawArmed = -1;         // shape window whose draw button was tapped: the next pinch traces its outline
static std::vector<XMFLOAT2> g_trace;  // outline being traced (window local), while GRAB_TRACE
static Window g_tracePlane;             // plane the outline is traced on (pos + rot), facing you
static XMFLOAT2 g_tracePrev{};          // last frame's trace sample (to drop sudden jumps)
static bool g_tracePrevOk = false;
// A plane through p facing the head: its front (+z) points at you, its up stays upright.
static Window TracePlane(XMVECTOR p) {
    Window t;
    XMVECTOR z = g_head - p;
    if (XMVectorGetX(XMVector3Length(z)) < 0.05f) z = XMVectorSet(0, 0, 1, 0);
    z = XMVector3Normalize(z);
    if (fabsf(XMVectorGetY(z)) > 0.95f) z = XMVector3Normalize(XMVectorSetY(z, 0) + XMVectorSet(0, 0, 0.001f, 0));
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), z));
    XMVECTOR up = XMVector3Cross(z, right);
    XMMATRIX m(right, up, z, XMVectorSet(0, 0, 0, 1));
    XMStoreFloat3(&t.pos, p);
    XMStoreFloat4(&t.rot, XMQuaternionNormalize(XMQuaternionRotationMatrix(m)));
    return t;
}
static float g_handScale = 1.0f;     // hand cutout size vs. typical real joint size
static bool g_d3d12Enabled = false;  // experimental D3D12 support (Ctrl+Alt+D, saved, next launch)
static std::vector<XrView> g_endViews;          // last submitted views (pose, fov) and their space
static XrSpace g_endSpace = XR_NULL_HANDLE;
static bool g_spawnAhead = false;    // Ctrl+Alt+N: new window in front of the head, made in the next UpdateFrame
static bool g_forceAlpha = false;    // set layer alpha flags without ALPHA_BLEND (Ctrl+Alt+K, saved)
// How cutouts show the real world. 0 = alpha: transparent, the runtime's own passthrough shows through
// (needs ALPHA_BLEND or force alpha). 1-3 = key colour: cutouts are painted solid green / blue / pink so a
// streamer that chroma-keys (e.g. Virtual Desktop) swaps that colour for its passthrough.
// Ctrl+Alt+C cycles it. Saved per runtime, so PICO can stay on alpha while Virtual Desktop uses a colour.
static int g_keyMode = 0;
static const float kKeyCol[4][3] = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 } };
static const wchar_t* kKeyName[4] = { L"alpha", L"green", L"blue", L"pink" };
static bool UseKeyColour();
static bool CutoutsShown();
static const int kGuideW = 2300, kGuideH = 1340;
static const float kGuideHalfW = 0.4100f, kGuideHalfH = 0.2389f;   // same aspect as the image
static const int kGuideHeader = 110;                              // title bar height (pixels)
static const RECT kMoveAllBtn = { 560, 12, 1160, 98 };            // "move all windows" button (pixels)
// Preset buttons 1-3 beside it: tap to load that saved layout, hold to save the current one there.
static RECT PresetBtn(int i) { return { 1196 + i * 152, 12, 1196 + i * 152 + 140, 98 }; }
static const double kPresetHoldSave = 1.5;   // seconds held to save
static bool g_presetHas[3] = {};             // preset file exists for this game
static int g_presetHover = -1;               // preset button pointed at (-1 none)
static int g_presetHeld = -1;                // preset button being held (-1 none)
static double g_presetHoldStart = 0;
static int g_presetStep = 0;                 // hold progress 0..10 (10 = saved)
static bool g_presetHoldSaved = false;       // this hold already saved
static int g_presetFlash = -1;               // preset just saved / loaded: show it for a moment
static bool g_presetFlashSaved = false;
static double g_presetFlashUntil = 0;
static std::vector<uint32_t> g_guidePixels;   // BGRA, top-down, kGuideW x kGuideH
static bool g_guideDirty = false;             // pixels changed, upload in DrawMask
static bool g_guideUploaded = false;          // the texture holds a rendered guide
static std::wstring g_guideText;              // what the pixels show (to skip redundant redraws)
static double g_guideNextText = 0;
static bool g_guideShow = false, g_guidePlaced = false;
static XMFLOAT3 g_guidePos{}, g_guideTarget{};
static XMFLOAT3 g_guideCorner[4];             // world corners: top-left, top-right, bottom-left, bottom-right
static XMFLOAT3 g_guideRight{ 1, 0, 0 }, g_guideUp{ 0, 1, 0 }, g_guideN{ 0, 0, 1 };   // panel axes (N faces you)
static bool g_guidePinned = false;            // moved by hand: stays put until edit mode ends
static int g_guideBtnState = 0;               // move-all button: 0 idle, 1 pointed at, 2 held
static std::vector<std::pair<XMFLOAT3, XMFLOAT4>> g_allStart;   // move all: windows at the start
static bool g_guideOn = true;        // Ctrl+Alt+G: guide panel while editing (saved)
// Game sends depth: -1 not known yet, 0 no, 1 yes,
// 2 game made a depth image but doesn't send it with its frames (UEVR: "Enable Depth" off),
// 3 game sends depth, but only on a later projection layer than the one cutouts are drawn on.
static int g_gameDepth = -1;
static bool g_gameMadeDepth = false;  // game created a depth swapchain this session
static int g_noDepthFrames = 0;       // frames seen without depth (wait a bit before saying "no")
static float g_gameNearZ = 0, g_gameFarZ = 0;
static int g_depthRead = -1;         // box shader got the game's depth: -1 not tried, 0 no, 1 yes
// Start depth detection over (new session: games like UEVR restart their session several times).
static void ResetDepthStatus() {
    g_gameDepth = -1; g_gameMadeDepth = false; g_noDepthFrames = 0; g_depthRead = -1;
}
static std::string g_runtimeName = "unknown";
static HandState g_hand[4];   // 0 left hand, 1 right hand, 2 left controller, 3 right controller
static double g_ctlComboSince = -1, g_ctlFrameSince = -1;
static bool g_ctlComboLatched = false, g_ctlFrameLatched = false;
static double g_frameSince = -1;
static double g_praySince = -1;
static bool g_prayLatched = false;
static bool g_frameLatched = false;
static double g_lastUpdate = 0;

static std::vector<Inst> g_maskInst;   // hand cutout spheres
static std::vector<Inst> g_uiInst;     // edit-mode handles, outline, gesture feedback
static std::vector<XMFLOAT3> g_winTris; // window cutout triangles (world): flat windows first, then boxes
static UINT g_flatVerts = 0;            // how many of g_winTris belong to flat windows
struct BoxDraw { int win; UINT first, count; };
static std::vector<BoxDraw> g_boxDraws; // one triangle range per box window
static const UINT kMaxInst = 8192;   // half for cutouts, half for edit-mode UI
static const UINT kMaxWinVerts = 32768;   // window triangles: 32 round boxes (576 each) fit easily

static UINT g_sphereIndexCount = 0;
#ifdef _WIN32
// D3D11 resources
static ID3D11Device1* g_dev = nullptr;
static ID3D11DeviceContext1* g_ctx = nullptr;
// Experimental D3D12 support: our D3D11 drawing runs through D3D11On12 on the game's own queue.
static ID3D11On12Device* g_on12 = nullptr;
static ID3DDeviceContextState* g_state = nullptr;
static ID3D11VertexShader* g_vsFull = nullptr;
static ID3D11VertexShader* g_vsMesh = nullptr;
static ID3D11VertexShader* g_vsSphere = nullptr;
static ID3D11PixelShader* g_psConst = nullptr;
static ID3D11PixelShader* g_psColor = nullptr;
static ID3D11PixelShader* g_psBox = nullptr;
static ID3D11Buffer* g_cbBox = nullptr;
static ID3D11Buffer* g_cbWin = nullptr;   // all windows, for psBox (cbuffer W)
static ID3D11VertexShader* g_vsPanel = nullptr;
static ID3D11PixelShader* g_psPanel = nullptr;
static ID3D11VertexShader* g_vsSphereD = nullptr;   // hands behind game objects
static ID3D11PixelShader* g_psHandD = nullptr;
static ID3D11VertexShader* g_vsIcon = nullptr;      // edit-mode handles
static ID3D11PixelShader* g_psIcon = nullptr;
static ID3D11InputLayout* g_ilIcon = nullptr;
static ID3D11Buffer* g_quadVB = nullptr;
static ID3D11BlendState* g_bsBlend = nullptr;       // soft edges
static ID3D11InputLayout* g_ilPanel = nullptr;
static ID3D11Buffer* g_panelVB = nullptr;
static ID3D11Texture2D* g_guideTex = nullptr;
static ID3D11ShaderResourceView* g_guideSrvSrgb = nullptr;   // for sRGB / float render targets
static ID3D11ShaderResourceView* g_guideSrvUnorm = nullptr;  // for plain 8/10-bit render targets
static ID3D11SamplerState* g_sampLinear = nullptr;
static ID3D11InputLayout* g_ilMesh = nullptr;
static ID3D11InputLayout* g_ilSphere = nullptr;
static ID3D11Buffer* g_cb = nullptr;
static ID3D11Buffer* g_winVB = nullptr;
static ID3D11Buffer* g_sphereVB = nullptr;
static ID3D11Buffer* g_sphereIB = nullptr;
static ID3D11Buffer* g_instVB = nullptr;
static ID3D11BlendState* g_bsAlphaOnly = nullptr;
static ID3D11BlendState* g_bsAll = nullptr;
static ID3D11RasterizerState* g_rs = nullptr;
static ID3D11DepthStencilState* g_dss = nullptr;

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
#endif
#ifndef _WIN32
static const bool g_on12 = false;   // D3D12 (Windows only)
#endif

static void ReleaseHands() {
    for (auto& h : g_hand) {
        if (h.tracker && nDestroyHandTrackerEXT) nDestroyHandTrackerEXT(h.tracker);
        h = HandState();
    }
}
static void ReleaseActions() {
    for (auto& s : g_act.space) if (s && nDestroySpace) nDestroySpace(s);
    if (g_act.eyeSpace && nDestroySpace) nDestroySpace(g_act.eyeSpace);
    if (g_act.set && nDestroyActionSet) nDestroyActionSet(g_act.set);
    g_act = Actions();
    // Controller actions live as long as the instance; only their session parts go here.
    for (auto& s : g_ctl.space) { if (s && nDestroySpace) nDestroySpace(s); s = XR_NULL_HANDLE; }
    g_ctl.attached = false;
}

#ifdef _WIN32
static void ReleaseSwapchainInfo(SwapchainInfo& info) {
    for (auto& [k, rtv] : info.rtvs) if (rtv) rtv->Release();
    info.rtvs.clear();
    for (auto& [k, dc] : info.depthCopies) { if (dc.srv) dc.srv->Release(); if (dc.tex) dc.tex->Release(); }
    info.depthCopies.clear();
    if (g_on12) for (auto* t : info.images) if (t) t->Release();
    info.images.clear();
}

static void ReleaseD3D() {
    for (auto& [sc, info] : g_swapchains) ReleaseSwapchainInfo(info);
    g_swapchains.clear();
    if (g_on12 && g_ctx) { g_ctx->ClearState(); g_ctx->Flush(); } // our own context, not the game's
    SafeRelease(g_on12);
    SafeRelease(g_dss); SafeRelease(g_rs); SafeRelease(g_bsAll); SafeRelease(g_bsAlphaOnly);
    SafeRelease(g_instVB); SafeRelease(g_sphereIB); SafeRelease(g_sphereVB); SafeRelease(g_winVB); SafeRelease(g_cb);
    SafeRelease(g_cbBox); SafeRelease(g_cbWin);
    SafeRelease(g_sampLinear); SafeRelease(g_guideSrvUnorm); SafeRelease(g_guideSrvSrgb); SafeRelease(g_guideTex);
    SafeRelease(g_panelVB); SafeRelease(g_ilPanel); SafeRelease(g_psPanel); SafeRelease(g_vsPanel);
    SafeRelease(g_vsSphereD); SafeRelease(g_psHandD);
    SafeRelease(g_bsBlend); SafeRelease(g_quadVB); SafeRelease(g_ilIcon); SafeRelease(g_psIcon); SafeRelease(g_vsIcon);
    g_guideUploaded = false; g_guideText.clear(); g_guidePixels.clear();
    SafeRelease(g_ilSphere); SafeRelease(g_ilMesh);
    SafeRelease(g_psBox); SafeRelease(g_psColor); SafeRelease(g_psConst); SafeRelease(g_vsSphere); SafeRelease(g_vsMesh); SafeRelease(g_vsFull);
    SafeRelease(g_state); SafeRelease(g_ctx); SafeRelease(g_dev);
}
#else
static void ReleaseD3D() {}
#endif

// ================================================================ config
static std::string ConfigPath() {
    std::string n = g_appName;
    for (char& c : n) if (!isalnum((unsigned char)c)) c = '_';
    return g_dir + PATH_SEP "windows_" + n + ".cfg";
}
// Hands behind game objects (Ctrl+Alt+O): game surfaces closer than your hand cover it.
static bool g_handOcclude = true;   // on by default
static int g_handDepthUsed = -1;   // last frame: 1 = hands tested against game depth, 0 = no depth to test, -1 = off

static void LoadSettings() {
    FILE* f = fopen((g_dir + PATH_SEP "settings.cfg").c_str(), "r");
    if (!f) return;
    float s;
    int hands;
    if (fscanf(f, "%f", &s) == 1) g_handScale = std::clamp(s, 0.3f, 2.5f);
    if (fscanf(f, "%d", &hands) == 1) g_handsEnabled = hands != 0;
    int d3d12;
    if (fscanf(f, "%d", &d3d12) == 1) g_d3d12Enabled = d3d12 != 0;
    int force;
    if (fscanf(f, "%d", &force) == 1) g_forceAlpha = force != 0;
    int guide;
    if (fscanf(f, "%d", &guide) == 1) g_guideOn = guide != 0;
    int occ;
    if (fscanf(f, "%d", &occ) == 1) g_handOcclude = occ != 0;
    fclose(f);
}
static void SaveSettings() {
    FILE* f = fopen((g_dir + PATH_SEP "settings.cfg").c_str(), "w");
    if (!f) return;
    fprintf(f, "%f %d %d %d %d %d\n", g_handScale, g_handsEnabled ? 1 : 0, g_d3d12Enabled ? 1 : 0, g_forceAlpha ? 1 : 0,
            g_guideOn ? 1 : 0, g_handOcclude ? 1 : 0);
    fclose(f);
}
// Is Virtual Desktop's streamer running? Then SteamVR's frames reach the Quest through VD too.
static bool VirtualDesktopRunning() {
#ifndef _WIN32
    return false;
#else
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof pe };
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, L"VirtualDesktop.Streamer.exe") == 0;
    CloseHandle(snap);
    return found;
#endif
}
static bool g_viaVD = false;   // SteamVR runtime, streamed by Virtual Desktop
// Key colour mode, saved per runtime (key_<runtime>.cfg). SteamVR through Virtual Desktop shares
// VDXR's file: same headset, same VD passthrough setting.
static std::string KeyModePath() {
    std::string n = g_viaVD ? std::string("VirtualDesktopXR") : g_runtimeName;
    for (char& c : n) if (!isalnum((unsigned char)c)) c = '_';
    return g_dir + PATH_SEP "key_" + n + ".cfg";
}
static void LoadKeyMode() {
    g_keyMode = 0;
    FILE* f = fopen(KeyModePath().c_str(), "r");
    if (!f) return;
    int m = 0;
    if (fscanf(f, "%d", &m) == 1 && m >= 0 && m < 4) g_keyMode = m;
    fclose(f);
}
static void SaveKeyMode() {
    FILE* f = fopen(KeyModePath().c_str(), "w");
    if (!f) return;
    fprintf(f, "%d\n", g_keyMode);
    fclose(f);
}
static bool UseKeyColour() { return g_keyMode > 0; }
// Something can show the real world: a key colour, the runtime's alpha blend, or forced alpha.
static bool CutoutsShown() { return UseKeyColour() || g_alphaSupported || g_forceAlpha; }

// Reads a windows file (the game's own, or a preset) into g_windows. False if there's no file.
static bool ReadWindows(const std::string& path, bool* enabled) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    std::vector<Window> ws;
    int count = 0, en = 1;
    static char line[4096];
    if (fgets(line, sizeof line, f) && sscanf(line, "%d %d", &count, &en) == 2) {
        if (enabled) *enabled = en != 0;
        // One window per line. The 18th value (box half depth) is new in 1.3.0; older files are flat.
        // 1.4.0 adds the shape: point count, then x y per point (0 = rounded rectangle).
        for (int i = 0; i < count && i < kMaxWindows && fgets(line, sizeof line, f); ++i) {
            Window w; int s[4], used = 0;
            int n = sscanf(line, "%f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d %d %f%n",
                    &w.pos.x, &w.pos.y, &w.pos.z, &w.rot.x, &w.rot.y, &w.rot.z, &w.rot.w, &w.hw, &w.hh,
                    &w.radius[0], &w.radius[1], &w.radius[2], &w.radius[3], &s[0], &s[1], &s[2], &s[3], &w.hd, &used);
            if (n < 17) break;
            if (n < 18) { w.hd = 0; used = 0; }
            w.hd = std::clamp(w.hd, 0.0f, kMaxHalfDepth);
            (void)s;   // linked corner selection (before 1.4.0): no longer used
            const char* c = line + used;
            // Then the shape: point count (0 = rectangle), then x y per point.
            int np = 0, adv = 0, keep = 0;
            bool ok = used && sscanf(c, "%d%n", &np, &adv) == 1;
            if (ok) c += adv;
            if (ok && np >= 3 && np <= kMaxShapePts) {
                for (int k = 0; k < np; ++k) {
                    XMFLOAT2 p;
                    if (sscanf(c, "%f %f%n", &p.x, &p.y, &adv) != 2) { ok = false; break; }
                    c += adv;
                    w.pts.push_back(p);
                }
                if (w.pts.size() < 3) w.pts.clear(); else UpdateShapeBounds(w);
            }
            // Then the keep-box flag (0 / 1), then the circle flag (0 / 1), then the lock flag (0 / 1, 1.4.3).
            int ell = 0, lock = 0;
            if (ok && sscanf(c, "%d%n", &keep, &adv) == 1) {
                w.keep = keep != 0;
                c += adv;
                if (sscanf(c, "%d%n", &ell, &adv) == 1) {
                    w.ellipse = ell != 0 && !IsShape(w);
                    c += adv;
                    if (sscanf(c, "%d%n", &lock, &adv) == 1) {
                        w.lock = lock != 0;
                        c += adv;
                        int bm = 0;   // then the box face mode (1.5.0)
                        if (sscanf(c, "%d", &bm) == 1) w.boxMode = std::clamp(bm, 0, 3);
                    }
                }
            }
            ws.push_back(w);
        }
    }
    fclose(f);
    g_windows = std::move(ws);
    return true;
}
// Presets 1-3: saved layouts for this game, next to its own file (windows_<game>_preset1.cfg).
static std::string PresetPath(int i) {
    std::string p = ConfigPath();
    return p.substr(0, p.size() - 4) + "_preset" + std::to_string(i + 1) + ".cfg";
}
static void CheckPresets() {
    for (int i = 0; i < 3; ++i) g_presetHas[i] = FileExists(PresetPath(i));
}
static void LoadWindows() {
    g_windows.clear();
    CheckPresets();
    if (!ReadWindows(ConfigPath(), &g_windowsEnabled)) {
        Log("No saved windows for this game. Make a picture-frame gesture to create one.\n");
        return;
    }
    Log("Loaded %d window(s).\n", (int)g_windows.size());
}
static bool WriteWindows(const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "%d %d\n", (int)g_windows.size(), g_windowsEnabled ? 1 : 0);
    for (auto& w : g_windows) {
        fprintf(f, "%f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d %d %f %d", w.pos.x, w.pos.y, w.pos.z,
            w.rot.x, w.rot.y, w.rot.z, w.rot.w, w.hw, w.hh, w.radius[0], w.radius[1], w.radius[2], w.radius[3],
            0, 0, 0, 0, w.hd, IsShape(w) ? (int)w.pts.size() : 0);
        if (IsShape(w)) for (auto& p : w.pts) fprintf(f, " %f %f", p.x, p.y);
        fprintf(f, " %d %d %d %d\n", w.keep ? 1 : 0, w.ellipse ? 1 : 0, w.lock ? 1 : 0, w.boxMode);
    }
    fclose(f);
    return true;
}
static void SaveWindows() {
    if (WriteWindows(ConfigPath())) Log("Saved %d window(s).\n", (int)g_windows.size());
}
static void PresetFlash(int i, bool saved) {
    g_presetFlash = i; g_presetFlashSaved = saved; g_presetFlashUntil = Now() + 2.0;
}
// Save the current windows as preset i (0..2).
static void SavePreset(int i, const char* why) {
    if (!WriteWindows(PresetPath(i))) { Log("Preset %d: couldn't save.\n", i + 1); return; }
    g_presetHas[i] = true;
    PresetFlash(i, true);
    Log("Preset %d saved: %d window(s) (%s)\n", i + 1, (int)g_windows.size(), why);
}
// Put the windows back as saved in preset i. Becomes the game's current layout too.
static void LoadPreset(int i, const char* why) {
    if (!g_presetHas[i]) { Log("Preset %d is empty: hold its button (or Ctrl+Alt+Shift+%d) to save one.\n", i + 1, i + 1); return; }
    if (!ReadWindows(PresetPath(i), nullptr)) { g_presetHas[i] = false; Log("Preset %d: file missing.\n", i + 1); return; }
    g_selected = -1; g_drawArmed = -1;
    for (auto& h : g_hand) if (h.mode != GRAB_PRESET) { h.mode = GRAB_NONE; h.win = -1; }
    PresetFlash(i, false);
    Log("Preset %d loaded: %d window(s) (%s)\n", i + 1, (int)g_windows.size(), why);
    SaveWindows();
}

// ================================================================ D3D setup
// Precompiled shader bytecode (shaders.hlsl, built by fxc), so no runtime shader compiler is needed.
#ifdef _WIN32
#include "vsFull.h"
#include "vsMesh.h"
#include "vsSphere.h"
#include "psConst.h"
#include "psColor.h"
#include "psBox.h"
#include "vsPanel.h"
#include "psPanel.h"
#include "vsSphereD.h"
#include "psHandD.h"
#include "vsIcon.h"
#include "psIcon.h"
#endif

struct CB { XMFLOAT4X4 mvp; XMFLOAT4 col, camR, camU; };   // must match cbuffer C in shaders.hlsl
// Must match cbuffer B in shaders.hlsl.
struct CBBox {
    XMFLOAT4 rectC, rectD, tanLRDU, depthMap;
    XMFLOAT4 info;                     // x = 1: game depth available
    XMFLOAT4 col;
};
// Must match cbuffer W in shaders.hlsl (MAX_WIN = kMaxWindows): every window, for psBox.
struct WinGPU {
    XMFLOAT4X4 toLocal;                // eye view space -> window local space
    XMFLOAT4 dim;                      // half width, half height, half depth, kind: 0 flat, 1 box, 2 keep box
    XMFLOAT4 radii;
    XMFLOAT4 shape;                    // x = shape point count (0 = rounded rectangle), y = 1: circle, z = box volume
    XMFLOAT4 pts[kMaxShapePts / 2];    // shape points, two per float4
};
struct CBWin { XMFLOAT4 count; WinGPU win[kMaxWindows]; };
static_assert(sizeof(WinGPU) % 16 == 0 && sizeof(CBWin) % 16 == 0, "cbuffer W layout");

// Low-poly unit sphere (hand cutouts): positions (x y z) and triangle indices.
static void MakeSphere(std::vector<float>& sv, std::vector<uint16_t>& si) {
    const int rings = 8, segs = 12;
    for (int i = 0; i <= rings; ++i) {
        float th = XM_PI * i / rings;
        for (int j = 0; j < segs; ++j) {
            float ph = XM_2PI * j / segs;
            sv.push_back(sinf(th) * cosf(ph)); sv.push_back(cosf(th)); sv.push_back(sinf(th) * sinf(ph));
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < segs; ++j) {
            uint16_t a = uint16_t(i * segs + j), b = uint16_t(i * segs + (j + 1) % segs);
            uint16_t c = uint16_t((i + 1) * segs + j), d = uint16_t((i + 1) * segs + (j + 1) % segs);
            si.insert(si.end(), { a, c, b, b, c, d });
        }
}

#ifdef _WIN32
static bool InitD3D(ID3D11Device* device) {
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), (void**)&g_dev))) { Log("No ID3D11Device1\n"); return false; }
    g_dev->GetImmediateContext1(&g_ctx);
    D3D_FEATURE_LEVEL fl = g_dev->GetFeatureLevel();
    UINT flags = (g_dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    if (FAILED(g_dev->CreateDeviceContextState(flags, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &g_state))) {
        Log("CreateDeviceContextState failed\n"); return false;
    }

    g_dev->CreateVertexShader(g_vsFull_bc, sizeof(g_vsFull_bc), nullptr, &g_vsFull);
    g_dev->CreateVertexShader(g_vsMesh_bc, sizeof(g_vsMesh_bc), nullptr, &g_vsMesh);
    g_dev->CreateVertexShader(g_vsSphere_bc, sizeof(g_vsSphere_bc), nullptr, &g_vsSphere);
    g_dev->CreatePixelShader(g_psConst_bc, sizeof(g_psConst_bc), nullptr, &g_psConst);
    g_dev->CreatePixelShader(g_psColor_bc, sizeof(g_psColor_bc), nullptr, &g_psColor);
    g_dev->CreatePixelShader(g_psBox_bc, sizeof(g_psBox_bc), nullptr, &g_psBox);
    g_dev->CreateVertexShader(g_vsPanel_bc, sizeof(g_vsPanel_bc), nullptr, &g_vsPanel);
    g_dev->CreatePixelShader(g_psPanel_bc, sizeof(g_psPanel_bc), nullptr, &g_psPanel);
    g_dev->CreateVertexShader(g_vsSphereD_bc, sizeof(g_vsSphereD_bc), nullptr, &g_vsSphereD);
    g_dev->CreatePixelShader(g_psHandD_bc, sizeof(g_psHandD_bc), nullptr, &g_psHandD);
    g_dev->CreateVertexShader(g_vsIcon_bc, sizeof(g_vsIcon_bc), nullptr, &g_vsIcon);
    g_dev->CreatePixelShader(g_psIcon_bc, sizeof(g_psIcon_bc), nullptr, &g_psIcon);
    {
        D3D11_INPUT_ELEMENT_DESC pied[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_dev->CreateInputLayout(pied, 2, g_vsPanel_bc, sizeof(g_vsPanel_bc), &g_ilPanel);
        D3D11_BUFFER_DESC pb{ 4 * 5 * sizeof(float), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
        g_dev->CreateBuffer(&pb, nullptr, &g_panelVB);
        // Typeless with mips, so it can be read as sRGB or plain to suit the game's image format.
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kGuideW; td.Height = kGuideH; td.MipLevels = 0; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &g_guideTex))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = (UINT)-1;
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; g_dev->CreateShaderResourceView(g_guideTex, &sd, &g_guideSrvSrgb);
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;      g_dev->CreateShaderResourceView(g_guideTex, &sd, &g_guideSrvUnorm);
        }
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_ANISOTROPIC; smp.MaxAnisotropy = 8;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->CreateSamplerState(&smp, &g_sampLinear);
        if (!g_vsPanel || !g_psPanel || !g_ilPanel || !g_panelVB || !g_guideSrvSrgb || !g_guideSrvUnorm || !g_sampLinear)
            Log("Edit-mode guide unavailable (D3D setup failed).\n");
    }
    D3D11_INPUT_ELEMENT_DESC mied{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    g_dev->CreateInputLayout(&mied, 1, g_vsMesh_bc, sizeof(g_vsMesh_bc), &g_ilMesh);
    D3D11_INPUT_ELEMENT_DESC sied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };
    g_dev->CreateInputLayout(sied, 3, g_vsSphere_bc, sizeof(g_vsSphere_bc), &g_ilSphere);
    {
        D3D11_INPUT_ELEMENT_DESC iied[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };
        g_dev->CreateInputLayout(iied, 6, g_vsIcon_bc, sizeof(g_vsIcon_bc), &g_ilIcon);
        static const float quad[12] = { -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1 };   // two triangles
        D3D11_BUFFER_DESC b{ sizeof(quad), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
        D3D11_SUBRESOURCE_DATA s{ quad };
        g_dev->CreateBuffer(&b, &s, &g_quadVB);
        if (!g_vsIcon || !g_psIcon || !g_ilIcon || !g_quadVB) Log("Edit handles: icon shader unavailable, drawing plain dots.\n");
    }

    // Low-poly unit sphere.
    {
        std::vector<float> sv;
        std::vector<uint16_t> si;
        MakeSphere(sv, si);
        g_sphereIndexCount = (UINT)si.size();
        D3D11_BUFFER_DESC b{ UINT(sv.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
        D3D11_SUBRESOURCE_DATA s{ sv.data() };
        g_dev->CreateBuffer(&b, &s, &g_sphereVB);
        b = { UINT(si.size() * 2), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER };
        s = { si.data() };
        g_dev->CreateBuffer(&b, &s, &g_sphereIB);
    }
    D3D11_BUFFER_DESC bd{ kMaxInst * sizeof(Inst), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
    g_dev->CreateBuffer(&bd, nullptr, &g_instVB);
    bd = { kMaxWinVerts * sizeof(XMFLOAT3), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
    g_dev->CreateBuffer(&bd, nullptr, &g_winVB);
    bd = { sizeof(CB), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    g_dev->CreateBuffer(&bd, nullptr, &g_cb);
    bd = { sizeof(CBBox), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    g_dev->CreateBuffer(&bd, nullptr, &g_cbBox);
    bd = { sizeof(CBWin), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    g_dev->CreateBuffer(&bd, nullptr, &g_cbWin);

    D3D11_BLEND_DESC bld{};
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
    g_dev->CreateBlendState(&bld, &g_bsAlphaOnly);
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_dev->CreateBlendState(&bld, &g_bsAll);
    {   // Edit handles: soft edges. Colour blends; alpha builds up to 1 (solid) like the rest of the UI.
        D3D11_BLEND_DESC bb{};
        auto& t = bb.RenderTarget[0];
        t.BlendEnable = TRUE;
        t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha = D3D11_BLEND_ONE; t.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev->CreateBlendState(&bb, &g_bsBlend);
    }

    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode = D3D11_FILL_SOLID; rsd.CullMode = D3D11_CULL_NONE; rsd.DepthClipEnable = FALSE;
    g_dev->CreateRasterizerState(&rsd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dsd{};
    g_dev->CreateDepthStencilState(&dsd, &g_dss);
    return g_vsFull && g_vsMesh && g_vsSphere && g_psConst && g_psColor && g_psBox && g_cbBox && g_cbWin && g_ilMesh && g_ilSphere && g_cb &&
           g_winVB && g_sphereVB && g_sphereIB && g_instVB && g_bsAlphaOnly && g_bsAll && g_rs && g_dss;
}
#endif

// ================================================================ hotkeys
// Ctrl+Alt shortcuts, registered with Windows (RegisterHotKey) by a small thread of our own that
// waits for them. Only these key combinations ever reach the layer: it doesn't read the keyboard.
enum HotkeyId { HK_E, HK_P, HK_J, HK_V, HK_X, HK_N, HK_G, HK_C, HK_O, HK_MINUS, HK_PLUS, HK_SHALLOW, HK_DEEP,
                HK_D, HK_K, HK_1, HK_2, HK_3, HK_S1, HK_S2, HK_S3, HK_COUNT };
#ifdef _WIN32
// HK_S1..3 are Ctrl+Alt+Shift+1..3 (save a preset); the rest are Ctrl+Alt+key.
static const UINT kHotkeyVk[HK_COUNT] = { 'E', 'P', 'J', 'V', 'X', 'N', 'G', 'C', 'O', VK_OEM_MINUS, VK_OEM_PLUS,
                                          VK_OEM_4, VK_OEM_6, 'D', 'K', '1', '2', '3', '1', '2', '3' };
static const char* kHotkeyName[HK_COUNT] = { "E", "P", "J", "V", "X", "N", "G", "C", "O", "-", "=", "[", "]", "D", "K",
                                             "1", "2", "3", "Shift+1", "Shift+2", "Shift+3" };
static std::atomic<uint32_t> g_hotkeyHits{ 0 };     // one bit per HotkeyId, set when pressed
static std::atomic<int> g_hotkeyFailed{ -1 };       // bits that couldn't be registered; -1 = not known yet
static HANDLE g_hotkeyThread = nullptr;
static DWORD g_hotkeyThreadId = 0;

static DWORD WINAPI HotkeyThread(LPVOID) {
    MSG m;
    PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // creates this thread's message queue
    int failed = 0;
    for (int i = 0; i < HK_COUNT; ++i)
        if (!RegisterHotKey(nullptr, i + 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT | (i >= HK_S1 ? MOD_SHIFT : 0), kHotkeyVk[i])) failed |= 1 << i;
    g_hotkeyFailed = failed;
    while (GetMessageW(&m, nullptr, 0, 0) > 0)
        if (m.message == WM_HOTKEY && m.wParam >= 1 && m.wParam <= HK_COUNT) g_hotkeyHits.fetch_or(1u << (m.wParam - 1));
    for (int i = 0; i < HK_COUNT; ++i) UnregisterHotKey(nullptr, i + 1);
    return 0;
}
static bool g_hotkeyLogged = false;
static void StartHotkeys() {
    if (!g_hotkeyThread) g_hotkeyThread = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, &g_hotkeyThreadId);
    int failed = g_hotkeyFailed;
    if (g_hotkeyLogged || failed < 0) return;   // log once, after the thread has registered them
    g_hotkeyLogged = true;
    if (!failed) { Log("Ctrl+Alt shortcuts ready.\n"); return; }
    std::string list;
    for (int i = 0; i < HK_COUNT; ++i) if (failed & (1 << i)) { list += ' '; list += kHotkeyName[i]; }
    Log("Ctrl+Alt shortcuts: another program already uses Ctrl+Alt +%s, so those don't work here.\n", list.c_str());
}
static void StopHotkeys() {
    if (!g_hotkeyThread) return;
    // The thread's message queue may not exist for a moment after it starts: retry briefly.
    for (int i = 0; i < 100 && !PostThreadMessageW(g_hotkeyThreadId, WM_QUIT, 0, 0); ++i)
        if (WaitForSingleObject(g_hotkeyThread, 10) == WAIT_OBJECT_0) break;
    WaitForSingleObject(g_hotkeyThread, 2000);
    CloseHandle(g_hotkeyThread);
    g_hotkeyThread = nullptr; g_hotkeyThreadId = 0;
    g_hotkeyHits = 0; g_hotkeyFailed = -1; g_hotkeyLogged = false;
}
#else
// Linux: no Ctrl+Alt shortcuts (a layer can't watch the keyboard there); everything is in the headset.
static std::atomic<uint32_t> g_hotkeyHits{ 0 };
static void StartHotkeys() {}
static void StopHotkeys() {}
#endif
// True once per press of Ctrl+Alt+<key>.
static bool Pressed(HotkeyId k) { return (g_hotkeyHits.fetch_and(~(1u << k)) & (1u << k)) != 0; }
static void SetEdit(bool on, const char* why) {
    if (g_edit == on) return;
    g_edit = on;
    Log("Edit mode %s (%s)\n", on ? "ON" : "OFF", why);
    if (!on) {
        for (auto& h : g_hand) { h.mode = GRAB_NONE; h.win = -1; }
        g_drawArmed = -1; g_trace.clear();
        SaveWindows();
    }
}
#ifdef _WIN32
#define KEY_COLOUR_HOW "Ctrl+Alt+C (or a V sign / thumbstick press in edit mode)"
#else
#define KEY_COLOUR_HOW "a V sign or thumbstick press in edit mode"
#endif
// Next cutout colour: alpha -> green -> blue -> pink (Ctrl+Alt+C, V sign, thumbstick press).
static void CycleKeyMode(const char* why) {
    g_keyMode = (g_keyMode + 1) % 4;
    Log("Cutouts: %ls%s (%s, for %s)\n", kKeyName[g_keyMode],
        g_keyMode ? " key colour, for streamers that chroma-key it to passthrough" :
        CutoutsShown() ? " (transparent)" : " (transparent, but this runtime shows no passthrough for it)",
        why, g_runtimeName.c_str());
    SaveKeyMode();
}
static void HandleKeys() {
    bool kE = Pressed(HK_E), kP = Pressed(HK_P), kJ = Pressed(HK_J), kV = Pressed(HK_V), kX = Pressed(HK_X), kN = Pressed(HK_N), kG = Pressed(HK_G);
    bool kC = Pressed(HK_C), kO = Pressed(HK_O);
    bool kMinus = Pressed(HK_MINUS), kPlus = Pressed(HK_PLUS);
    bool kShallow = Pressed(HK_SHALLOW), kDeep = Pressed(HK_DEEP);   // [ and ]
    if ((kShallow || kDeep) && g_selected >= 0 && g_selected < (int)g_windows.size()) {
        Window& w = g_windows[g_selected];
        w.hd = std::clamp(w.hd + (kDeep ? 0.005f : -0.005f), 0.0f, kMaxHalfDepth);
        if (w.hd < 0.004f) w.hd = 0;
        Log("Window %d depth %.0f cm%s\n", g_selected, w.hd * 200, w.hd > 0 ? "" : " (flat)");
        SaveWindows();
    }
    if (kMinus || kPlus) {
        g_handScale = std::clamp(g_handScale + (kPlus ? 0.05f : -0.05f), 0.3f, 2.5f);
        Log("Hand cutout size %.2f\n", g_handScale);
        SaveSettings();
    }
    if (kE) SetEdit(!g_edit, "hotkey");
    // Dev / tests only: ALPHA_VIEWS_EDIT=<frames> turns edit mode on after that many frames.
    {
        static int editAfter = atoi(EnvVar("ALPHA_VIEWS_EDIT").c_str()), frame = 0;
        if (editAfter > 0 && ++frame == editAfter) SetEdit(true, "ALPHA_VIEWS_EDIT");
    }
    if (kN) g_spawnAhead = true;
    if (kG) { g_guideOn = !g_guideOn; Log("Edit-mode guide %s\n", g_guideOn ? "on" : "off"); SaveSettings(); }
    if (kP) { g_windowsEnabled = !g_windowsEnabled; Log("Windows %s\n", g_windowsEnabled ? "on" : "off"); SaveWindows(); }
    if (kJ) { g_handsEnabled = !g_handsEnabled; Log("Hand cutouts %s (hotkey)\n", g_handsEnabled ? "on" : "off"); SaveSettings(); }
    if (kV) { g_visualize = !g_visualize; Log("Visualize %s\n", g_visualize ? "on" : "off"); }
    if (kC) CycleKeyMode("Ctrl+Alt+C");
    for (int i = 0; i < 3; ++i) {
        if (Pressed(HotkeyId(HK_S1 + i))) SavePreset(i, "Ctrl+Alt+Shift+number");
        if (Pressed(HotkeyId(HK_1 + i))) LoadPreset(i, "Ctrl+Alt+number");
    }
    if (kO) {
        g_handOcclude = !g_handOcclude;
        Log("Hands behind game objects %s (Ctrl+Alt+O)%s\n", g_handOcclude ? "ON" : "off",
            g_handOcclude ? ": game surfaces closer than your hand cover it. Needs the game's depth." : "");
        SaveSettings();
    }
    if (kX) {
        g_windows.clear();
        g_selected = -1; g_drawArmed = -1;
        for (auto& h : g_hand) { h.mode = GRAB_NONE; h.win = -1; }
        Log("All windows deleted\n"); SaveWindows();
    }
}
// Ctrl+Alt+D: experimental D3D12 support. Checked every frame even while the layer is idle
// (a D3D12 game with it off), so testers can switch it on from inside that game.
static void HandleD3D12Key() {
    if (!Pressed(HK_D)) return;
    g_d3d12Enabled = !g_d3d12Enabled;
    Log("Experimental D3D12 support %s. Restart the game for it to take effect.\n", g_d3d12Enabled ? "ON" : "OFF");
    SaveSettings();
#ifdef _WIN32
    MessageBeep(g_d3d12Enabled ? MB_OK : MB_ICONHAND);
#endif
}
// Ctrl+Alt+K: force alpha. For runtimes that don't offer ALPHA_BLEND but can still pass alpha on
// (test option). Takes effect straight away.
static void HandleForceAlphaKey() {
    if (!Pressed(HK_K)) return;
    g_forceAlpha = !g_forceAlpha;
    Log("Force alpha %s.\n", g_forceAlpha ? "ON: cutouts transparent, layer alpha flags set (game's blend mode kept)" : "OFF");
    SaveSettings();
#ifdef _WIN32
    MessageBeep(g_forceAlpha ? MB_OK : MB_ICONHAND);
#endif
}

// ================================================================ hand analysis
static bool JValid(const HandState& h, int k) { return (h.j[k].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0; }
static XMVECTOR JP(const HandState& h, int k) { return V(h.j[k].pose.position); }

// 1 = straight finger, lower = bent. Chain from proximal to tip.
static float Straightness(const HandState& h, const int* chain, int n) {
    float path = 0;
    for (int i = 0; i + 1 < n; ++i) path += Dist(JP(h, chain[i]), JP(h, chain[i + 1]));
    return path > 1e-4f ? Dist(JP(h, chain[0]), JP(h, chain[n - 1])) / path : 0;
}
static const int kThumb[] = { XR_HAND_JOINT_THUMB_PROXIMAL_EXT, XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT };
static const int kIndex[] = { XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT };
static const int kMiddle[] = { XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT };
static const int kRing[] = { XR_HAND_JOINT_RING_PROXIMAL_EXT, XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT };
static const int kLittle[] = { XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT };

static bool Curled(const HandState& h, const int* c) {
    return Straightness(h, c, 4) < 0.75f || Dist(JP(h, c[3]), JP(h, XR_HAND_JOINT_PALM_EXT)) < 0.06f;
}
static bool Extended(const HandState& h, const int* c, int n) { return Straightness(h, c, n) > 0.88f; }
static bool OthersCurled(const HandState& h) { return Curled(h, kMiddle) && Curled(h, kRing) && Curled(h, kLittle); }

// "L" shape: thumb and index out at roughly a right angle, other fingers curled.
static bool IsLShape(const HandState& h) {
    if (!Extended(h, kIndex, 4) || !Extended(h, kThumb, 3) || !OthersCurled(h)) return false;
    XMVECTOR ti = XMVector3Normalize(JP(h, XR_HAND_JOINT_THUMB_TIP_EXT) - JP(h, XR_HAND_JOINT_THUMB_PROXIMAL_EXT));
    XMVECTOR ii = XMVector3Normalize(JP(h, XR_HAND_JOINT_INDEX_TIP_EXT) - JP(h, XR_HAND_JOINT_INDEX_PROXIMAL_EXT));
    float c = XMVectorGetX(XMVector3Dot(ti, ii));
    return c < 0.6f && c > -0.5f; // ~53..120 degrees
}
// +1 thumbs up, -1 thumbs down, 0 neither.
static int ThumbDir(const HandState& h) {
    if (!Extended(h, kThumb, 3) || !Curled(h, kIndex) || !OthersCurled(h)) return 0;
    XMVECTOR d = XMVector3Normalize(JP(h, XR_HAND_JOINT_THUMB_TIP_EXT) - JP(h, XR_HAND_JOINT_THUMB_PROXIMAL_EXT));
    float y = XMVectorGetY(d);
    return y > 0.75f ? 1 : (y < -0.75f ? -1 : 0);
}
// V sign: index and middle straight and spread apart, ring and little curled.
// (The L shape needs the middle finger curled, so the two can't be confused.)
static bool IsVSign(const HandState& h) {
    if (!Extended(h, kIndex, 4) || !Extended(h, kMiddle, 4) || !Curled(h, kRing) || !Curled(h, kLittle)) return false;
    return Dist(JP(h, XR_HAND_JOINT_INDEX_TIP_EXT), JP(h, XR_HAND_JOINT_MIDDLE_TIP_EXT)) > 0.03f;
}
// Corner of the picture frame: the crook between thumb and index.
static XMVECTOR FrameCorner(const HandState& h) {
    return (JP(h, XR_HAND_JOINT_THUMB_PROXIMAL_EXT) + JP(h, XR_HAND_JOINT_INDEX_PROXIMAL_EXT)) * 0.5f;
}

// ================================================================ interaction
// a, b: the two frame corners (width spans them). ea, eb: extra points that also set the height
// (index tips for hands; for controllers just a and b again).
static bool SpawnWindow(XMVECTOR a, XMVECTOR b, XMVECTOR ea, XMVECTOR eb, XMVECTOR head) {
    XMVECTOR c = (a + b) * 0.5f;
    XMVECTOR n = head - c;
    n = XMVectorSetY(n, XMVectorGetY(n) * 0.5f); // keep it fairly upright
    if (XMVectorGetX(XMVector3Length(n)) < 0.05f) n = XMVectorSet(0, 0, 1, 0);
    n = XMVector3Normalize(n);
    XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), n);
    if (XMVectorGetX(XMVector3Length(right)) < 0.1f) right = XMVectorSet(1, 0, 0, 0);
    right = XMVector3Normalize(right);
    XMVECTOR up = XMVector3Cross(n, right);
    XMMATRIX basis(right, up, n, XMVectorSet(0, 0, 0, 1));
    // Width spans the two crooks. Height spans the crooks and index tips, so both styles work:
    // thumbs touching with index fingers up (crooks level), or L's at opposite corners.
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    const XMVECTOR pts[4] = { a, b, ea, eb };
    for (int i = 0; i < 4; i++) {
        float x = XMVectorGetX(XMVector3Dot(pts[i] - c, right));
        float y = XMVectorGetX(XMVector3Dot(pts[i] - c, up));
        if (i < 2) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
        y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
    Window w;
    XMStoreFloat3(&w.pos, c + right * ((x0 + x1) * 0.5f) + up * ((y0 + y1) * 0.5f));
    XMStoreFloat4(&w.rot, XMQuaternionNormalize(XMQuaternionRotationMatrix(basis)));
    w.hw = std::max(0.03f, (x1 - x0) * 0.5f);
    w.hh = std::max(0.03f, (y1 - y0) * 0.5f);
    if ((int)g_windows.size() >= kMaxWindows) {   // full: never delete one to make room
        g_windowsFullUntil = Now() + 4;
        Log("Window not created: %d windows is the most. Delete one first (thumbs down, hold B / Y, or shrink it).\n", kMaxWindows);
        return false;
    }
    g_windows.push_back(w);
    g_drawArmed = -1;
    g_selected = (int)g_windows.size() - 1;
    for (auto& h : g_hand) { h.mode = GRAB_NONE; h.win = -1; }
    Log("Window created %.0f x %.0f cm\n", w.hw * 200, w.hh * 200);
    SetEdit(true, "new window");
    SaveWindows();
    return true;
}

static XMMATRIX HandGrabMatrix(const HandState& h) {
    return XMMatrixRotationQuaternion(XMLoadFloat4(&h.grabRot)) *
           XMMatrixTranslation(h.pinchPt.x, h.pinchPt.y, h.pinchPt.z);
}

// Free shape handles: a dot per point (orange), a small dot in the middle of each edge (adds a
// point), and two buttons just above the window: type (purple, rectangle <-> shape) and, for
// shapes, draw (white; red while armed).
static const float kPointR = 0.011f, kPointHit = 0.03f, kMidR = 0.0065f, kMidHit = 0.02f;
static const float kButtonR = 0.013f, kButtonHit = 0.03f;
// Buttons above the window, left to right: lock, type, draw (free shapes) or circle (rectangles), snap.
// 6 cm apart and 4.5 cm above the window up close; spread further on far windows (SpreadTop).
static XMFLOAT2 TopButton(const Window& w, float x) { float s = SpreadTop(w); return { x * s, w.hh + 0.045f * s }; }
static XMFLOAT2 LockButtonLocal(const Window& w) { return TopButton(w, -0.09f); }
static XMFLOAT2 TypeButtonLocal(const Window& w) { return TopButton(w, -0.03f); }
static XMFLOAT2 DrawButtonLocal(const Window& w) { return TopButton(w, 0.03f); }
static XMFLOAT2 CircleButtonLocal(const Window& w) { return TopButton(w, 0.03f); }
static XMFLOAT2 SnapButtonLocal(const Window& w) { return TopButton(w, 0.09f); }
// Keep-box button (solid box): just outside the depth handle, on the centre line too.
static XMVECTOR KeepButton(const Window& w) { return WinToWorld(w, w.hw + (kDepthOut + 0.042f) * SpreadSide(w), 0, 0); }
static XMVECTOR FacesButton(const Window& w) { return WinToWorld(w, w.hw + (kDepthOut + 0.084f) * SpreadSide(w), 0, 0); }
static XMFLOAT2 MidLocal(const Window& w, int k) {
    const XMFLOAT2 &a = w.pts[k], &b = w.pts[(k + 1) % w.pts.size()];
    return { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
}

// World position of a grab target's dot (centre dot for move).
static XMVECTOR TargetPoint(const Window& w, GrabMode mode, int corner) {
    if (mode == GRAB_POINT && corner >= 0 && corner < (int)w.pts.size()) return WinToWorld(w, w.pts[corner].x, w.pts[corner].y);
    if (mode == GRAB_ADDPT && corner >= 0 && corner < (int)w.pts.size()) { XMFLOAT2 m = MidLocal(w, corner); return WinToWorld(w, m.x, m.y); }
    if (mode == GRAB_TYPE) { XMFLOAT2 b = TypeButtonLocal(w); return WinToWorld(w, b.x, b.y); }
    if (mode == GRAB_DRAWBTN) { XMFLOAT2 b = DrawButtonLocal(w); return WinToWorld(w, b.x, b.y); }
    if (mode == GRAB_CORNER) return WinToWorld(w, kCS[corner][0] * w.hw, kCS[corner][1] * w.hh);
    if (mode == GRAB_RADIUS) { XMFLOAT2 rh = RadiusHandleLocal(w, corner); return WinToWorld(w, rh.x, rh.y); }
    if (mode == GRAB_DEPTH) return DepthHandle(w);
    if (mode == GRAB_KEEPBTN) return KeepButton(w);
    if (mode == GRAB_FACESBTN) return FacesButton(w);
    if (mode == GRAB_CIRCLEBTN) { XMFLOAT2 b = CircleButtonLocal(w); return WinToWorld(w, b.x, b.y); }
    if (mode == GRAB_LOCKBTN) { XMFLOAT2 b = LockButtonLocal(w); return WinToWorld(w, b.x, b.y); }
    if (mode == GRAB_SNAPBTN) { XMFLOAT2 b = SnapButtonLocal(w); return WinToWorld(w, b.x, b.y); }
    return WinToWorld(w, 0, 0);
}

// ---------------------------------------------------------------- guide panel: grab and buttons
static bool GuideLive() { return g_guideShow && g_guidePlaced; }
// A point in the panel's own coordinates: x right, y up, z towards you (metres, centre = 0).
static XMFLOAT3 GuideLocal(XMVECTOR p) {
    XMVECTOR v = p - V(g_guidePos);
    return { XMVectorGetX(XMVector3Dot(v, V(g_guideRight))), XMVectorGetX(XMVector3Dot(v, V(g_guideUp))),
             XMVectorGetX(XMVector3Dot(v, V(g_guideN))) };
}
static bool OnGuide(float x, float y) { return fabsf(x) <= kGuideHalfW && fabsf(y) <= kGuideHalfH; }
// Which panel button is at this panel point: GRAB_ALL (move all windows), GRAB_PRESET (idx = 0..2),
// or GRAB_GUIDE (the rest of the panel). A little margin around each button.
static GrabMode GuideButtonAt(float x, float y, int* idx) {
    float px = (x / kGuideHalfW + 1) * 0.5f * kGuideW, py = (1 - y / kGuideHalfH) * 0.5f * kGuideH;
    if (py < kMoveAllBtn.top - 12 || py > kMoveAllBtn.bottom + 16) return GRAB_GUIDE;
    if (px > kMoveAllBtn.left - 20 && px < kMoveAllBtn.right + 14) return GRAB_ALL;
    for (int i = 0; i < 3; ++i) {
        RECT b = PresetBtn(i);
        if (px > b.left - 6 && px < b.right + 6) { if (idx) *idx = i; return GRAB_PRESET; }
    }
    return GRAB_GUIDE;
}
// Where a ray meets the panel. t = distance along the ray. False if it misses.
static bool GuideRay(XMVECTOR o, XMVECTOR d, float& t, XMFLOAT3& l) {
    if (!GuideLive()) return false;
    float den = XMVectorGetX(XMVector3Dot(d, V(g_guideN)));
    if (fabsf(den) < 1e-4f) return false;
    t = XMVectorGetX(XMVector3Dot(V(g_guidePos) - o, V(g_guideN))) / den;
    if (t <= 0.05f) return false;
    l = GuideLocal(o + d * t);
    return OnGuide(l.x, l.y);
}
// Hand (or controller tip) touching the panel: within 5 cm of it.
static GrabMode GuideTouch(XMVECTOR p, int* idx = nullptr) {
    if (!GuideLive()) return GRAB_NONE;
    XMFLOAT3 l = GuideLocal(p);
    if (fabsf(l.z) > 0.05f || !OnGuide(l.x, l.y)) return GRAB_NONE;
    return GuideButtonAt(l.x, l.y, idx);
}

// What a ray points at: the guide panel if it's in front, else a handle of the selected window
// (within a small angle), else the nearest window surface it hits. len = distance along the ray
// to that point (0 = nothing).
static GazeTarget RayTarget(XMVECTOR o, XMVECTOR d, float maxDeg, float* len) {
    GazeTarget g;
    float gl = 0;
    d = XMVector3Normalize(d);
    auto angle = [&](XMVECTOR p) {
        XMVECTOR v = XMVector3Normalize(p - o);
        return acosf(std::clamp(XMVectorGetX(XMVector3Dot(v, d)), -1.0f, 1.0f));
    };
    float best = XMConvertToRadians(maxDeg);
    bool onHandle = false;   // a handle of the selected window: drawn over the guide panel, so it wins over it
    XMVECTOR hp = XMVectorZero();
    if (g_selected >= 0 && g_selected < (int)g_windows.size()) {
        const Window& w = g_windows[g_selected];
        auto consider = [&](GrabMode m, int k) {
            XMVECTOR p = TargetPoint(w, m, k);
            float a = angle(p);
            if (a < best) { best = a; g = { m, g_selected, k }; gl = Dist(o, p); onHandle = true; hp = p; }
        };
        if (IsShape(w)) {
            for (int k = 0; k < (int)w.pts.size(); ++k) { consider(GRAB_POINT, k); consider(GRAB_ADDPT, k); }
            consider(GRAB_DRAWBTN, -1);
        } else {
            for (int k = 0; k < 4; ++k) { consider(GRAB_CORNER, k); if (!w.ellipse) consider(GRAB_RADIUS, k); }
        }
        consider(GRAB_TYPE, -1);
        consider(GRAB_DEPTH, -1);
        consider(GRAB_KEEPBTN, -1);
        if (w.hd > 0) consider(GRAB_FACESBTN, -1);
        if (!IsShape(w)) consider(GRAB_CIRCLEBTN, -1);
        consider(GRAB_LOCKBTN, -1);
        consider(GRAB_SNAPBTN, -1);
        consider(GRAB_MOVE, -1); // centre dot
    }
    if (g.mode == GRAB_NONE) {
        float bestT = 1e9f;
        for (int wi = 0; wi < (int)g_windows.size(); ++wi) {
            const Window& w = g_windows[wi];
            XMVECTOR n = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&w.rot));
            float den = XMVectorGetX(XMVector3Dot(d, n));
            if (fabsf(den) < 1e-4f) continue;
            float t = XMVectorGetX(XMVector3Dot(V(w.pos) - o, n)) / den;
            if (t <= 0 || t >= bestT) continue;
            XMFLOAT3 l = WorldToWin(w, o + d * t);
            if (fabsf(l.x) < w.hw && fabsf(l.y) < w.hh) { bestT = t; g = { GRAB_MOVE, wi, -1 }; gl = t; }
        }
    }
    // The panel is solid: if the ray reaches it first, that's what it's on. Except the selected window's
    // handles and buttons: they're drawn on top of the panel, so pointing at one gets it, even with the
    // panel nearer (otherwise you'd see the button but couldn't press it).
    float gt; XMFLOAT3 l;
    if (onHandle) {   // ...unless the handle itself is behind the panel (hidden there, see UpdateGuide)
        float ht; XMFLOAT3 hl;
        if (GuideRay(o, XMVector3Normalize(hp - o), ht, hl) && ht < gl) onHandle = false;
    }
    if (!onHandle && GuideRay(o, d, gt, l) && (g.mode == GRAB_NONE || gt < gl)) {
        int idx = -1;
        GrabMode gm = GuideButtonAt(l.x, l.y, &idx);
        g = { gm, -1, idx };
        gl = gt;
    }
    if (len) *len = gl;
    return g;
}
static void UpdateGazeTarget() {
    g_gazeTarget = GazeTarget();
    if (!g_gazeValid || !g_edit) return;
    g_gazeTarget = RayTarget(V(g_gazeOrigin), V(g_gazeDir), 3.0f, nullptr);
}
// Controllers get a laser when there's no eye tracking (with it, look + trigger works instead).
static bool LaserMode() { return g_edit && Now() - g_gazeLastValid > 2.0; }
// Is a ray (eye gaze, or a controller laser) on this target? For highlighting.
static bool RayOn(GrabMode m, int wi, int k) {
    if (g_gazeTarget.win == wi && g_gazeTarget.mode == m && g_gazeTarget.corner == k) return true;
    for (int i = 2; i < 4; ++i)
        if (LaserMode() && g_hand[i].active && g_hand[i].ray.win == wi && g_hand[i].ray.mode == m && g_hand[i].ray.corner == k) return true;
    return false;
}
static bool RayOnWin(int wi) {
    if (g_gazeTarget.win == wi) return true;
    for (int i = 2; i < 4; ++i) if (LaserMode() && g_hand[i].active && g_hand[i].ray.win == wi) return true;
    return false;
}
// Laser grab: the point on the ray where it meets the window's plane (for corners, points,
// tracing), or at the grab distance if the ray misses the plane.
static XMVECTOR LaserPlanePoint(const HandState& h, const Window& w) {
    XMVECTOR o = V(h.aimO), d = V(h.aimD);
    XMVECTOR n = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&w.rot));
    float den = XMVectorGetX(XMVector3Dot(d, n));
    if (fabsf(den) > 0.05f) {
        float t = XMVectorGetX(XMVector3Dot(V(w.pos) - o, n)) / den;
        if (t > 0.05f && t < 20) return o + d * t;
    }
    return o + d * h.grabDist;
}
// Laser on the depth dot: the point on the dot's front-back line closest to the ray.
static XMVECTOR LaserDepthPoint(const HandState& h, const Window& w) {
    XMVECTOR o = V(h.aimO), d = V(h.aimD);
    XMVECTOR a = WinToWorld(w, w.hw + DepthOut(w), 0, 0);
    XMVECTOR u = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&w.rot));
    // closest points between lines a + s u and o + t d
    float b = XMVectorGetX(XMVector3Dot(u, d));
    XMVECTOR r = a - o;
    float dd = XMVectorGetX(XMVector3Dot(u, r)), e = XMVectorGetX(XMVector3Dot(d, r));
    float den = 1 - b * b;
    float s = den > 1e-4f ? (b * e - dd) / den : 0;
    return a + u * s;
}

static const char* GrabName(GrabMode m) {
    switch (m) {
    case GRAB_MOVE: return "move"; case GRAB_CORNER: return "corner"; case GRAB_RADIUS: return "radius";
    case GRAB_DEPTH: return "depth"; case GRAB_POINT: return "shape point"; case GRAB_ADDPT: return "add point";
    case GRAB_TYPE: return "type button"; case GRAB_DRAWBTN: return "draw button"; case GRAB_TRACE: return "trace";
    case GRAB_KEEPBTN: return "keep-box button"; case GRAB_CIRCLEBTN: return "circle button";
    case GRAB_LOCKBTN: return "lock button"; case GRAB_SNAPBTN: return "snap button"; case GRAB_FACESBTN: return "box faces button";
    case GRAB_GUIDE: return "guide panel"; case GRAB_ALL: return "all windows"; case GRAB_PRESET: return "preset button";
    default: return "none";
    }
}
// Other hands holding points of window wi let go (its points were added, removed or replaced).
static void DropPointGrabs(int wi, const HandState* except) {
    for (auto& o : g_hand)
        if (&o != except && o.win == wi && (o.mode == GRAB_POINT || o.mode == GRAB_TRACE)) { o.mode = GRAB_NONE; o.win = -1; }
}
// Type button: rounded rectangle <-> free shape.
static void ToggleShape(int wi) {
    Window& w = g_windows[wi];
    DropPointGrabs(wi, nullptr);
    if (IsShape(w)) { MakeRect(w); if (g_drawArmed == wi) g_drawArmed = -1; }
    else MakeShape(w);
    Log("Window %d is now a %s\n", wi, IsShape(w) ? "free shape (drag the orange points, or tap the pencil to draw)" : "rectangle");
    SaveWindows();
}

// Circle button: rectangle <-> circle (an ellipse filling the window).
static void ToggleCircle(int wi) {
    Window& w = g_windows[wi];
    w.ellipse = !w.ellipse;
    Log("Window %d is now a %s\n", wi, w.ellipse ? "circle" : "rectangle");
    SaveWindows();
}
// Keep-box button: normal box <-> keep box.
static void ToggleKeep(int wi) {
    Window& w = g_windows[wi];
    w.keep = !w.keep;
    Log("Window %d is now a %s\n", wi, w.keep ? "keep box (passthrough, but the game's surfaces inside the box stay)"
                                                : "normal window (the game inside it turns into passthrough)");
    SaveWindows();
}

// Box faces button: the next face mode (which faces of the box are passthrough).
static void CycleBoxMode(int wi) {
    Window& w = g_windows[wi];
    w.boxMode = (w.boxMode + 1) % 4;
    Log("Window %d box faces: %s\n", wi, kBoxModeName[w.boxMode]);
    SaveWindows();
}

// Lock button: while locked, moving the window keeps its angle (it can't be tilted by accident).
static void ToggleLock(int wi) {
    Window& w = g_windows[wi];
    w.lock = !w.lock;
    Log("Window %d angle %s\n", wi, w.lock ? "locked: moving it keeps its angle (the snap button still turns it)" : "unlocked");
    SaveWindows();
}
// Snap button: square the window up with the game world's axes, facing you. Each press goes one step
// further: upright facing forward / back (Z), upright facing the side (X), lying level (Y), then round
// again. The first press on a window that isn't square yet picks whichever of the three is nearest,
// so a window that got tilted while pushing it out just stands back up.
static XMVECTOR SnapRot(const Window& w, int k) {
    static const XMVECTOR kAxis[3] = { XMVectorSet(0, 0, 1, 0), XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) };
    XMVECTOR q = XMLoadFloat4(&w.rot);
    XMVECTOR n = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q), u = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);
    // Face you (the window's front, +Z, points towards your head along that axis).
    float toHead = XMVectorGetX(XMVector3Dot(g_head - V(w.pos), kAxis[k]));
    float s = fabsf(toHead) > 1e-3f ? toHead : XMVectorGetX(XMVector3Dot(n, kAxis[k]));
    XMVECTOR nt = kAxis[k] * (s < 0 ? -1.0f : 1.0f);
    XMVECTOR up = kAxis[2];
    if (k == 2) {
        // Lying level: its top edge points along the level axis (X or Z) nearest to where it points now.
        // Upright now: it tips back, so its top edge goes away from you.
        XMVECTOR hz = u - kAxis[2] * XMVectorGetX(XMVector3Dot(u, kAxis[2]));
        if (XMVectorGetX(XMVector3Length(hz)) < 0.3f) hz = -(n - kAxis[2] * XMVectorGetX(XMVector3Dot(n, kAxis[2])));
        float hx = XMVectorGetX(hz), hzz = XMVectorGetZ(hz);
        up = fabsf(hx) > fabsf(hzz) ? XMVectorSet(hx < 0 ? -1.0f : 1.0f, 0, 0, 0) : XMVectorSet(0, 0, hzz < 0 ? -1.0f : 1.0f, 0);
    }
    XMVECTOR r = XMVector3Cross(up, nt);   // local x = y cross z
    XMMATRIX m(r, up, nt, XMVectorSet(0, 0, 0, 1));
    return XMQuaternionNormalize(XMQuaternionRotationMatrix(m));
}
static void SnapWindow(int wi) {
    Window& w = g_windows[wi];
    static const char* kName[3] = { "upright, facing forward / back (Z)", "upright, facing the side (X)", "level (Y)" };
    XMVECTOR q = XMLoadFloat4(&w.rot);
    XMVECTOR n = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
    // Nearest of the three to the window's facing now.
    float az = fabsf(XMVectorGetZ(n)), ax = fabsf(XMVectorGetX(n)), ay = fabsf(XMVectorGetY(n));
    int k = az >= ax && az >= ay ? 0 : ax >= ay ? 1 : 2;
    // Already square there (within ~1 degree): next one.
    XMVECTOR t = SnapRot(w, k);
    if (fabsf(XMVectorGetX(XMVector4Dot(q, t))) > 0.99996f) { k = (k + 1) % 3; t = SnapRot(w, k); }
    XMStoreFloat4(&w.rot, t);
    for (auto& h : g_hand) if (h.win == wi && h.mode == GRAB_MOVE) { h.mode = GRAB_NONE; h.win = -1; }   // re-grab from here
    Log("Window %d snapped %s\n", wi, kName[k]);
    SaveWindows();
}

// Grabbing the guide panel (move it), or holding its "move all windows" button (every window moves
// with the hand, as one: they keep their places relative to each other).
static float YawOf(const XMFLOAT4& q, bool* level) {
    XMVECTOR f = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), XMLoadFloat4(&q));
    float fx = XMVectorGetX(f), fz = XMVectorGetZ(f);
    if (level) *level = fx * fx + fz * fz > 0.25f;   // pointing within ~60 degrees of level
    return atan2f(-fx, -fz);
}
static void SnapshotAll() {
    g_allStart.clear();
    for (auto& w : g_windows) g_allStart.push_back({ w.pos, w.rot });
}
static void BeginGuideGrab(HandState& h, GrabMode gm, bool indirect, bool laser, int idx = -1) {
    for (auto& o : g_hand) if (&o != &h && o.mode == gm) return;   // one hand at a time
    if (gm == GRAB_PRESET) {
        if (idx < 0 || idx > 2) return;
        // Tap: load it. Hold: save the current windows there (UpdateGuideGrab).
        h.mode = gm; h.win = -1; h.corner = idx; h.indirect = indirect; h.laser = laser;
        g_presetHeld = idx; g_presetHoldStart = Now(); g_presetHoldSaved = false;
        return;
    }
    h.mode = gm; h.win = -1; h.corner = -1; h.indirect = indirect;
    h.winStartPos = g_guidePos; h.handStartRot = h.grabRot;
    if (gm == GRAB_GUIDE) {
        g_guidePinned = true;
        if (laser) {
            // Held on the laser at the distance it was grabbed.
            float t; XMFLOAT3 l;
            if (!GuideRay(V(h.aimO), V(h.aimD), t, l)) t = Dist(V(h.aimO), V(g_guidePos));
            h.laser = true; h.grabDist = t;
            XMStoreFloat3(&h.laserStart, V(h.aimO) + V(h.aimD) * t);
            XMStoreFloat3(&h.targetStart, V(g_guidePos) - (V(h.aimO) + V(h.aimD) * t));
        } else if (indirect) {
            h.gain = std::clamp(Dist(g_head, V(g_guidePos)) / std::max(0.2f, Dist(g_head, V(h.pinchPt))), 1.0f, 4.0f);
        } else {
            XMStoreFloat3(&h.targetStart, V(g_guidePos) - V(h.pinchPt));
        }
        Log("Guide panel grabbed\n");
    } else {
        SnapshotAll();
        Log("Moving all %d windows together\n", (int)g_windows.size());
    }
}
static void UpdateGuideGrab(HandState& h) {
    if (h.mode == GRAB_PRESET) {
        if (!g_presetHoldSaved && Now() - g_presetHoldStart >= kPresetHoldSave) {
            g_presetHoldSaved = true;
            SavePreset(h.corner, "held its button");
        }
        return;
    }
    // Nothing moves until the hand / controller (or laser point) has clearly moved (as for windows).
    if (!h.dragging) {
        XMVECTOR rayPt = V(h.aimO) + V(h.aimD) * h.grabDist;
        bool go = Dist(V(h.pinchPt), V(h.pinchStartPt)) >= (h.isCtl ? 0.01f : 0.012f);
        if (h.laser) go = go || Dist(rayPt, V(h.laserStart)) > 0.03f;
        if (!go) return;
        h.dragging = true;
        h.pinchStartPt = h.pinchPt; h.handStartRot = h.grabRot; h.winStartPos = g_guidePos;
        if (h.mode == GRAB_GUIDE && h.laser) XMStoreFloat3(&h.targetStart, V(g_guidePos) - rayPt);
        else if (h.mode == GRAB_GUIDE && !h.indirect) XMStoreFloat3(&h.targetStart, V(g_guidePos) - V(h.pinchPt));
        if (h.mode == GRAB_ALL) SnapshotAll();
    }
    XMVECTOR delta = V(h.pinchPt) - V(h.pinchStartPt);
    if (h.mode == GRAB_GUIDE) {
        XMVECTOR pos = h.laser ? V(h.aimO) + V(h.aimD) * h.grabDist + V(h.targetStart)
                     : h.indirect ? V(h.winStartPos) + delta * h.gain
                     : V(h.pinchPt) + V(h.targetStart);
        XMStoreFloat3(&g_guidePos, pos);
        g_guideTarget = g_guidePos;
        return;
    }
    if (g_allStart.size() != g_windows.size()) { h.mode = GRAB_NONE; return; }   // a window was added / removed
    // All windows: moved with the hand, and turned with it about the vertical (no tilt, so the
    // whole set stays level). Turning about the point where the pinch started.
    static float yaw = 0;
    bool l0, l1;
    float y0 = YawOf(h.handStartRot, &l0), y1 = YawOf(h.grabRot, &l1);
    if (!l0) yaw = 0;
    else if (l1) { yaw = y1 - y0; if (yaw > XM_PI) yaw -= XM_2PI; if (yaw < -XM_PI) yaw += XM_2PI; }
    XMMATRIX R = XMMatrixRotationY(yaw);
    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw), pivot = V(h.pinchStartPt);
    for (size_t i = 0; i < g_windows.size(); ++i) {
        Window& w = g_windows[i];
        XMVECTOR p0 = V(g_allStart[i].first);
        XMStoreFloat3(&w.pos, pivot + XMVector3TransformNormal(p0 - pivot, R) + delta);
        XMStoreFloat4(&w.rot, XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&g_allStart[i].second), qYaw)));
    }
}
static void EndGuideGrab(HandState& h) {
    if (h.mode == GRAB_ALL) { Log("All windows moved\n"); SaveWindows(); }
    if (h.mode == GRAB_PRESET) {
        if (!g_presetHoldSaved && h.corner >= 0 && h.corner < 3) LoadPreset(h.corner, "tapped its button");
        g_presetHeld = -1;
    }
    h.mode = GRAB_NONE; h.win = -1; h.corner = -1; h.laser = false;
}

static void BeginGrab(HandState& h) {
    XMVECTOR p = V(h.pinchPt);
    float best = 1e9f;
    h.mode = GRAB_NONE; h.win = -1; h.corner = -1; h.indirect = false; h.newPoint = false; h.laser = false; h.dragging = false;
    h.dragOffset = {};
    // Controllers without eye tracking point with a laser; with eye tracking, look + trigger.
    const bool useLaser = h.isCtl && LaserMode();
    const GazeTarget gt = useLaser ? h.ray : g_gazeTarget;
    // 0. Draw armed: this pinch traces the new outline (unless it's on the draw button: cancel).
    if (g_drawArmed >= 0 && g_drawArmed < (int)g_windows.size() && IsShape(g_windows[g_drawArmed])) {
        const Window& w = g_windows[g_drawArmed];
        bool onButton = Dist(p, TargetPoint(w, GRAB_DRAWBTN, -1)) < kButtonHit ||
                        (gt.win == g_drawArmed && gt.mode == GRAB_DRAWBTN);
        if (onButton) {
            Log("Drawing cancelled\n");
            g_drawArmed = -1;
            h.mode = GRAB_DRAWBTN; h.win = g_selected;   // inert until release
            return;
        }
        for (auto& o : g_hand) if (&o != &h && o.mode == GRAB_TRACE) return;   // one tracer at a time
        h.mode = GRAB_TRACE; h.win = g_drawArmed;
        g_trace.clear();
        // Laser: start at least 50 cm out along it, so the drawing surface (which faces your head) is
        // well clear of the controller and the laser always meets it at a good angle.
        if (useLaser) { h.laser = true; h.grabDist = std::max(0.5f, Dist(V(h.aimO), V(w.pos))); p = V(h.aimO) + V(h.aimD) * h.grabDist; }
        // Draw on a plane through the start point, facing you as you are now (not the window's old
        // plane, which may be side-on if you've turned). The window moves onto it when it's done.
        g_tracePlane = TracePlane(p);
        XMFLOAT3 l = WorldToWin(g_tracePlane, p);
        g_tracePrev = { l.x, l.y }; g_tracePrevOk = true;
        g_trace.push_back({ l.x, l.y });
        Log("Tracing a new outline for window %d\n", h.win);
        return;
    }
    // 1. Direct touch: hand at a dot. Dots exist only on the selected window. Checked before the guide
    // panel: the dots are drawn on top of it, so touching one gets the dot, not the panel behind it.
    if (g_selected >= 0 && g_selected < (int)g_windows.size()) {
        int wi = g_selected;
        Window& w = g_windows[wi];
        auto at = [&](GrabMode m, int k, float hit) {
            float d = Dist(p, TargetPoint(w, m, k));
            if (d < hit && d < best) { best = d; h.mode = m; h.win = wi; h.corner = k; }
        };
        if (IsShape(w)) {
            for (int k = 0; k < (int)w.pts.size(); ++k) at(GRAB_ADDPT, k, kMidHit);
            for (int k = 0; k < (int)w.pts.size(); ++k) at(GRAB_POINT, k, kPointHit);
            at(GRAB_DRAWBTN, -1, kButtonHit);
        } else {
            if (!w.ellipse) for (int k = 0; k < 4; ++k) at(GRAB_RADIUS, k, kRadiusHit);
            for (int k = 0; k < 4; ++k) at(GRAB_CORNER, k, kCornerHit);
        }
        at(GRAB_TYPE, -1, kButtonHit);
        at(GRAB_KEEPBTN, -1, kButtonHit);
        if (w.hd > 0) at(GRAB_FACESBTN, -1, kButtonHit);
        if (!IsShape(w)) at(GRAB_CIRCLEBTN, -1, kButtonHit);
        at(GRAB_LOCKBTN, -1, kButtonHit);
        at(GRAB_SNAPBTN, -1, kButtonHit);
        float dd = Dist(p, DepthHandle(w));
        if (dd < kDepthHit && dd < best) { best = dd; h.mode = GRAB_DEPTH; h.win = wi; }
        float d = Dist(p, WinToWorld(w, 0, 0));
        if (d < kCenterHit && d < best) { best = d; h.mode = GRAB_MOVE; h.win = wi; }
    }
    // 1b. Hand (or controller tip) on the guide panel: move it, or its "move all windows" button.
    if (h.mode == GRAB_NONE) { int pi = -1; if (GrabMode gm = GuideTouch(p, &pi); gm != GRAB_NONE) { BeginGuideGrab(h, gm, false, false, pi); return; } }
    // 2. Eyes (or the controller's laser): whatever you're looking or pointing at.
    if (h.mode == GRAB_NONE && (gt.mode == GRAB_GUIDE || gt.mode == GRAB_ALL || gt.mode == GRAB_PRESET)) { BeginGuideGrab(h, gt.mode, true, useLaser, gt.corner); return; }
    if (h.mode == GRAB_NONE && gt.mode != GRAB_NONE && gt.win >= 0 && gt.win < (int)g_windows.size()) {
        const Window& w = g_windows[gt.win];
        h.mode = gt.mode; h.win = gt.win; h.corner = gt.corner;
        h.indirect = true;
        XMVECTOR tp = TargetPoint(w, h.mode, h.corner);
        XMStoreFloat3(&h.targetStart, tp);
        h.winStartPos = w.pos; h.winStartRot = w.rot;
        h.handStartRot = h.grabRot;
        // Far targets move further per cm of hand motion, so you don't have to reach.
        float dt = Dist(g_head, tp), dh = std::max(0.2f, Dist(g_head, p));
        h.gain = std::clamp(dt / dh, 1.0f, 4.0f);
        if (useLaser) {
            // The grabbed point stays on the ray, this far along it (thumbstick pushes / pulls it).
            h.laser = true;
            h.grabDist = std::max(0.1f, h.rayLen > 0 ? h.rayLen : Dist(V(h.aimO), tp));
            XMStoreFloat3(&h.laserStart, V(h.aimO) + V(h.aimD) * h.grabDist);
            if (h.mode == GRAB_MOVE) {
                // Hold the window rigidly, as if on a stick: it moves and tilts with the controller.
                XMStoreFloat4x4(&h.rel, WinMatrix(w) * XMMatrixInverse(nullptr, HandGrabMatrix(h)));
                h.push = h.turn = 0;
            }
            Log("Laser grab: window %d %s, %.0f cm away\n", h.win, GrabName(h.mode), h.grabDist * 100);
        } else {
            Log("Eye grab (%s): window %d %s (gain %.1f)\n", h.isCtl ? "controller" : "hand", h.win, GrabName(h.mode), h.gain);
        }
    }
    // Buttons act on the pinch. An edge dot becomes a new point, grabbed straight away.
    if (h.mode == GRAB_TYPE && h.win >= 0) ToggleShape(h.win);
    if (h.mode == GRAB_KEEPBTN && h.win >= 0) ToggleKeep(h.win);
    if (h.mode == GRAB_FACESBTN && h.win >= 0) CycleBoxMode(h.win);
    if (h.mode == GRAB_CIRCLEBTN && h.win >= 0) ToggleCircle(h.win);
    if (h.mode == GRAB_LOCKBTN && h.win >= 0) ToggleLock(h.win);
    if (h.mode == GRAB_SNAPBTN && h.win >= 0) SnapWindow(h.win);
    if (h.mode == GRAB_DRAWBTN && h.win >= 0) {
        g_drawArmed = h.win;
        Log("Draw: pinch and trace the new outline of window %d (release to finish)\n", h.win);
    }
    if (h.mode == GRAB_ADDPT && h.win >= 0) {
        Window& w = g_windows[h.win];
        if ((int)w.pts.size() >= kMaxShapePts) {
            Log("Shape has the most points (%d) already\n", kMaxShapePts);
            h.mode = GRAB_NONE;
        } else {
            DropPointGrabs(h.win, &h);
            w.pts.insert(w.pts.begin() + h.corner + 1, MidLocal(w, h.corner));
            h.mode = GRAB_POINT; h.corner += 1; h.newPoint = true;
        }
    }
    // 3. Direct touch: hand on a window surface.
    if (h.mode == GRAB_NONE) {
        for (int wi = 0; wi < (int)g_windows.size(); ++wi) {
            Window& w = g_windows[wi];
            XMFLOAT3 l = WorldToWin(w, p);
            if (fabsf(l.x) < w.hw + 0.03f && fabsf(l.y) < w.hh + 0.03f && fabsf(l.z) < w.hd + 0.06f) {
                float d = std::max(0.0f, fabsf(l.z) - w.hd);
                if (d < best) { best = d; h.mode = GRAB_MOVE; h.win = wi; }
            }
        }
    }
    if (h.mode == GRAB_MOVE && !h.indirect) {
        XMMATRIX rel = WinMatrix(g_windows[h.win]) * XMMatrixInverse(nullptr, HandGrabMatrix(h));
        XMStoreFloat4x4(&h.rel, rel);
    }
    if (h.win >= 0 && h.win != g_selected) { g_selected = h.win; Log("Window %d selected\n", g_selected); }
}

static void DeleteWindow(int wi) {
    if (wi < 0 || wi >= (int)g_windows.size()) return;
    g_windows.erase(g_windows.begin() + wi);
    for (auto& o : g_hand) {
        if (o.win == wi) { o.mode = GRAB_NONE; o.win = -1; }
        else if (o.win > wi) --o.win;
    }
    if (g_selected == wi) g_selected = g_windows.empty() ? -1 : (int)g_windows.size() - 1;
    else if (g_selected > wi) --g_selected;
    if (g_drawArmed == wi) g_drawArmed = -1;
    else if (g_drawArmed > wi) --g_drawArmed;
    Log("Window deleted\n");
    SaveWindows();
}

// The traced outline becomes the window's shape (or, if it's unusable, drawing stays armed).
static void FinishTrace(HandState& h, Window& w) {
    std::vector<XMFLOAT2> s = TraceToShape(g_trace);
    if (s.empty()) Log("Traced outline too small or too short (%d samples): shape unchanged, still armed\n", (int)g_trace.size());
    else {
        DropPointGrabs(h.win, &h);
        w.pos = g_tracePlane.pos; w.rot = g_tracePlane.rot;   // now facing you, where you drew it
        w.pts = s;
        RecentreShape(w);
        g_drawArmed = -1;
        Log("Window %d outline drawn: %d points%s\n", h.win, (int)w.pts.size(), TraceAtStart(g_trace) ? " (closed at the start dot)" : "");
    }
    g_trace.clear();
}

static void UpdateGrab(HandState& h) {
    if (h.mode == GRAB_GUIDE || h.mode == GRAB_ALL || h.mode == GRAB_PRESET) { UpdateGuideGrab(h); return; }
    if (h.win < 0 || h.win >= (int)g_windows.size()) { h.mode = GRAB_NONE; return; }
    Window& w = g_windows[h.win];
    // Laser on the depth dot: no dragging (it's hard to aim along the depth line). The
    // thumbstick sets the depth while the dot is held.
    if (h.laser && h.mode == GRAB_DEPTH) return;
    XMVECTOR rayPt = V(h.aimO) + V(h.aimD) * h.grabDist;
    const bool onPlane = h.mode == GRAB_CORNER || h.mode == GRAB_RADIUS || h.mode == GRAB_POINT || h.mode == GRAB_TRACE;
    // Grabbing only selects: nothing moves until the hand / controller has clearly moved, so
    // pinching or pulling the trigger (which nudges the controller) never shifts the window.
    // Then it starts from there, without a jump. (Tracing starts straight away.)
    if (!h.dragging && h.mode != GRAB_TRACE) {
        bool go = Dist(V(h.pinchPt), V(h.pinchStartPt)) >= (h.isCtl ? 0.01f : 0.012f);
        if (h.isCtl) {   // controllers are steady enough to also start on a turn of ~3 degrees
            XMVECTOR dq = XMQuaternionMultiply(XMQuaternionInverse(XMLoadFloat4(&h.handStartRot)), XMLoadFloat4(&h.grabRot));
            go = go || fabsf(XMVectorGetW(XMQuaternionNormalize(dq))) < cosf(XMConvertToRadians(1.5f));
        }
        if (h.laser) go = go || Dist(rayPt, V(h.laserStart)) > 0.03f || h.push != 0 || h.turn != 0;
        if (!go) return;
        h.dragging = true;
        h.pinchStartPt = h.pinchPt;
        h.handStartRot = h.grabRot;
        h.winStartPos = w.pos; h.winStartRot = w.rot;
        h.winStartHw = w.hw; h.winStartHh = w.hh; h.winStartHd = w.hd;
        XMStoreFloat3(&h.laserStart, rayPt);
        if (h.mode == GRAB_MOVE && h.laser) {
            // as held now (UpdateGrab: turn * rel * push * controller = the window)
            XMMATRIX hold = XMMatrixTranslation(0, 0, -h.push) * HandGrabMatrix(h);
            XMStoreFloat4x4(&h.rel, XMMatrixRotationY(-h.turn) * WinMatrix(w) * XMMatrixInverse(nullptr, hold));
        } else if (h.mode == GRAB_MOVE && !h.indirect) {
            XMStoreFloat4x4(&h.rel, WinMatrix(w) * XMMatrixInverse(nullptr, HandGrabMatrix(h)));
        }
        // Handles keep their offset from the hand / ray, so they don't snap to it.
        XMVECTOR from = h.laser && onPlane ? LaserPlanePoint(h, w) : V(h.pinchPt);
        XMStoreFloat3(&h.dragOffset, h.indirect && !h.laser ? XMVectorZero() : TargetPoint(w, h.mode, h.corner) - from);
    }
    XMVECTOR handDelta = V(h.pinchPt) - V(h.pinchStartPt);
    // Direct: the dot follows the hand. Eye grab: the dot moves by the hand's motion (scaled).
    XMVECTOR p = h.indirect ? V(h.targetStart) + handDelta * h.gain : V(h.pinchPt) + V(h.dragOffset);
    // Laser: the grabbed point rides on the ray. Corners, points and tracing follow where the ray
    // meets the window's plane; the depth dot follows the ray along its front-back line.
    if (h.laser) {
        if (h.mode == GRAB_DEPTH) p = LaserDepthPoint(h, w);
        else if (onPlane) p = LaserPlanePoint(h, w) + (h.mode == GRAB_TRACE ? XMVectorZero() : V(h.dragOffset));
        else p = rayPt;
    }
    // Held rigidly (hand or laser): the window goes where the hand's motion carries it. Locked windows
    // keep the angle they had when the drag began, and only follow the grabbed point (where the hand or
    // laser took hold), so turning your hand or controller moves the window but never tilts it.
    auto placeRigid = [&](FXMMATRIX m, XMVECTOR grabStart) {
        XMVECTOR s, r, t;
        if (!XMMatrixDecompose(&s, &r, &t, m)) return;
        if (!w.lock) { XMStoreFloat3(&w.pos, t); XMStoreFloat4(&w.rot, XMQuaternionNormalize(r)); return; }
        XMMATRIX start = XMMatrixRotationQuaternion(XMLoadFloat4(&h.winStartRot)) *
                         XMMatrixTranslation(h.winStartPos.x, h.winStartPos.y, h.winStartPos.z);
        XMVECTOR local = XMVector3TransformCoord(grabStart, XMMatrixInverse(nullptr, start));
        XMVECTOR now = XMVector3TransformCoord(local, m);
        XMStoreFloat3(&w.pos, V(h.winStartPos) + (now - grabStart));
        w.rot = h.winStartRot;
    };
    if (h.mode == GRAB_MOVE && h.laser) {
        // Held rigidly at a distance (like a direct grab on a long stick), plus the thumbstick's
        // push / pull along the controller's aim and turn about the window's own vertical axis.
        XMMATRIX m = XMMatrixRotationY(h.turn) * XMLoadFloat4x4(&h.rel) * XMMatrixTranslation(0, 0, -h.push) * HandGrabMatrix(h);
        placeRigid(m, V(h.laserStart));
    } else if (h.mode == GRAB_MOVE && h.indirect) {
        // Move by scaled hand motion; tilt/rotate by the hand's rotation since the pinch (not when locked).
        if (!w.lock) {
            XMVECTOR q0 = XMLoadFloat4(&h.handStartRot), q1 = XMLoadFloat4(&h.grabRot);
            XMVECTOR delta = XMQuaternionMultiply(XMQuaternionInverse(q0), q1);
            XMStoreFloat4(&w.rot, XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&h.winStartRot), delta)));
        }
        XMStoreFloat3(&w.pos, V(h.winStartPos) + handDelta * h.gain);
    } else if (h.mode == GRAB_MOVE) {
        placeRigid(XMLoadFloat4x4(&h.rel) * HandGrabMatrix(h), V(h.pinchStartPt));
    } else if (h.mode == GRAB_CORNER) {
        int k = h.corner;
        XMFLOAT3 l = WorldToWin(w, p);
        float ox = -kCS[k][0] * w.hw, oy = -kCS[k][1] * w.hh; // opposite corner stays put
        float nx = (l.x + ox) * 0.5f, ny = (l.y + oy) * 0.5f;
        XMVECTOR c = WinToWorld(w, nx, ny);
        w.hw = std::max(0.005f, fabsf(l.x - ox) * 0.5f);
        w.hh = std::max(0.005f, fabsf(l.y - oy) * 0.5f);
        XMStoreFloat3(&w.pos, c);
        ClampRadii(w);
    } else if (h.mode == GRAB_RADIUS) {
        int k = h.corner;
        XMFLOAT3 l = WorldToWin(w, p);
        float u = ((w.hw - kCS[k][0] * l.x) + (w.hh - kCS[k][1] * l.y)) * 0.5f; // distance in from the corner
        float r = std::clamp((u - kRadiusHandleInset) / kArcK, 0.0f, std::min(w.hw, w.hh));
        w.radius[k] = r;
    } else if (h.mode == GRAB_DEPTH) {
        // The box grows evenly in front of and behind the window, by however far the handle moves towards
        // you (or away: shallower). The handle sits on the centre line, so it's the move that counts, not
        // where it is. Pushed back to ~1 cm: flat.
        float z = h.winStartHd + WorldToWin(w, p).z;
        w.hd = z < 0.01f ? 0 : std::min(z, kMaxHalfDepth);
    } else if (h.mode == GRAB_POINT && h.corner >= 0 && h.corner < (int)w.pts.size()) {
        XMFLOAT3 l = WorldToWin(w, p);
        w.pts[h.corner] = { l.x, l.y };
        UpdateShapeBounds(w);
    } else if (h.mode == GRAB_TRACE) {
        // The pinch point (or where the laser meets the drawing surface), flattened onto that surface.
        // A point every ~8 mm of movement. Skipped: the laser grazing or missing the surface, and
        // sudden jumps (tracking glitches), which would otherwise leave a stray point far away.
        XMVECTOR at = V(h.pinchPt);
        bool good = true;
        if (h.laser) {
            XMVECTOR o = V(h.aimO), d = V(h.aimD);
            XMVECTOR n = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&g_tracePlane.rot));
            float den = XMVectorGetX(XMVector3Dot(d, n));
            float t = fabsf(den) > 0.25f ? XMVectorGetX(XMVector3Dot(V(g_tracePlane.pos) - o, n)) / den : -1;
            good = t > 0.05f && t < 4.0f;
            if (good) at = o + d * t;
        }
        XMFLOAT3 l = WorldToWin(g_tracePlane, at);
        // jump: compared with last frame (not the last kept point, so one miss can't block the rest)
        float jx = l.x - g_tracePrev.x, jy = l.y - g_tracePrev.y;
        bool jumped = !g_tracePrevOk || jx * jx + jy * jy > 0.15f * 0.15f;
        g_tracePrev = { l.x, l.y }; g_tracePrevOk = good;
        if (jumped) good = false;
        const XMFLOAT2& last = g_trace.back();
        float dx = l.x - last.x, dy = l.y - last.y;
        if (good && dx * dx + dy * dy > 0.008f * 0.008f && g_trace.size() < 600) g_trace.push_back({ l.x, l.y });
        // Back on the yellow start dot: the outline is finished, no need to let go first.
        if (TraceAtStart(g_trace)) {
            FinishTrace(h, w);
            SaveWindows();
            h.mode = GRAB_DRAWBTN;   // inert until released
        }
    }
}

static void EndGrab(HandState& h, double now) {
    if (h.mode == GRAB_GUIDE || h.mode == GRAB_ALL || h.mode == GRAB_PRESET) { EndGuideGrab(h); return; }
    if (h.win >= 0 && h.win < (int)g_windows.size()) {
        Window& w = g_windows[h.win];
        bool tap = (now - h.pinchStart) < 0.35 && !h.dragging && Dist(V(h.pinchPt), V(h.pinchStartPt)) < 0.012f;
        // Shrunk to almost nothing (under 2 cm wide or tall) in this drag: delete it. Only when this drag
        // made it smaller, so touching a corner of a window that's already short never deletes it.
        bool shrunk = h.dragging && (w.hw < h.winStartHw - 1e-4f || w.hh < h.winStartHh - 1e-4f);
        if (h.mode == GRAB_CORNER && shrunk && (w.hw < 0.01f || w.hh < 0.01f)) {
            int wi = h.win;
            Log("Window %d shrunk to %.1f x %.1f cm: deleting it\n", wi, w.hw * 200, w.hh * 200);
            h.mode = GRAB_NONE; h.win = -1;
            DeleteWindow(wi);
            return;
        }
        if (h.mode == GRAB_DEPTH) Log("Window %d depth %.0f cm%s\n", h.win, w.hd * 200, w.hd > 0 ? "" : " (flat)");
        if (h.mode == GRAB_POINT && IsShape(w)) {
            // A quick tap on a point removes it (a shape keeps at least 3).
            if (tap && !h.newPoint && w.pts.size() > 3 && h.corner >= 0 && h.corner < (int)w.pts.size()) {
                DropPointGrabs(h.win, &h);
                w.pts.erase(w.pts.begin() + h.corner);
                Log("Shape point removed (%d left)\n", (int)w.pts.size());
            }
            RecentreShape(w);
        }
        if (h.mode == GRAB_TRACE) FinishTrace(h, w);
        SaveWindows();
    }
    h.mode = GRAB_NONE; h.win = -1; h.corner = -1; h.newPoint = false;
}

// Locate hands, run gestures and grabs, and build all cutout/UI geometry for this frame.
// ================================================================ edit-mode guide panel
// While edit mode is on, a solid panel floats ahead of the driver (a little above eye level so it
// doesn't hide the wheel) with the controls, the selected window's details and the depth status.
// The text is drawn with GDI into a CPU image a few times a second, uploaded in DrawMask, and
// shown on a quad. The panel stays put until it drifts out of view, then glides back in front.

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
#else   // UTF-8 -> UTF-32 (wchar_t is 32 bits)
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        uint32_t cp = len == 1 ? c : c & (0x3F >> (len - 1));
        for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        w += (wchar_t)cp;
        i += len;
    }
    return w;
#endif
}
// Wide strings: %ls for a wchar_t* (on both compilers).
static std::wstring Fmt(const wchar_t* f, ...) {
    wchar_t b[512];
    va_list a; va_start(a, f);
#ifdef _WIN32
    _vsnwprintf_s(b, _countof(b), _TRUNCATE, f, a);
#else
    if (vswprintf(b, _countof(b), f, a) < 0) b[_countof(b) - 1] = 0;
#endif
    va_end(a);
    return b;
}
// A distance in short words: "55 cm", "2.4 m", "infinite".
static std::wstring ShortDist(float m) {
    if (!std::isfinite(m) || m > 1000) return L"infinite";
    if (m < 1) return Fmt(L"%.0f cm", m * 100);
    return Fmt(L"%.1f m", m);
}

// One row of the guide: a picture (resource name, from guide_icons.rc), what it does, how.
struct GuideRow { const wchar_t* icon; std::wstring action, how; };
// A status line: style 0 normal, 1 warning, 2 dim.
struct GuideNote { int style; std::wstring text; };
struct GuideKey { const wchar_t* key; const wchar_t* action; };
static const GuideKey kGuideKeys[] = {
    { L"E", L"Edit mode on / off" }, { L"N", L"New window" }, { L"X", L"Delete all windows" },
    { L"[ ]", L"Box depth" }, { L"P", L"Windows on / off" }, { L"J", L"Hands on / off" }, { L"O", L"Hands behind objects" },
    { L"V", L"Tint cutouts" }, { L"C", L"Change cutout colour" }, { L"G", L"Hide this panel" },
    { L"1 2 3", L"Load preset (+Shift: save)" },
    { L"- =", L"Hand cutout size" }, { L"D", L"Direct3D 12 (test)" }, { L"K", L"Force alpha (test)" },
};

struct GuideData {
    std::vector<GuideRow> hands, ctl;
    std::vector<GuideNote> win, depth, status;
};

static void GuideContent(GuideData& g) {
    bool gaze = g_gazeValid, laser = LaserMode();
    // Same action on the same row in both columns; the last row differs (hands: show / hide hands,
    // controllers: box depth, which have no counterpart).
    g.hands = {
        { L"FRAME", L"New window", L"Picture frame with both hands" },
        { gaze ? L"GAZE" : L"DOTS", L"Grab a handle or window", gaze ? L"Look at it, then pinch" : L"Pinch it" },
        { L"THUMBSUP", L"Save and finish", L"Thumbs up, hold" },
        { L"THUMBSDOWN", L"Delete window", L"Thumbs down, hold" },
        { L"VSIGN", L"Change cutout colour", L"V sign, hold" },
        { L"PRAYER", L"Show / hide hands", L"Palms together, hold" },
    };
    g.ctl = {
        { L"CTL_NEW", L"New window", L"Both grips, hold" },
        laser ? GuideRow{ L"CTL_LASER", L"Grab a handle or window", L"Point the laser, pull the trigger. Stick: nearer / further" }
              : gaze ? GuideRow{ L"CTL_GAZE", L"Grab a handle or window", L"Look at it, pull the trigger" }
                     : GuideRow{ L"CTL_GRAB", L"Grab a handle or window", L"Touch it, pull the trigger" },
        { L"CTL_EDIT", L"Save and finish", L"Grips + triggers, hold" },
        { L"CTL_DELETE", L"Delete window", L"Hold B or Y" },
        { L"CTL_COLOUR", L"Change cutout colour", L"Press the stick" },
        { L"CTL_DEPTH", L"Make it a box", laser ? L"Stick up / down, or grab the green box handle and use the stick" : L"Stick up / down" },
    };

    // Selected window.
    if (g_selected >= 0 && g_selected < (int)g_windows.size()) {
        const Window& w = g_windows[g_selected];
        g.win.push_back({ 0, Fmt(L"Window %d of %d", g_selected + 1, (int)g_windows.size()) });
        g.win.push_back({ 0, Fmt(L"Size: %.0f x %.0f cm", w.hw * 200, w.hh * 200) });
        // A box with a face mode is passthrough on its chosen faces only (keep doesn't apply then).
        bool faces = w.hd > 0 && w.boxMode > 0;
        std::wstring t = w.keep && !faces ? (w.hd > 0 ? Fmt(L"keep box, %.0f cm deep", w.hd * 200) : std::wstring(L"keep, flat"))
                                          : (w.hd > 0 ? Fmt(L"box, %.0f cm deep", w.hd * 200) : std::wstring(L"flat"));
        if (IsShape(w)) t += L", free shape";
        else if (w.ellipse) t += L", circle";
        g.win.push_back({ 0, L"Type: " + t });
        if (w.hd > 0) g.win.push_back({ 0, std::wstring(L"Box faces: ") + kBoxModeNameW[std::clamp(w.boxMode, 0, 3)] });
        g.win.push_back({ 0, L"Distance: " + ShortDist(Dist(g_head, V(w.pos))) });
    } else if (g_windows.empty()) {
        g.win.push_back({ 2, L"No windows yet" });
    } else {
        g.win.push_back({ 2, Fmt(L"None selected (%d window%ls)", (int)g_windows.size(), g_windows.size() > 1 ? L"s" : L"") });
    }
    if (g_drawArmed >= 0) g.win.push_back({ 1, L"Drawing: pinch and trace, let go to finish" });
    if (Now() < g_windowsFullUntil)
        g.win.push_back({ 1, Fmt(L"%d windows is the most: delete one to make another", kMaxWindows) });

    // Depth.
    if (g_gameDepth == 1) g.depth.push_back({ 0, L"Game depth: supported (" + ShortDist(g_gameNearZ) + L" to " + ShortDist(g_gameFarZ) + L")" });
    else if (g_gameDepth == 2) { g.depth.push_back({ 1, L"Game depth: made but not shared" }); g.depth.push_back({ 2, L"UEVR: turn on Enable Depth, restart the game" }); }
    else if (g_gameDepth == 3) g.depth.push_back({ 1, L"Game depth: on a different layer" });
    else if (g_gameDepth == 0) g.depth.push_back({ 1, L"Game depth: not supported" });
    else g.depth.push_back({ 2, L"Game depth: checking..." });
    bool anyBox = false;
    for (auto& w : g_windows) if (w.hd > 0 || w.keep) anyBox = true;
    if (!anyBox) g.depth.push_back({ 2, L"Box windows: none yet" });
    else if (g_depthRead == 1) g.depth.push_back({ 0, L"Box windows: using game depth" });
    else g.depth.push_back({ 1, L"Box windows: solid blocks (no depth)" });

    // Status.
    g.status.push_back({ 0, Widen("Game: " + g_appName) });
    g.status.push_back({ 0, Widen("Runtime: " + g_runtimeName + (g_viaVD ? " (Virtual Desktop)" : "")) });
    if (g_alphaSupported) g.status.push_back({ 0, L"Alpha channel: supported" });
    else if (g_forceAlpha) g.status.push_back({ 0, L"Alpha channel: forced on" });
    else g.status.push_back({ UseKeyColour() ? 0 : 1, L"Alpha channel: unsupported (missing from streamer)" });
    if (UseKeyColour()) g.status.push_back({ 0, L"Cutout colour: " + std::wstring(kKeyName[g_keyMode]) + L" screen" });
    else if (CutoutsShown()) g.status.push_back({ 0, L"Cutout colour: clear" });
    else g.status.push_back({ 1, L"Use Change cutout colour (above) for a green screen" });
    bool hl = g_hand[0].active, hr = g_hand[1].active;
    if (!g_hand[0].tracker && !g_hand[1].tracker) g.status.push_back({ 2, L"Hands: not available (see log.txt)" });
    else g.status.push_back({ 0, hl && hr ? L"Hands: both tracked" : hl ? L"Hands: left tracked" : hr ? L"Hands: right tracked" : L"Hands: not tracked" });
    if (g_handOcclude) g.status.push_back(g_handDepthUsed == 0 ? GuideNote{ 1, L"Hands behind objects: on, but no game depth" }
                                                       : GuideNote{ 0, L"Hands behind objects: on" });
    else g.status.push_back({ 2, L"Hands behind objects: off" });
}

#ifndef _WIN32
#include "guide_font.h"
#endif

// A handle symbol for the guide's legend, painted like psIcon (shaders.hlsl) into BGRA pixels.
struct LegendIcon { IconId id; float c[3]; float g[3]; bool ownGlyphCol; float angle; const wchar_t* word; };
static void PaintIcon(uint32_t* px, int W, int H, int cx, int cy, float rad, const LegendIcon& ic) {
    float ca = cosf(ic.angle), sa = sinf(ic.angle), aa = 1.0f / rad;
    float lum = 0.3f * ic.c[0] + 0.59f * ic.c[1] + 0.11f * ic.c[2];
    float gc[3] = { 1, 1, 1 };
    if (ic.ownGlyphCol) { gc[0] = ic.g[0]; gc[1] = ic.g[1]; gc[2] = ic.g[2]; }
    else if (lum > 0.74f) { gc[0] = 0.08f; gc[1] = 0.09f; gc[2] = 0.11f; }
    int r = (int)ceilf(rad) + 1;
    for (int y = std::max(0, cy - r); y < std::min(H, cy + r); ++y)
        for (int x = std::max(0, cx - r); x < std::min(W, cx + r); ++x) {
            float px0 = (x + 0.5f - cx) / rad, py0 = -(y + 0.5f - cy) / rad, d = sqrtf(px0 * px0 + py0 * py0);
            float disc = std::clamp((1 - d) / aa + 0.5f, 0.0f, 1.0f);
            if (disc <= 0) continue;
            glyph::P q{ px0 * ca + py0 * sa, -px0 * sa + py0 * ca };
            float rim = std::clamp((d - (1 - std::max(0.12f, aa))) / aa + 0.5f, 0.0f, 1.0f);
            float gcov = std::clamp(0.5f - glyph::Glyph(ic.id, q) / aa, 0.0f, 1.0f);
            uint32_t& o = px[y * W + x];
            float dst[3] = { ((o >> 16) & 255) / 255.0f, ((o >> 8) & 255) / 255.0f, (o & 255) / 255.0f };
            uint32_t out = 0xFF000000u;
            for (int k = 0; k < 3; ++k) {
                float c = ic.c[k] * (1.05f - 0.15f * d * d);
                c += (ic.c[k] * 0.3f - c) * rim;
                c += (gc[k] - c) * gcov;
                c = dst[k] + (c - dst[k]) * disc;
                out |= uint32_t(std::clamp(c, 0.0f, 1.0f) * 255 + 0.5f) << (16 - 8 * k);
            }
            o = out;
        }
}

// Draws the guide into g_guidePixels if anything on it changed.
static void RenderGuide() {
    GuideData g;
    GuideContent(g);
    std::wstring key;
    for (auto* rows : { &g.hands, &g.ctl }) for (auto& r : *rows) { key += r.icon; key += r.how; key += L'\n'; }
    for (auto* ns : { &g.win, &g.depth, &g.status }) for (auto& n : *ns) { key += n.text; key += L'\n'; }
    key += Fmt(L"%d %d %d %d %d %d %d%d%d", g_guideBtnState, g_presetHover, g_presetHeld, g_presetStep, g_presetFlash,
               g_presetFlashSaved ? 1 : 0, g_presetHas[0] ? 1 : 0, g_presetHas[1] ? 1 : 0, g_presetHas[2] ? 1 : 0);
    if (key == g_guideText && !g_guidePixels.empty()) return;
    g_guideText = key;

#ifdef _WIN32
    static HDC dc = nullptr, iconDc = nullptr;
    static void* bits = nullptr;
    static HFONT fTitle, fHead, fAction, fHow, fNote, fKey;
    static HMODULE mod = nullptr;
    if (!dc) {
        dc = CreateCompatibleDC(nullptr);
        iconDc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader = { sizeof(BITMAPINFOHEADER), kGuideW, -kGuideH, 1, 32, BI_RGB };
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bmp || !bits) { Log("Guide: couldn't create the image.\n"); if (dc) DeleteDC(dc); dc = nullptr; return; }
        SelectObject(dc, bmp);
        SetStretchBltMode(dc, HALFTONE);
        auto font = [](int px, int weight) {
            return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        fTitle = font(40, FW_BOLD); fHead = font(32, FW_BOLD); fAction = font(33, FW_SEMIBOLD); fHow = font(25, FW_NORMAL);
        fNote = font(26, FW_NORMAL); fKey = font(26, FW_BOLD);
        SetBkMode(dc, TRANSPARENT);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&RenderGuide, &mod);
    }
    auto fill = [&](RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); };
    auto shape = [&](RECT r, int rad, COLORREF c) {   // rad < 0: circle
        HBRUSH b = CreateSolidBrush(c); HPEN p = CreatePen(PS_SOLID, 1, c);
        HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
        if (rad < 0) Ellipse(dc, r.left, r.top, r.right, r.bottom);
        else RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
    };
    auto text = [&](const std::wstring& s, HFONT f, COLORREF c, RECT r, UINT flags) {
        SelectObject(dc, f); SetTextColor(dc, c);
        DrawTextW(dc, s.c_str(), -1, &r, flags | DT_NOPREFIX);
    };
    auto height = [&](const std::wstring& s, HFONT f, RECT r) {
        SelectObject(dc, f);
        DrawTextW(dc, s.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        return (int)(r.bottom - r.top);
    };
    auto width = [&](const std::wstring& s, HFONT f) {
        SelectObject(dc, f);
        RECT need{ 0, 0, 400, 40 }; DrawTextW(dc, s.c_str(), -1, &need, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        return (int)need.right;
    };
    auto picture = [&](const wchar_t* name, RECT r) {
        HBITMAP ib = mod ? (HBITMAP)LoadImageW(mod, name, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION) : nullptr;
        if (!ib) return;
        BITMAP bm{}; GetObject(ib, sizeof bm, &bm);
        HGDIOBJ old = SelectObject(iconDc, ib);
        StretchBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, iconDc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(iconDc, old);
        DeleteObject(ib);
    };
    const bool keyboard = true;
#else
    static GuideCanvas cv;
    static GuideFont ft[6];
    static bool fontsOk = false;
    if (cv.px.empty()) {
        cv.W = kGuideW; cv.H = kGuideH; cv.px.assign((size_t)kGuideW * kGuideH, 0xFF000000u);
        fontsOk = ft[0].Init(2, 40) && ft[1].Init(2, 32) && ft[2].Init(1, 33) && ft[3].Init(0, 25) && ft[4].Init(0, 26) && ft[5].Init(2, 26);
        if (!fontsOk) Log("Guide: no font found (install fonts-dejavu or similar): the panel has no text.\n");
    }
    GuideFont *fTitle = &ft[0], *fHead = &ft[1], *fAction = &ft[2], *fHow = &ft[3], *fNote = &ft[4], *fKey = &ft[5];
    auto fill = [&](RECT r, COLORREF c) { cv.Fill(r, c); };
    auto shape = [&](RECT r, int rad, COLORREF c) { cv.Shape(r, rad, c); };
    auto text = [&](const std::wstring& s, GuideFont* f, COLORREF c, RECT r, UINT flags) { cv.Text(s, f, c, r, flags); };
    auto height = [&](const std::wstring& s, GuideFont* f, RECT r) { return GuideCanvas::Height(s, f, r); };
    auto width = [&](const std::wstring& s, GuideFont* f) { return (int)ceilf(GuideCanvas::Measure(s, f)); };
    auto picture = [&](const wchar_t* name, RECT r) { cv.Picture(name, r); };
    const bool keyboard = false;   // no Ctrl+Alt shortcuts on Linux
#endif
    const COLORREF cHead = RGB(110, 230, 170), cText = RGB(232, 236, 242), cDim = RGB(150, 158, 170),
                   cWarn = RGB(255, 170, 90), cLine = RGB(52, 58, 68);
    fill({ 0, 0, kGuideW, kGuideH }, RGB(22, 25, 31));
    fill({ 0, 0, kGuideW, kGuideHeader }, RGB(30, 70, 60));
    text(L"EDIT MODE", fTitle, RGB(235, 255, 245), { 40, 0, kGuideW - 40, kGuideHeader }, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    text(keyboard ? L"Grab the panel to move it  ·  Ctrl+Alt+G hides it" : L"Grab the panel to move it", fHow, RGB(170, 210, 195), { 40, 0, kGuideW - 40, kGuideHeader }, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    // "Move all windows" button: hold it (pinch, trigger) and move your hand; every window follows.
    {
        const COLORREF bg = g_guideBtnState == 2 ? RGB(40, 150, 255) : g_guideBtnState == 1 ? RGB(60, 110, 170) : RGB(42, 58, 82);
        const COLORREF edge = g_guideBtnState ? RGB(150, 210, 255) : RGB(90, 120, 160);
        RECT b = kMoveAllBtn;
        shape(b, 40, edge);
        shape({ b.left + 3, b.top + 3, b.right - 3, b.bottom - 3 }, 36, bg);
        text(g_guideBtnState == 2 ? L"Moving: let go to place them" : L"Hold to move all windows",
             fAction, RGB(245, 250, 255), { b.left + 86, b.top, b.right - 20, b.bottom }, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    // Preset buttons 1-3: tap loads that saved layout, hold saves the current windows there.
    for (int i = 0; i < 3; ++i) {
        RECT b = PresetBtn(i);
        bool held = g_presetHeld == i, hover = g_presetHover == i, flash = g_presetFlash == i;
        COLORREF bg = !g_presetHas[i] ? RGB(34, 40, 50) : RGB(48, 62, 84);
        if (hover) bg = RGB(64, 92, 130);
        if (flash) bg = g_presetFlashSaved ? RGB(30, 130, 80) : RGB(40, 120, 200);
        COLORREF edge = held || hover || flash ? RGB(170, 215, 255) : RGB(80, 100, 130);
        shape(b, 30, edge);
        RECT in{ b.left + 3, b.top + 3, b.right - 3, b.bottom - 3 };
        shape(in, 26, bg);
        if (held) {   // fills from the left while held; full = saved
            RECT p = in; p.right = in.left + (in.right - in.left) * g_presetStep / 10;
            if (p.right > p.left + 4) shape(p, 26, g_presetStep >= 10 ? RGB(30, 150, 90) : RGB(200, 130, 40));
        }
        const wchar_t* sub = flash ? (g_presetFlashSaved ? L"saved" : L"loaded")
                           : held ? (g_presetStep >= 10 ? L"saved" : L"saving...")
                           : hover ? L"hold = save"
                           : g_presetHas[i] ? L"tap = load" : L"empty";
        text(Fmt(L"Preset %d", i + 1), fKey, g_presetHas[i] || held || flash ? RGB(245, 250, 255) : cDim,
             { b.left, b.top + 6, b.right, b.top + 46 }, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        text(sub, fHow, RGB(200, 215, 230), { b.left + 4, b.top + 44, b.right - 4, b.bottom - 6 },
             DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    // Columns: hands, controllers (picture rows), keyboard (key list).
    const int x1 = 40, x2 = 790, x3 = 1540, xEnd = kGuideW - 40;
    const int tile = 112, rowH = 124, top = 182, rowsBottom = top + 6 * rowH - 12;
    auto column = [&](const wchar_t* title, const std::vector<GuideRow>& rows, int x0, int xr) {
        text(title, fHead, cHead, { x0, 126, xr, 172 }, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        for (size_t i = 0; i < rows.size(); ++i) {
            int y = top + (int)i * rowH;
            shape({ x0, y, x0 + tile, y + tile }, 22, RGB(255, 255, 255));
            picture(rows[i].icon, { x0 + 6, y + 6, x0 + tile - 6, y + tile - 6 });
            int tx = x0 + tile + 22;
            text(rows[i].action, fAction, RGB(240, 244, 250), { tx, y + 10, xr, y + 54 }, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            text(rows[i].how, fHow, RGB(160, 170, 182), { tx, y + 54, xr, y + rowH - 4 }, DT_LEFT | DT_WORDBREAK);
        }
    };
    column(L"HANDS", g.hands, x1, x2 - 40);
    column(L"CONTROLLERS", g.ctl, x2, keyboard ? x3 - 40 : xEnd);
    if (keyboard) {
        text(L"KEYBOARD", fHead, cHead, { x3, 126, xEnd, 172 }, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        text(L"hold Ctrl+Alt, then", fHow, cDim, { x3 + 190, 126, xEnd, 172 }, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        const int n = (int)_countof(kGuideKeys), kh = (rowsBottom - top) / n;
        for (int i = 0; i < n; ++i) {
            int y = top + i * kh;
            shape({ x3, y + 4, x3 + 76, y + kh - 8 }, 14, RGB(70, 78, 92));
            text(kGuideKeys[i].key, fKey, RGB(245, 248, 252), { x3, y + 4, x3 + 76, y + kh - 8 }, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            text(kGuideKeys[i].action, fNote, cText, { x3 + 96, y, xEnd, y + kh - 4 }, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
    }
    for (int x : { x2 - 22, x3 - 22 }) if (keyboard || x == x2 - 22) fill({ x, 138, x + 3, rowsBottom }, cLine);

    // Handle symbols: one word each (the symbols are painted in after GDI, below).
    int y = rowsBottom + 14;
    fill({ x1, y, xEnd, y + 2 }, cLine);
    y += 16;
    static const LegendIcon legend[] = {
        { ICON_MOVE, { 0.15f, 0.45f, 1 }, {}, false, 0, L"move" },
        { ICON_RESIZE, { 1, 0.45f, 0 }, {}, false, 0.78539816f, L"resize" },
        { ICON_ROUND, { 1, 0.2f, 1 }, {}, false, 0, L"round" },
        { ICON_DEPTH, { 0.1f, 0.68f, 0.22f }, {}, false, 0, L"box" },
        { ICON_KEEP, { 0.95f, 0.95f, 0.95f }, { 0.1f, 0.68f, 0.22f }, true, 0, L"keep box" },
        { ICON_FACES, { 0.95f, 0.55f, 0.1f }, {}, false, 0, L"box faces" },
        { ICON_TO_SHAPE, { 0.4f, 0.2f, 0.8f }, {}, false, 0, L"shape" },
        { ICON_TO_CIRCLE, { 0.05f, 0.62f, 0.7f }, {}, false, 0, L"circle" },
        { ICON_DRAW, { 0.92f, 0.92f, 0.92f }, {}, false, 0, L"draw" },
        { ICON_ADD, { 1, 0.78f, 0.55f }, {}, false, 0, L"add point" },
        { ICON_LOCK, { 0.98f, 0.76f, 0.12f }, {}, false, 0, L"lock angle" },
        { ICON_SNAP, { 0.55f, 0.8f, 1 }, { 0.08f, 0.09f, 0.11f }, true, 0, L"snap upright / level" },
    };
    struct Placed { int x, y; };
    Placed placed[_countof(legend)];
    text(L"Handles:", fNote, cDim, { x1, y, x1 + 140, y + 40 }, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    int x = x1 + 120;
    for (size_t i = 0; i < _countof(legend); ++i) {
        // Next line when this one is full.
        const int needW = width(legend[i].word, fNote);
        if (x + 46 + needW > xEnd && x > x1 + 120) { x = x1 + 120; y += 44; }
        placed[i] = { x + 19, y + 20 };
        RECT m{ x + 46, y, x + 46 + needW + 4, y + 40 };
        text(legend[i].word, fNote, cText, m, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        x = m.right + 40;
    }

    // Status columns: selected window, depth, status.
    y += 56;
    fill({ x1, y, xEnd, y + 2 }, cLine);
    y += 12;
    auto notes = [&](const wchar_t* title, const std::vector<GuideNote>& ns, int x0, int xr) {
        text(title, fHead, cHead, { x0, y, xr, y + 44 }, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        int yy = y + 50;
        for (auto& n : ns) {
            COLORREF c = n.style == 1 ? cWarn : n.style == 2 ? cDim : cText;
            RECT r{ x0, yy, xr, kGuideH };
            int h = height(n.text, fNote, r);
            text(n.text, fNote, c, r, DT_LEFT | DT_WORDBREAK);
            yy += h + 4;
        }
    };
    notes(L"SELECTED WINDOW", g.win, x1, x2 - 40);
    notes(L"DEPTH", g.depth, x2, x3 - 40);
    notes(L"STATUS", g.status, x3, xEnd);

#ifdef _WIN32
    GdiFlush();
    g_guidePixels.assign((const uint32_t*)bits, (const uint32_t*)bits + kGuideW * kGuideH);
#else
    g_guidePixels = cv.px;
#endif
    for (auto& p : g_guidePixels) p |= 0xFF000000u;
    for (size_t i = 0; i < _countof(legend); ++i)
        PaintIcon(g_guidePixels.data(), kGuideW, kGuideH, placed[i].x, placed[i].y, 19.0f, legend[i]);
    {
        static const LegendIcon moveAll = { ICON_MOVE, { 0.95f, 0.97f, 1 }, { 0.1f, 0.35f, 0.7f }, true, 0, L"" };
        PaintIcon(g_guidePixels.data(), kGuideW, kGuideH, kMoveAllBtn.left + 46, (kMoveAllBtn.top + kMoveAllBtn.bottom) / 2, 28.0f, moveAll);
    }
    g_guideDirty = true;
    // Dev only: with ALPHA_VIEWS_DUMP set, save the latest guide image as guide_dump.ppm.
    static bool dumpOn = !EnvVar("ALPHA_VIEWS_DUMP").empty();
    if (dumpOn) {
        if (FILE* f = fopen((g_dir + PATH_SEP "guide_dump.ppm").c_str(), "wb")) {
            fprintf(f, "P6 %d %d 255\n", kGuideW, kGuideH);
            for (uint32_t px : g_guidePixels) { unsigned char c[3] = { (unsigned char)(px >> 16), (unsigned char)(px >> 8), (unsigned char)px }; fwrite(c, 1, 3, f); }
            fclose(f);
        }
    }
}

// Places the panel and refreshes its text. Called at the end of UpdateFrame.
static void UpdateGuide(XrSpace space, XMVECTOR head, double dt, double now) {
    g_guideShow = g_edit && g_guideOn;
    if (!g_guideShow) { g_guidePlaced = false; g_guidePinned = false; g_guideBtnState = 0; g_presetHover = g_presetHeld = -1; return; }

    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMVECTOR fwd = XMVectorSet(0, 0, -1, 0);
    if (!g_endViews.empty() && g_endSpace == space) fwd = XMVector3Rotate(fwd, Q(g_endViews[0].pose.orientation));
    fwd = XMVectorSetY(fwd, 0);
    fwd = XMVectorGetX(XMVector3Length(fwd)) < 0.1f ? XMVectorSet(0, 0, -1, 0) : XMVector3Normalize(fwd);
    XMVECTOR want = head + fwd * 0.85f + up * 0.27f;   // centre ~18 degrees above eye level

    bool move = !g_guidePlaced;
    if (g_guidePlaced && !g_guidePinned) {
        XMVECTOR to = XMVectorSetY(V(g_guidePos) - head, 0);
        float d = XMVectorGetX(XMVector3Length(to));
        float cosA = d > 0.05f ? XMVectorGetX(XMVector3Dot(to / d, fwd)) : -1;
        if (cosA < 0.72f || d > 1.4f || d < 0.4f) move = true;   // > ~44 degrees off to the side, or too far/near
    }
    if (move) XMStoreFloat3(&g_guideTarget, want);
    XMVECTOR pos = V(g_guidePos);
    if (!g_guidePlaced) pos = want;
    else if (!g_guidePinned) pos += (V(g_guideTarget) - pos) * (float)std::min(1.0, dt * 4.0);
    XMStoreFloat3(&g_guidePos, pos);
    g_guidePlaced = true;

    // Face the head.
    XMVECTOR n = head - pos;
    n = XMVectorGetX(XMVector3Length(n)) < 0.05f ? -fwd : XMVector3Normalize(n);
    XMVECTOR right = XMVector3Cross(up, n);
    right = XMVectorGetX(XMVector3Length(right)) < 0.1f ? XMVectorSet(1, 0, 0, 0) : XMVector3Normalize(right);
    XMVECTOR pu = XMVector3Cross(n, right);
    XMStoreFloat3(&g_guideRight, right); XMStoreFloat3(&g_guideUp, pu); XMStoreFloat3(&g_guideN, n);
    XMStoreFloat3(&g_guideCorner[0], pos - right * kGuideHalfW + pu * kGuideHalfH);
    XMStoreFloat3(&g_guideCorner[1], pos + right * kGuideHalfW + pu * kGuideHalfH);
    XMStoreFloat3(&g_guideCorner[2], pos - right * kGuideHalfW - pu * kGuideHalfH);
    XMStoreFloat3(&g_guideCorner[3], pos + right * kGuideHalfW - pu * kGuideHalfH);

    float dHead = XMVectorGetX(XMVector3Dot(head - pos, n));
    if (dHead > 0.05f) {
        auto behind = [&](const Inst& u) {
            XMVECTOR q = XMVectorSet(u.x, u.y, u.z, 0);
            float dq = XMVectorGetX(XMVector3Dot(q - pos, n));
            if (dq > -0.005f) return false;               // in front of the panel
            XMVECTOR x = head + (q - head) * (dHead / (dHead - dq)) - pos;
            // Only if its centre is behind the panel: no margin of the icon's size, which grows with
            // distance (FarScale) and made an invisible border round the panel hiding far windows' buttons.
            return fabsf(XMVectorGetX(XMVector3Dot(x, right))) < kGuideHalfW &&
                   fabsf(XMVectorGetX(XMVector3Dot(x, pu))) < kGuideHalfH;
        };
        g_uiInst.erase(std::remove_if(g_uiInst.begin(), g_uiInst.end(), behind), g_uiInst.end());
    }

    // Panel buttons: held, pointed at (eyes, laser or a hand on it), or idle.
    int btn = 0, pHover = -1, pHeld = -1;
    for (auto& h : g_hand) {
        if (h.mode == GRAB_ALL) btn = 2;
        if (h.mode == GRAB_PRESET) pHeld = h.corner;
    }
    auto pointed = [&](GrabMode m, int k) {
        if (m == GRAB_ALL && btn == 0) btn = 1;
        if (m == GRAB_PRESET && pHover < 0) pHover = k;
    };
    pointed(g_gazeTarget.mode, g_gazeTarget.corner);
    for (auto& h : g_hand) {
        if (!h.active || h.mode != GRAB_NONE) continue;
        if (LaserMode() && h.isCtl) pointed(h.ray.mode, h.ray.corner);
        int k = -1; GrabMode m = GuideTouch(V(h.pinchPt), &k);
        pointed(m, k);
    }
    // Holding a preset: progress towards saving, in steps (each step redraws the panel).
    int step = pHeld >= 0 ? (g_presetHoldSaved ? 10 : std::min(9, (int)((now - g_presetHoldStart) / kPresetHoldSave * 10))) : 0;
    if (g_presetFlash >= 0 && now > g_presetFlashUntil) g_presetFlash = -1;
    int state = btn | (pHover + 1) << 2 | (pHeld + 1) << 4 | step << 6 | (g_presetFlash + 1) << 10 | (g_presetFlashSaved ? 1 << 12 : 0);
    static int lastState = -1;
    bool changed = state != lastState;
    lastState = state;
    g_guideBtnState = btn; g_presetHover = pHover; g_presetHeld = pHeld; g_presetStep = step;
    if (changed || now >= g_guideNextText) { g_guideNextText = now + 0.25; RenderGuide(); }
}

static void UpdateFrame(XrSpace space, XrTime time, XMVECTOR head) {
    double now = Now();
    double dt = g_lastUpdate > 0 ? std::min(0.1, now - g_lastUpdate) : 0;
    g_lastUpdate = now;
    g_head = head;
    g_maskInst.clear(); g_uiInst.clear(); g_winTris.clear();

    // --- eye gaze ray (eye-tracking headsets)
    g_gazeValid = false;
    if (g_act.attached && g_act.eyeSpace && nLocateSpace) {
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(nLocateSpace(g_act.eyeSpace, space, time, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            g_gazeValid = true;
            g_gazeLastValid = Now();
            g_gazeOrigin = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
            XMStoreFloat3(&g_gazeDir, XMVector3Rotate(XMVectorSet(0, 0, -1, 0), Q(loc.pose.orientation)));
            if (!g_gazeLogged) { Log("Eye gaze active: look at a dot and pinch anywhere.\n"); g_gazeLogged = true; }
        }
    }
    UpdateGazeTarget();

    // --- Ctrl+Alt+N: 30 x 20 cm window 50 cm ahead, 20 cm below the eyes (for runtimes without
    // good finger tracking, e.g. SteamVR). Facing uses the last submitted view's orientation.
    if (g_spawnAhead) {
        g_spawnAhead = false;
        XMVECTOR fwd = XMVectorSet(0, 0, -1, 0);
        if (!g_endViews.empty() && g_endSpace == space) fwd = XMVector3Rotate(fwd, Q(g_endViews[0].pose.orientation));
        fwd = XMVectorSetY(fwd, 0);
        fwd = XMVectorGetX(XMVector3Length(fwd)) < 0.1f ? XMVectorSet(0, 0, -1, 0) : XMVector3Normalize(fwd);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(fwd, XMVectorSet(0, 1, 0, 0)));
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        XMVECTOR c = head + fwd * 0.5f - up * 0.2f;
        if (SpawnWindow(c - right * 0.15f, c + right * 0.15f, c - right * 0.15f - up * 0.1f, c + right * 0.15f + up * 0.1f,
                    XMVectorSetY(head, XMVectorGetY(c))))
            Log("Window placed in front of the head (hotkey).\n");
    }

    // --- locate hands
    for (int hi = 0; hi < 2; ++hi) {
        HandState& h = g_hand[hi];
        bool active = false;
        if (h.tracker && nLocateHandJointsEXT) {
            XrHandJointLocationsEXT locs{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
            locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locs.jointLocations = h.j;
            XrHandJointsLocateInfoEXT li{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
            li.baseSpace = space; li.time = time;
            XrHandTrackingDataSourceStateEXT dss{ XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT };
            if (g_handSourceEnabled) locs.next = &dss;
            active = XR_SUCCEEDED(nLocateHandJointsEXT(h.tracker, &li, &locs)) && locs.isActive &&
                     JValid(h, XR_HAND_JOINT_PALM_EXT) && JValid(h, XR_HAND_JOINT_THUMB_TIP_EXT) &&
                     JValid(h, XR_HAND_JOINT_INDEX_TIP_EXT);
            static bool jointsLogged[2] = {};
            if (active && !jointsLogged[hi]) { jointsLogged[hi] = true; Log("%s hand joints received\n", hi ? "Right" : "Left"); }
            if (active && g_handSourceEnabled && dss.isActive) {
                int src = dss.dataSource == XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT ? 2 : 1;
                if (src != h.jointSource)
                    Log("%s hand joints come from %s\n", hi ? "Right" : "Left", src == 2 ? "a controller" : "hand tracking");
                h.jointSource = src;
                // Joints made from a held controller: that's the controller, not your hand. But if
                // that side's controller isn't in use (e.g. Steam Link labels its hand tracking
                // this way), they're your hand.
                if (src == 2 && g_hand[2 + hi].rawActive) active = false;
            }
        }
        if (active != h.active) Log("%s hand tracking %s\n", hi ? "Right" : "Left", active ? "active" : "lost");
        h.active = active;
        if (!active) {
            if (h.mode != GRAB_NONE) EndGrab(h, now);
            h.pinching = false;
            h.thumbsUpSince = h.thumbsDownSince = -1; h.thumbsLatched = h.thumbsDownLatched = false;
            continue;
        }
        const XrQuaternionf& po = h.j[XR_HAND_JOINT_PALM_EXT].pose.orientation;
        h.grabRot = { po.x, po.y, po.z, po.w };

        // Pinch: the system's own pinch (XR_EXT_hand_interaction) when available, else joint distance.
        XMVECTOR tt = JP(h, XR_HAND_JOINT_THUMB_TIP_EXT), it = JP(h, XR_HAND_JOINT_INDEX_TIP_EXT);
        XMStoreFloat3(&h.pinchPt, (tt + it) * 0.5f);
        bool sysOk = false;
        float val = 0;
        if (g_act.attached && nGetActionStateFloat && g_act.pinchValue) {
            XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
            gi.action = g_act.pinchValue;
            gi.subactionPath = g_act.hand[hi];
            XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
            if (XR_SUCCEEDED(nGetActionStateFloat(g_session, &gi, &st)) && st.isActive) { sysOk = true; val = st.currentState; }
            if (sysOk && g_act.space[hi]) {
                XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
                if (XR_SUCCEEDED(nLocateSpace(g_act.space[hi], space, time, &loc)) &&
                    (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
                    h.pinchPt = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
            }
        }
        if (sysOk && !h.sysPinchLogged) { Log("%s hand: using system pinch\n", hi ? "Right" : "Left"); h.sysPinchLogged = true; }
        bool was = h.pinching;
        if (sysOk) h.pinching = was ? val > 0.5f : val > 0.9f;
        else { float pd = Dist(tt, it); h.pinching = was ? pd < 0.04f : pd < 0.022f; }   // Quest joints sit further apart when pinching
        // While pinching, the grab point is pinned to the palm: fingers still closing or opening
        // (and fingertip jitter) don't move it, only the hand does.
        {
            const XrPosef& pp = h.j[XR_HAND_JOINT_PALM_EXT].pose;
            XMMATRIX palm = XMMatrixRotationQuaternion(Q(pp.orientation)) * XMMatrixTranslation(pp.position.x, pp.position.y, pp.position.z);
            if (h.pinching && !was)
                XMStoreFloat3(&h.pinchAnchor, XMVector3TransformCoord(V(h.pinchPt), XMMatrixInverse(nullptr, palm)));
            else if (h.pinching)
                XMStoreFloat3(&h.pinchPt, XMVector3TransformCoord(V(h.pinchAnchor), palm));
        }
        if (h.pinching && !was) {
            h.pinchStart = now; h.pinchStartPt = h.pinchPt;
            if (g_edit) BeginGrab(h);
        } else if (!h.pinching && was) {
            if (h.mode != GRAB_NONE) EndGrab(h, now);
        }
        if (h.pinching && h.mode != GRAB_NONE) UpdateGrab(h);

        // Thumbs up only LOCKS IN edits (edit mode off). It never turns edit mode on, because a
        // thumb sticking out from the wheel rim looks just like a thumbs up.
        int td = h.pinching ? 0 : ThumbDir(h);
        XMFLOAT3 tp; XMStoreFloat3(&tp, tt);
        if (td == 1 && g_edit) {
            if (h.thumbsUpSince < 0) h.thumbsUpSince = now;
            float prog = float((now - h.thumbsUpSince) / 0.8);
            if (!h.thumbsLatched) {
                g_uiInst.push_back({ tp.x, tp.y + 0.03f, tp.z, 0.004f + 0.01f * std::min(prog, 1.0f), 1, 0.8f, 0, 1 });
                if (prog >= 1) { h.thumbsLatched = true; SetEdit(false, "thumbs up"); }
            }
        } else {
            h.thumbsUpSince = -1; h.thumbsLatched = false;
        }
        // Thumbs down (edit mode): delete the selected window.
        if (td == -1 && g_edit && g_selected >= 0) {
            if (h.thumbsDownSince < 0) h.thumbsDownSince = now;
            float prog = float((now - h.thumbsDownSince) / 0.8);
            if (!h.thumbsDownLatched) {
                g_uiInst.push_back({ tp.x, tp.y - 0.03f, tp.z, 0.004f + 0.01f * std::min(prog, 1.0f), 1, 0.1f, 0.1f, 1 });
                if (prog >= 1) { h.thumbsDownLatched = true; DeleteWindow(g_selected); }
            }
        } else {
            h.thumbsDownSince = -1; h.thumbsDownLatched = false;
        }
        // V sign (edit mode only): next cutout colour. The dot shows the colour it switches to.
        if (g_edit && !h.pinching && h.mode == GRAB_NONE && IsVSign(h)) {
            if (h.vSince < 0) h.vSince = now;
            float prog = float((now - h.vSince) / 0.8);
            if (!h.vLatched) {
                int next = (g_keyMode + 1) % 4;
                float c[3] = { kKeyCol[next][0], kKeyCol[next][1], kKeyCol[next][2] };
                if (!next) c[0] = c[1] = c[2] = 0.85f;   // back to alpha: light grey
                XMFLOAT3 vp;
                XMStoreFloat3(&vp, (JP(h, XR_HAND_JOINT_INDEX_TIP_EXT) + JP(h, XR_HAND_JOINT_MIDDLE_TIP_EXT)) * 0.5f);
                g_uiInst.push_back({ vp.x, vp.y + 0.03f, vp.z, 0.004f + 0.01f * std::min(prog, 1.0f), c[0], c[1], c[2], 1 });
                if (prog >= 1) { h.vLatched = true; CycleKeyMode("V sign"); }
            }
        } else {
            h.vSince = -1; h.vLatched = false;
        }
    }

    // --- controllers
    static bool stickDirty = false, comboNeedsRelease = false;
    bool stickMoving = false;
    for (int ci = 0; ci < 2; ++ci) {
        HandState& h = g_hand[2 + ci];
        h.isCtl = true;
        bool active = false;
        if (g_ctl.attached && nGetActionStateFloat && nGetActionStateBoolean) {
            XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
            gi.subactionPath = g_ctl.hand[ci];
            auto getF = [&](XrAction a, bool* isActive) {
                XrActionStateFloat s{ XR_TYPE_ACTION_STATE_FLOAT };
                gi.action = a;
                bool ok = a && XR_SUCCEEDED(nGetActionStateFloat(g_session, &gi, &s)) && s.isActive;
                if (isActive) *isActive = ok;
                return ok ? s.currentState : 0.0f;
            };
            h.trigger = getF(g_ctl.trigger, &active);   // trigger bound + active = a controller is in use
            h.squeeze = getF(g_ctl.squeeze, nullptr);
            h.stickX = h.stickY = 0;
            if (g_ctl.stick && nGetActionStateVector2f) {
                XrActionStateVector2f vs{ XR_TYPE_ACTION_STATE_VECTOR2F };
                gi.action = g_ctl.stick;
                if (XR_SUCCEEDED(nGetActionStateVector2f(g_session, &gi, &vs)) && vs.isActive) { h.stickX = vs.currentState.x; h.stickY = vs.currentState.y; }
            }
            if (!h.stickSeen && (fabsf(h.stickX) > 0.5f || fabsf(h.stickY) > 0.5f)) {
                h.stickSeen = true;
                Log("%s thumbstick input received\n", ci ? "Right" : "Left");
            }
            if (!h.trigSeen && h.trigger > 0.8f) { h.trigSeen = true; Log("%s trigger input received\n", ci ? "Right" : "Left"); }
            if (!h.gripSeen && h.squeeze > 0.8f) { h.gripSeen = true; Log("%s grip input received\n", ci ? "Right" : "Left"); }
            // Which controller type the runtime picked for this hand (logged when it changes).
            if (nGetCurrentInteractionProfile && now - h.profileCheck > 1.0) {
                h.profileCheck = now;
                XrInteractionProfileState ps{ XR_TYPE_INTERACTION_PROFILE_STATE };
                if (XR_SUCCEEDED(nGetCurrentInteractionProfile(g_session, g_ctl.hand[ci], &ps)) && ps.interactionProfile != h.profile) {
                    h.profile = ps.interactionProfile;
                    char buf[XR_MAX_PATH_LENGTH] = "none";
                    uint32_t len = 0;
                    if (h.profile) nPathToString(g_instance, h.profile, sizeof buf, &len, buf);
                    Log("%s hand input profile: %s\n", ci ? "Right" : "Left", buf);
                }
            }
            XrActionStateBoolean bs{ XR_TYPE_ACTION_STATE_BOOLEAN };
            gi.action = g_ctl.button;
            h.button = g_ctl.button && XR_SUCCEEDED(nGetActionStateBoolean(g_session, &gi, &bs)) && bs.isActive && bs.currentState;
            XrActionStateBoolean cs{ XR_TYPE_ACTION_STATE_BOOLEAN };
            gi.action = g_ctl.stickClick;
            h.stickClick = g_ctl.stickClick && XR_SUCCEEDED(nGetActionStateBoolean(g_session, &gi, &cs)) && cs.isActive && cs.currentState;
            XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
            const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (active && g_ctl.space[ci] && XR_SUCCEEDED(nLocateSpace(g_ctl.space[ci], space, time, &loc)) &&
                (loc.locationFlags & need) == need) {
                // Grab point: 4 cm in front of the controller's aim pose (its tip).
                XMVECTOR q = Q(loc.pose.orientation);
                XMStoreFloat3(&h.pinchPt, V(loc.pose.position) + XMVector3Rotate(XMVectorSet(0, 0, -0.04f, 0), q));
                XMStoreFloat4(&h.grabRot, q);
                h.aimO = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
                XMStoreFloat3(&h.aimD, XMVector3Rotate(XMVectorSet(0, 0, -1, 0), q));
            } else {
                active = false;
            }
        }
        // Virtual Desktop (and others) turn tracked hands into pretend controllers (pinch = trigger,
        // fist = grip). When that hand's joints are tracked, use the hand and ignore its controller.
        // SteamVR can make hand joints from held controllers, so there a tracked hand doesn't prove the
        // controller is pretend, unless the runtime says the joints come from hand tracking
        // (XR_EXT_hand_tracking_data_source).
        h.rawActive = active;
        static const bool steamVR = g_runtimeName.find("SteamVR") != std::string::npos;
        bool realHand = g_hand[ci].active && (g_handSourceEnabled ? g_hand[ci].jointSource == 1 : !steamVR);
        if (active && realHand) {
            if (!h.emuIgnoredLogged) {
                Log("%s hand is tracked: ignoring its emulated controller input\n", ci ? "Right" : "Left");
                h.emuIgnoredLogged = true;
            }
            active = false;
        }
        if (active != h.active) Log("%s controller %s\n", ci ? "Right" : "Left", active ? "active" : "lost");
        h.active = active;
        if (!active) {
            if (h.mode != GRAB_NONE) EndGrab(h, now);
            h.pinching = false; h.buttonSince = -1; h.buttonLatched = false;
            continue;
        }

        // Laser (edit mode, no eye tracking): what the controller points at. Kept while grabbing.
        if (!LaserMode()) h.ray = GazeTarget();
        else if (h.mode == GRAB_NONE) h.ray = RayTarget(V(h.aimO), V(h.aimD), 4.0f, &h.rayLen);

        // Trigger = pinch.
        bool was = h.pinching;
        h.pinching = was ? h.trigger > 0.4f : h.trigger > 0.75f;
        if (h.pinching && !was) {
            h.pinchStart = now; h.pinchStartPt = h.pinchPt;
            if (g_edit && h.squeeze < 0.5f) BeginGrab(h);   // grip held too = the edit combo, not a grab
        } else if (!h.pinching && was) {
            if (h.mode != GRAB_NONE) EndGrab(h, now);
        }
        if (h.pinching && h.mode != GRAB_NONE) UpdateGrab(h);

        // Draw the laser: dots along the ray, up to what it points at (or what it holds).
        if (LaserMode()) {
            bool holding = h.laser && h.mode != GRAB_NONE;
            bool onSomething = holding || h.ray.mode != GRAB_NONE;
            float len = holding ? h.grabDist : (h.ray.mode != GRAB_NONE ? h.rayLen : 1.2f);
            if (holding && h.mode == GRAB_MOVE && h.win >= 0 && h.win < (int)g_windows.size())
                len = XMVectorGetX(XMVector3Dot(V(g_windows[h.win].pos) - V(h.aimO), V(h.aimD)));   // follows push / pull
            len = std::clamp(len, 0.05f, 8.0f);
            float step = std::max(0.025f, len / 90);   // at most ~90 dots, however far
            XMVECTOR o = V(h.aimO), d = V(h.aimD);
            for (float t = 0.06f; t < len; t += step) {
                XMFLOAT3 q; XMStoreFloat3(&q, o + d * t);
                if (onSomething) g_uiInst.push_back({ q.x, q.y, q.z, 0.0018f, 0.45f, 0.9f, 1, 1 });
                else g_uiInst.push_back({ q.x, q.y, q.z, 0.0014f, 0.6f, 0.6f, 0.6f, 1 });
            }
            if (onSomething) { XMFLOAT3 q; XMStoreFloat3(&q, o + d * len); g_uiInst.push_back({ q.x, q.y, q.z, 0.006f, 0.45f, 0.9f, 1, 1 }); }
        }

        bool clicked = h.stickClick && !h.stickClickWas;
        h.stickClickWas = h.stickClick;
        if (!g_edit) { h.buttonSince = -1; h.buttonLatched = false; continue; }
        // Thumbstick press (edit mode): next cutout colour.
        if (clicked) CycleKeyMode("thumbstick press");
        // B / Y held: delete the selected window.
        if (h.button && g_selected >= 0) {
            if (h.buttonSince < 0) h.buttonSince = now;
            float prog = float((now - h.buttonSince) / 0.8);
            if (!h.buttonLatched) {
                g_uiInst.push_back({ h.pinchPt.x, h.pinchPt.y + 0.03f, h.pinchPt.z, 0.004f + 0.01f * std::min(prog, 1.0f), 1, 0.1f, 0.1f, 1 });
                if (prog >= 1) { h.buttonLatched = true; DeleteWindow(g_selected); }
            }
        } else if (!h.button) {
            h.buttonSince = -1; h.buttonLatched = false;
        }
        auto stick = [](float v) { return fabsf(v) < 0.3f ? 0.0f : (fabsf(v) - 0.3f) / 0.7f * (v > 0 ? 1.0f : -1.0f); };
        if (h.laser && h.mode == GRAB_MOVE) {
            // Holding a window with the laser: up / down pushes it away / pulls it closer,
            // left / right turns it.
            float sy = stick(h.stickY), sx = stick(h.stickX);
            if (sy != 0) {
                // Faster when far. Keep the window between 15 cm and 6 m in front of the controller.
                float z0 = XMVectorGetZ(XMLoadFloat4x4(&h.rel).r[3]);   // window centre in controller space
                float dist = std::max(0.5f, -(z0 - h.push));
                h.push = std::clamp(h.push + sy * dist * float(dt), z0 + 0.15f, std::max(z0 + 0.15f, z0 + 6.0f));
            }
            // Turning: not for locked windows (they keep their angle; the snap button squares them up).
            bool locked = h.win >= 0 && h.win < (int)g_windows.size() && g_windows[h.win].lock;
            if (sx != 0 && !locked) h.turn -= sx * 1.5f * float(dt);
        }
        // Thumbstick up / down: selected window deeper / shallower.
        else if (h.mode != GRAB_GUIDE && h.mode != GRAB_ALL && h.mode != GRAB_PRESET && fabsf(h.stickY) > 0.3f && g_selected >= 0 && g_selected < (int)g_windows.size()) {
            Window& w = g_windows[g_selected];
            float s = (fabsf(h.stickY) - 0.3f) / 0.7f * (h.stickY > 0 ? 1.0f : -1.0f);
            // Up to 30 cm/s total depth, faster as the box gets bigger (so a deep box doesn't take ages).
            float rate = std::max(0.15f, w.hd * 0.8f);
            w.hd = std::clamp(w.hd + s * rate * float(dt), 0.0f, kMaxHalfDepth);
            stickMoving = stickDirty = true;
        }
    }
    if (stickDirty && !stickMoving) {
        stickDirty = false;
        if (g_selected >= 0 && g_selected < (int)g_windows.size()) {
            Window& w = g_windows[g_selected];
            if (w.hd < 0.004f) w.hd = 0;
            Log("Window %d depth %.0f cm%s (thumbstick)\n", g_selected, w.hd * 200, w.hd > 0 ? "" : " (flat)");
        }
        SaveWindows();
    }
    // Two-controller combos.
    {
        HandState &cl = g_hand[2], &cr = g_hand[3];
        bool both = cl.active && cr.active;
        bool grips = both && cl.squeeze > 0.8f && cr.squeeze > 0.8f;
        bool combo = grips && cl.trigger > 0.8f && cr.trigger > 0.8f;
        // Both grips + both triggers, held: edit mode on / lock in.
        if (combo) {
            if (g_ctlComboSince < 0) g_ctlComboSince = now;
            float prog = float((now - g_ctlComboSince) / 1.2);
            if (!g_ctlComboLatched) {
                for (HandState* h : { &cl, &cr })
                    g_uiInst.push_back({ h->pinchPt.x, h->pinchPt.y + 0.03f, h->pinchPt.z, 0.004f + 0.01f * std::min(prog, 1.0f), 1, 1, 1, 1 });
                if (prog >= 1) {
                    g_ctlComboLatched = true;
                    comboNeedsRelease = true;
                    SetEdit(!g_edit, "controllers: grips + triggers");
                }
            }
        } else {
            g_ctlComboSince = -1; g_ctlComboLatched = false;
        }
        if (comboNeedsRelease && (!both || (cl.squeeze < 0.3f && cr.squeeze < 0.3f))) comboNeedsRelease = false;
        // Edit mode, both grips only, held: new window between the controller tips.
        bool frame = g_edit && grips && !combo && !comboNeedsRelease && cl.trigger < 0.3f && cr.trigger < 0.3f &&
                     cl.mode == GRAB_NONE && cr.mode == GRAB_NONE && Dist(V(cl.pinchPt), V(cr.pinchPt)) > 0.08f;
        if (frame) {
            if (g_ctlFrameSince < 0) g_ctlFrameSince = now;
            float prog = float((now - g_ctlFrameSince) / 0.6);
            if (!g_ctlFrameLatched) {
                for (HandState* h : { &cl, &cr })
                    g_uiInst.push_back({ h->pinchPt.x, h->pinchPt.y, h->pinchPt.z, 0.004f + 0.012f * std::min(prog, 1.0f), 0, 1, 0.3f, 1 });
                if (prog >= 1) {
                    g_ctlFrameLatched = true;
                    XMVECTOR a = V(cl.pinchPt), b = V(cr.pinchPt);
                    SpawnWindow(a, b, a, b, head);
                }
            }
        } else if (!grips) {
            g_ctlFrameSince = -1; g_ctlFrameLatched = false;
        } else {
            g_ctlFrameSince = -1;   // still gripping after a window was made: wait for release
        }
    }

    // --- picture frame gesture (both hands)
    if (g_hand[0].active && g_hand[1].active && IsLShape(g_hand[0]) && IsLShape(g_hand[1]) &&
        Dist(FrameCorner(g_hand[0]), FrameCorner(g_hand[1])) > 0.08f) {
        if (g_frameSince < 0) g_frameSince = now;
        float prog = float((now - g_frameSince) / 0.6);
        if (!g_frameLatched) {
            for (int hi = 0; hi < 2; ++hi) {
                XMFLOAT3 c; XMStoreFloat3(&c, FrameCorner(g_hand[hi]));
                g_uiInst.push_back({ c.x, c.y, c.z, 0.004f + 0.012f * std::min(prog, 1.0f), 0, 1, 0.3f, 1 });
            }
            if (prog >= 1) {
                g_frameLatched = true;
                // Not editing: the first frame gesture only opens edit mode. Editing: create a window.
                if (!g_edit) SetEdit(true, "picture frame");
                else SpawnWindow(FrameCorner(g_hand[0]), FrameCorner(g_hand[1]),
                                 JP(g_hand[0], XR_HAND_JOINT_INDEX_TIP_EXT), JP(g_hand[1], XR_HAND_JOINT_INDEX_TIP_EXT), head);
            }
        }
    } else {
        g_frameSince = -1; g_frameLatched = false;
    }

    // --- prayer gesture (palms together, fingers pointing the same way): show/hide hand cutouts
    if (g_hand[0].active && g_hand[1].active && !g_hand[0].pinching && !g_hand[1].pinching) {
        const HandState &L = g_hand[0], &R = g_hand[1];
        // Joint +Y is the back of the hand, -Z points to the fingertips.
        XMVECTOR nL = XMVector3Rotate(XMVectorSet(0, -1, 0, 0), Q(L.j[XR_HAND_JOINT_PALM_EXT].pose.orientation));
        XMVECTOR nR = XMVector3Rotate(XMVectorSet(0, -1, 0, 0), Q(R.j[XR_HAND_JOINT_PALM_EXT].pose.orientation));
        XMVECTOR fL = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), Q(L.j[XR_HAND_JOINT_PALM_EXT].pose.orientation));
        XMVECTOR fR = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), Q(R.j[XR_HAND_JOINT_PALM_EXT].pose.orientation));
        bool praying = Dist(JP(L, XR_HAND_JOINT_PALM_EXT), JP(R, XR_HAND_JOINT_PALM_EXT)) < 0.07f &&
                       Dist(JP(L, XR_HAND_JOINT_MIDDLE_TIP_EXT), JP(R, XR_HAND_JOINT_MIDDLE_TIP_EXT)) < 0.05f &&
                       XMVectorGetX(XMVector3Dot(nL, nR)) < -0.6f &&
                       XMVectorGetX(XMVector3Dot(fL, fR)) > 0.6f &&
                       Extended(L, kMiddle, 4) && Extended(R, kMiddle, 4);
        if (praying) {
            if (g_praySince < 0) g_praySince = now;
            float prog = float((now - g_praySince) / 0.8);
            if (!g_prayLatched) {
                XMFLOAT3 c;
                XMStoreFloat3(&c, (JP(L, XR_HAND_JOINT_MIDDLE_TIP_EXT) + JP(R, XR_HAND_JOINT_MIDDLE_TIP_EXT)) * 0.5f);
                g_uiInst.push_back({ c.x, c.y + 0.03f, c.z, 0.004f + 0.01f * std::min(prog, 1.0f), 1, 1, 1, 1 });
                if (prog >= 1) {
                    g_prayLatched = true;
                    g_handsEnabled = !g_handsEnabled;
                    Log("Hand cutouts %s (prayer gesture)\n", g_handsEnabled ? "on" : "off");
                    SaveSettings();
                }
            }
        } else {
            g_praySince = -1; g_prayLatched = false;
        }
    } else {
        g_praySince = -1; g_prayLatched = false;
    }

    // --- window cutout triangles
    static std::vector<XMFLOAT2> per; // reused each frame
    g_boxDraws.clear();
    g_flatVerts = 0;
    if (g_windowsEnabled || g_edit) {
        // Flat windows first (one draw), then each box (its own draw: front, back and sides).
        // Keep windows count as boxes here (their depth test needs the box shader).
        static std::vector<int> tri;   // shape triangles (indices into its points)
        for (auto& w : g_windows) {
            if (w.hd > 0 || w.keep) continue;
            Perimeter(w, per);
            if (IsShape(w)) {   // any outline, convex or not: ear-clipped triangles
                Triangulate(per, tri);
                if (g_winTris.size() + tri.size() > kMaxWinVerts) continue;
                for (int i : tri) { XMFLOAT3 f; XMStoreFloat3(&f, WinToWorld(w, per[i].x, per[i].y)); g_winTris.push_back(f); }
                continue;
            }
            XMFLOAT3 c; XMStoreFloat3(&c, WinToWorld(w, 0, 0));
            for (size_t i = 0; i < per.size() && g_winTris.size() + 3 <= kMaxWinVerts; ++i) {
                const XMFLOAT2 &a = per[i], &b = per[(i + 1) % per.size()];
                XMFLOAT3 pa, pb;
                XMStoreFloat3(&pa, WinToWorld(w, a.x, a.y));
                XMStoreFloat3(&pb, WinToWorld(w, b.x, b.y));
                g_winTris.push_back(c); g_winTris.push_back(pa); g_winTris.push_back(pb);
            }
        }
        g_flatVerts = (UINT)g_winTris.size();
        for (int wi = 0; wi < (int)g_windows.size(); ++wi) {
            const Window& w = g_windows[wi];
            if (w.hd <= 0 && !w.keep) continue;   // keep windows always need the depth test, even flat
            Perimeter(w, per);
            if (g_winTris.size() + per.size() * 12 > kMaxWinVerts) break;
            UINT first = (UINT)g_winTris.size();
            auto put = [&](float x, float y, float z) {
                XMFLOAT3 f; XMStoreFloat3(&f, WinToWorld(w, x, y, z)); g_winTris.push_back(f);
            };
            if (IsShape(w)) {   // front and back caps from the triangulated outline, plus the sides
                Triangulate(per, tri);
                for (float z : { w.hd, -w.hd }) for (int i : tri) put(per[i].x, per[i].y, z);
                for (size_t i = 0; i < per.size(); ++i) {
                    const XMFLOAT2 &a = per[i], &b = per[(i + 1) % per.size()];
                    put(a.x, a.y, w.hd); put(b.x, b.y, w.hd); put(b.x, b.y, -w.hd);
                    put(a.x, a.y, w.hd); put(b.x, b.y, -w.hd); put(a.x, a.y, -w.hd);
                }
                g_boxDraws.push_back({ wi, first, (UINT)g_winTris.size() - first });
                continue;
            }
            for (size_t i = 0; i < per.size(); ++i) {
                const XMFLOAT2 &a = per[i], &b = per[(i + 1) % per.size()];
                put(0, 0, w.hd); put(a.x, a.y, w.hd); put(b.x, b.y, w.hd);          // front
                put(0, 0, -w.hd); put(a.x, a.y, -w.hd); put(b.x, b.y, -w.hd);       // back
                put(a.x, a.y, w.hd); put(b.x, b.y, w.hd); put(b.x, b.y, -w.hd);     // side
                put(a.x, a.y, w.hd); put(b.x, b.y, -w.hd); put(a.x, a.y, -w.hd);
            }
            g_boxDraws.push_back({ wi, first, (UINT)g_winTris.size() - first });
        }
    }

    // --- edit-mode handles
    if (g_edit) {
        for (int wi = 0; wi < (int)g_windows.size(); ++wi) {
            Window& w = g_windows[wi];
            bool grabbed = false;
            for (auto& h : g_hand) if (h.win == wi && h.mode == GRAB_MOVE) grabbed = true;
            // outline: dots every ~1.5 cm
            Perimeter(w, per);
            bool selected = wi == g_selected;
            bool gazed = RayOnWin(wi);
            float oc[3] = { 0.45f, 0.45f, 0.45f };                 // unselected: grey outline only
            if (gazed) { oc[0] = oc[1] = oc[2] = 0.75f; }          // looked at: lighter
            if (selected) { oc[0] = 1; oc[1] = 1; oc[2] = 1; }
            if (grabbed) { oc[0] = 1; oc[1] = 0.9f; oc[2] = 0.2f; }
            else if (w.keep && w.hd <= 0) oc[2] *= 0.35f;            // flat keep window: gold-ish
            // Spacing widens on big windows (at most ~250 outline dots each).
            float perLen = 0;
            for (size_t i = 0; i < per.size(); ++i) {
                XMFLOAT2 a = per[i], b = per[(i + 1) % per.size()];
                perLen += sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            }
            const float gap = std::max(0.015f, perLen / 250), boxGap = std::max(0.03f, perLen / 100);
            for (size_t i = 0; i < per.size(); ++i) {
                XMFLOAT2 a = per[i], b = per[(i + 1) % per.size()];
                float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                int n = std::max(1, int(len / gap));
                for (int s = 0; s < n; ++s) {
                    float t = float(s) / n;
                    XMFLOAT3 p; XMStoreFloat3(&p, WinToWorld(w, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
                    g_uiInst.push_back({ p.x, p.y, p.z, (selected ? 0.003f : 0.002f) * FarScale(V(p)), oc[0], oc[1], oc[2], 1 });
                }
            }
            // Box: dimmer outlines of the front and back faces (every ~3 cm) and the corner edges.
            if (w.hd > 0) {
                float bc[3] = { oc[0] * 0.6f, oc[1] * 0.9f + 0.1f, oc[2] * 0.6f };   // slightly green
                if (w.keep) { bc[0] = oc[0] * 0.8f + 0.2f; bc[1] = oc[1] * 0.65f + 0.1f; bc[2] = oc[2] * 0.2f; }   // keep box: gold
                for (float z : { w.hd, -w.hd }) {
                    for (size_t i = 0; i < per.size(); ++i) {
                        XMFLOAT2 a = per[i], b = per[(i + 1) % per.size()];
                        float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                        int n = std::max(1, int(len / boxGap));
                        for (int s = 0; s < n; ++s) {
                            float t = float(s) / n;
                            XMFLOAT3 p; XMStoreFloat3(&p, WinToWorld(w, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, z));
                            g_uiInst.push_back({ p.x, p.y, p.z, 0.002f, bc[0], bc[1], bc[2], 1 });
                        }
                    }
                }
                // Edges joining front and back: at each rectangle corner, or at each shape point.
                int edges = IsShape(w) ? (int)w.pts.size() : 4;
                for (int k = 0; k < edges; ++k) {
                    float cx = IsShape(w) ? w.pts[k].x : w.ellipse ? kCS[k][0] * w.hw * 0.70710678f : kCS[k][0] * (w.hw - w.radius[k] * kArcK);
                    float cy = IsShape(w) ? w.pts[k].y : w.ellipse ? kCS[k][1] * w.hh * 0.70710678f : kCS[k][1] * (w.hh - w.radius[k] * kArcK);
                    int n = std::clamp(int(2 * w.hd / 0.03f), 1, 120);   // deep boxes: dots spread out
                    for (int s = 0; s <= n; ++s) {
                        XMFLOAT3 p; XMStoreFloat3(&p, WinToWorld(w, cx, cy, -w.hd + 2 * w.hd * float(s) / n));
                        g_uiInst.push_back({ p.x, p.y, p.z, 0.002f, bc[0], bc[1], bc[2], 1 });
                    }
                }
            }
            if (!selected) continue;
            // Handles: discs with a symbol for what they do. Move, corner and rounding handles lie in the
            // window's plane (their arrows point along it); the buttons above the window and the depth and
            // keep-box handles always face you (zero axes = face the eye), so they read from any angle.
            XMVECTOR wq = XMLoadFloat4(&w.rot);
            XMVECTOR wr = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), wq), wu = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), wq);
            const XMVECTOR face = XMVectorZero();
            auto icon = [&](XMVECTOR at, float r, IconId id, float cr, float cg, float cb, XMVECTOR ax, XMVECTOR ay,
                            const float* glyphCol = nullptr) {
                Inst u{};
                XMFLOAT3 p, a, b;
                XMStoreFloat3(&p, at); XMStoreFloat3(&a, ax); XMStoreFloat3(&b, ay);
                u.x = p.x; u.y = p.y; u.z = p.z; u.r = r * FarScale(at); u.cr = cr; u.cg = cg; u.cb = cb; u.ca = 1;
                u.ax = a.x; u.ay = a.y; u.az = a.z; u.icon = (float)id;
                u.bx = b.x; u.by = b.y; u.bz = b.z;
                if (glyphCol) { u.gr = glyphCol[0]; u.gg = glyphCol[1]; u.gb = glyphCol[2]; u.ga = 1; }
                g_uiInst.push_back(u);
            };
            // depth handle (green, wire box): pull towards you to make a box
            {
                bool active = false, hover = RayOn(GRAB_DEPTH, wi, -1);
                for (auto& h : g_hand) {
                    if (h.win == wi && h.mode == GRAB_DEPTH) active = true;
                    if (h.active && Dist(V(h.pinchPt), DepthHandle(w)) < kDepthHit) hover = true;
                }
                float dr = kDepthHandleR * (hover || active ? 1.25f : 1.0f);
                // dotted link from the edge out to the handle (spread like the handle on far windows)
                const float ss = SpreadSide(w);
                for (float x = 0.008f * ss; x < DepthOut(w) - dr * ss; x += 0.008f * ss) {
                    XMFLOAT3 p; XMStoreFloat3(&p, WinToWorld(w, w.hw + x, 0, 0));
                    g_uiInst.push_back({ p.x, p.y, p.z, 0.0018f * ss, 0.1f, 0.68f, 0.22f, 1 });
                }
                if (active) icon(DepthHandle(w), dr, ICON_DEPTH, 1, 0.9f, 0.2f, face, face);
                else icon(DepthHandle(w), dr, ICON_DEPTH, hover ? 0.3f : 0.1f, hover ? 0.9f : 0.68f, hover ? 0.4f : 0.22f, face, face);
            }
            auto gazeOn = [&](GrabMode m, int k) {
                return RayOn(m, wi, k);
            };
            // centre handle (blue, four-way arrow): move
            {
                bool active = false, hover = gazeOn(GRAB_MOVE, -1);
                for (auto& h : g_hand) {
                    if (h.win == wi && h.mode == GRAB_MOVE) active = true;
                    if (h.active && Dist(V(h.pinchPt), WinToWorld(w, 0, 0)) < kCenterHit) hover = true;
                }
                float cr = kCenterHandleR * (hover || active ? 1.25f : 1.0f);
                if (active) icon(WinToWorld(w, 0, 0), cr, ICON_MOVE, 1, 0.9f, 0.2f, wr, wu);
                else icon(WinToWorld(w, 0, 0), cr, ICON_MOVE, hover ? 0.35f : 0.15f, hover ? 0.65f : 0.45f, 1, wr, wu);
            }
            auto handHover = [&](XMVECTOR at, float hit) {
                for (auto& h : g_hand) if (h.active && Dist(V(h.pinchPt), at) < hit) return true;
                return false;
            };
            // type button (purple): shows the outline it switches to (free shape, or back to a rectangle)
            {
                XMVECTOR at = TargetPoint(w, GRAB_TYPE, -1);
                bool hover = gazeOn(GRAB_TYPE, -1) || handHover(at, kButtonHit);
                float s = hover ? 1.0f : 0.8f;
                icon(at, kButtonR * (hover ? 1.25f : 1.0f), IsShape(w) ? ICON_TO_RECT : ICON_TO_SHAPE,
                     0.5f * s, 0.25f * s, 1.0f * s, face, face);
            }
            // keep-box button (solid box, the inverse of the depth handle): white with a green box while
            // it's a keep box, grey while off
            {
                XMVECTOR at = TargetPoint(w, GRAB_KEEPBTN, -1);
                bool hover = gazeOn(GRAB_KEEPBTN, -1) || handHover(at, kButtonHit);
                static const float gOn[3] = { 0.1f, 0.68f, 0.22f }, gOff[3] = { 0.16f, 0.55f, 0.26f };
                float b = w.keep ? 0.95f : (hover ? 0.62f : 0.42f);
                icon(at, kButtonR * (hover || w.keep ? 1.25f : 1.0f), ICON_KEEP, b, b, b, face, face, w.keep ? gOn : gOff);
            }
            if (w.hd > 0) {
                // box faces button (boxes only): its colour says the face mode, each press the next
                // (green whole box, orange top + far wall, blue far wall + floor, purple all but front + floor)
                XMVECTOR at = TargetPoint(w, GRAB_FACESBTN, -1);
                bool hover = gazeOn(GRAB_FACESBTN, -1) || handHover(at, kButtonHit);
                const float* c = kBoxModeCol[std::clamp(w.boxMode, 0, 3)];
                float s = hover ? 1.15f : 0.9f;
                icon(at, kButtonR * (hover ? 1.25f : 1.0f), ICON_FACES, std::min(1.0f, c[0] * s), std::min(1.0f, c[1] * s),
                     std::min(1.0f, c[2] * s), face, face);
            }
            if (!IsShape(w)) {
                // circle button (teal): shows what it switches to (a circle, or back to a rectangle)
                XMVECTOR at = TargetPoint(w, GRAB_CIRCLEBTN, -1);
                bool hover = gazeOn(GRAB_CIRCLEBTN, -1) || handHover(at, kButtonHit);
                float s = hover ? 1.0f : 0.82f;
                icon(at, kButtonR * (hover ? 1.25f : 1.0f), w.ellipse ? ICON_TO_RECT : ICON_TO_CIRCLE,
                     0.05f * s, 0.62f * s, 0.7f * s, face, face);
            }
            {
                // lock button: gold with a closed padlock while the angle is locked, grey and open while not
                XMVECTOR at = TargetPoint(w, GRAB_LOCKBTN, -1);
                bool hover = gazeOn(GRAB_LOCKBTN, -1) || handHover(at, kButtonHit);
                if (w.lock) icon(at, kButtonR * 1.25f, ICON_LOCK, 0.98f, 0.76f, 0.12f, face, face);
                else { float b = hover ? 0.62f : 0.42f; icon(at, kButtonR * (hover ? 1.25f : 1.0f), ICON_UNLOCK, b, b, b, face, face); }
            }
            {
                // snap button (pale blue, upright post): stand it upright / lay it level, square to the world
                XMVECTOR at = TargetPoint(w, GRAB_SNAPBTN, -1);
                bool hover = gazeOn(GRAB_SNAPBTN, -1) || handHover(at, kButtonHit);
                static const float dark[3] = { 0.08f, 0.09f, 0.11f };
                float s = hover ? 1.0f : 0.85f;
                icon(at, kButtonR * (hover ? 1.25f : 1.0f), ICON_SNAP, 0.55f * s, 0.8f * s, 1.0f * s, face, face, dark);
            }
            if (IsShape(w)) {
                // draw button (white pencil button; red while armed)
                XMVECTOR at = TargetPoint(w, GRAB_DRAWBTN, -1);
                bool hover = gazeOn(GRAB_DRAWBTN, -1) || handHover(at, kButtonHit);
                bool armed = g_drawArmed == wi;
                icon(at, kButtonR * (hover || armed ? 1.25f : 1.0f), ICON_DRAW,
                     armed ? 0.95f : 0.92f, armed ? 0.2f : 0.92f, armed ? 0.2f : 0.92f, face, face);
                // points (orange) and edge handles (small, pale, plus: pinch to add a point)
                for (int k = 0; k < (int)w.pts.size(); ++k) {
                    bool active = false;
                    for (auto& h : g_hand) if (h.win == wi && h.corner == k && h.mode == GRAB_POINT) active = true;
                    XMVECTOR pa = TargetPoint(w, GRAB_POINT, k);
                    bool ph = gazeOn(GRAB_POINT, k) || handHover(pa, kPointHit);
                    icon(pa, kPointR * (ph || active ? 1.25f : 1.0f), ICON_POINT, 1, active ? 1.0f : (ph ? 0.7f : 0.45f), active ? 0.5f : 0, wr, wu);
                    XMVECTOR ma = TargetPoint(w, GRAB_ADDPT, k);
                    bool mh = gazeOn(GRAB_ADDPT, k) || handHover(ma, kMidHit);
                    icon(ma, kMidR * (mh ? 1.4f : 1.0f), ICON_ADD, 1, 0.78f, 0.55f, wr, wu);
                }
                continue;   // shapes have no corner or rounding handles
            }
            for (int k = 0; k < 4; ++k) {
                bool active = false, hover = gazeOn(GRAB_CORNER, k);
                for (auto& h : g_hand) {
                    if (h.win == wi && h.corner == k && h.mode == GRAB_CORNER) active = true;
                    if (h.active && Dist(V(h.pinchPt), WinToWorld(w, kCS[k][0] * w.hw, kCS[k][1] * w.hh)) < kCornerHit) hover = true;
                }
                // corner (orange, diagonal arrow pointing out of the corner): resize
                float sx = kCS[k][0], sy = kCS[k][1];
                XMVECTOR diag = XMVector3Normalize(wr * sx + wu * sy), perp = XMVector3Normalize(wu * sx - wr * sy);
                float cr = kCornerHandleR * (hover || active ? 1.25f : 1.0f);
                icon(WinToWorld(w, sx * w.hw, sy * w.hh), cr, ICON_RESIZE, 1, active ? 1.0f : (hover ? 0.7f : 0.45f), active ? 0.5f : 0, diag, perp);

                if (w.ellipse) continue;   // circles have no rounding handles
                XMFLOAT2 rh = RadiusHandleLocal(w, k);
                bool rActive = false, rHover = gazeOn(GRAB_RADIUS, k);
                for (auto& h : g_hand) {
                    if (h.win == wi && h.corner == k && h.mode == GRAB_RADIUS) rActive = true;
                    if (h.active && Dist(V(h.pinchPt), WinToWorld(w, rh.x, rh.y)) < kRadiusHit) rHover = true;
                }
                // inside the corner (magenta, a curve facing its corner): round that corner
                float rr = kRadiusHandleR * (rHover || rActive ? 1.25f : 1.0f);
                icon(WinToWorld(w, rh.x, rh.y), rr, ICON_ROUND, 1, 0.2f, 1, wr * sx, wu * sy);
            }
        }
        // the outline being traced (white dots on the window's plane)
        for (auto& h : g_hand) {
            if (h.mode != GRAB_TRACE || h.win < 0 || h.win >= (int)g_windows.size()) continue;
            size_t step = std::max<size_t>(1, g_trace.size() / 250);   // keep within the dot budget
            for (size_t ti = 0; ti < g_trace.size(); ti += step) {
                const XMFLOAT2& t = g_trace[ti];
                XMFLOAT3 p; XMStoreFloat3(&p, WinToWorld(g_tracePlane, t.x, t.y));
                g_uiInst.push_back({ p.x, p.y, p.z, 0.003f, 1, 1, 1, 1 });
            }
            // Yellow start dot: come back to it to finish the outline. Grows and turns green once
            // you're near it and far enough round for it to close.
            if (!g_trace.empty()) {
                const XMFLOAT2& s = g_trace[0], & e = g_trace.back();
                float d = sqrtf((e.x - s.x) * (e.x - s.x) + (e.y - s.y) * (e.y - s.y));
                bool closing = TraceCanClose(g_trace) && d < 2.5f * kTraceClose;
                XMFLOAT3 p; XMStoreFloat3(&p, WinToWorld(g_tracePlane, s.x, s.y));
                g_uiInst.push_back({ p.x, p.y, p.z, closing ? 0.014f : 0.009f, closing ? 0.2f : 1, closing ? 1 : 0.85f, closing ? 0.3f : 0.1f, 1 });
            }
        }
        // pinch cursors
        for (auto& h : g_hand) {
            if (!h.active) continue;
            g_uiInst.push_back({ h.pinchPt.x, h.pinchPt.y, h.pinchPt.z, 0.004f,
                                 h.pinching ? 0.2f : 0.6f, h.pinching ? 1.0f : 0.6f, h.pinching ? 0.2f : 0.6f, 1 });
        }
    }

    // --- tight hand cutouts
    if (g_handsEnabled) {
        static const int chains[5][6] = {
            { XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
              XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT, -1 },
            { XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
              XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT },
            { XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
              XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT },
            { XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT,
              XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT },
            { XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
              XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT },
        };
        // Across the palm: neighbouring metacarpal and knuckle joints.
        static const int across[][2] = {
            { XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_METACARPAL_EXT },
            { XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_RING_METACARPAL_EXT },
            { XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_METACARPAL_EXT },
            { XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT },
            { XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT },
            { XR_HAND_JOINT_RING_PROXIMAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT },
            { XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_INDEX_METACARPAL_EXT },
            { XR_HAND_JOINT_THUMB_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT },
        };
        float col[4] = { 0, 0, 0, 0 };
        if (UseKeyColour()) { col[0] = kKeyCol[g_keyMode][0]; col[1] = kKeyCol[g_keyMode][1]; col[2] = kKeyCol[g_keyMode][2]; col[3] = 1; }
        else if (g_visualize) { col[1] = 1; col[2] = 1; col[3] = 0.5f; }
        for (auto& h : g_hand) {
            if (!h.active || h.isCtl) continue;   // controllers have no joints
            static bool loggedRadii = false;
            if (!loggedRadii) {
                Log("Joint radii (mm): palm %.1f wrist %.1f index prox %.1f index tip %.1f thumb tip %.1f, scale %.2f\n",
                    h.j[XR_HAND_JOINT_PALM_EXT].radius * 1000, h.j[XR_HAND_JOINT_WRIST_EXT].radius * 1000,
                    h.j[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].radius * 1000, h.j[XR_HAND_JOINT_INDEX_TIP_EXT].radius * 1000,
                    h.j[XR_HAND_JOINT_THUMB_TIP_EXT].radius * 1000, g_handScale);
                loggedRadii = true;
            }
            // PICO reports 15 mm for every joint, which is far too big for fingers, so use per-joint
            // sizes (mm) instead, times the user scale (Ctrl+Alt -/=). Tuned by eye: halfway between
            // typical adult finger half-thickness (clipped) and a flat 18.5 mm (too big).
            static const float kJointRadiusMM[XR_HAND_JOINT_COUNT_EXT] = {
                15.75f, 16.75f,                         // palm, wrist
                15.75f, 14.75f, 14.0f, 13.5f,           // thumb: metacarpal, proximal, distal, tip
                15.25f, 14.25f, 13.5f, 13.0f, 12.75f,   // index: metacarpal, proximal, intermediate, distal, tip
                15.25f, 14.5f, 13.75f, 13.25f, 13.0f,   // middle
                15.0f, 14.25f, 13.5f, 13.0f, 12.75f,    // ring
                14.75f, 13.5f, 13.0f, 12.5f, 12.25f,    // little
            };
            auto rad = [&](int k) { return kJointRadiusMM[k] * 0.001f * g_handScale; };
            // Tip joints sit at the very end of the finger, so pull their spheres back by the
            // radius to stop the cutout poking past the fingertip.
            auto pos = [&](int k) {
                bool tip = k == XR_HAND_JOINT_THUMB_TIP_EXT || k == XR_HAND_JOINT_INDEX_TIP_EXT ||
                           k == XR_HAND_JOINT_MIDDLE_TIP_EXT || k == XR_HAND_JOINT_RING_TIP_EXT ||
                           k == XR_HAND_JOINT_LITTLE_TIP_EXT;
                XMVECTOR p = JP(h, k);
                if (tip && JValid(h, k - 1)) {
                    XMVECTOR d = p - JP(h, k - 1);
                    float len = XMVectorGetX(XMVector3Length(d));
                    if (len > 1e-4f) p -= d * (std::min(rad(k), len * 0.5f) / len);
                }
                return p;
            };
            auto add = [&](XMVECTOR p, float r) {
                XMFLOAT3 f; XMStoreFloat3(&f, p);
                g_maskInst.push_back({ f.x, f.y, f.z, r, col[0], col[1], col[2], col[3] });
            };
            auto segment = [&](int a, int b) {
                if (!JValid(h, a) || !JValid(h, b)) return;
                XMVECTOR pa = pos(a), pb = pos(b);
                float ra = rad(a), rb = rad(b);
                int steps = std::min(16, int(Dist(pa, pb) / (0.5f * std::min(ra, rb))));
                for (int i = 1; i <= steps; ++i) {
                    float t = float(i) / float(steps + 1);
                    add(pa + (pb - pa) * t, ra + (rb - ra) * t);
                }
            };
            for (int k = 0; k < XR_HAND_JOINT_COUNT_EXT; ++k) if (JValid(h, k)) add(pos(k), rad(k));
            for (auto& c : chains) for (int s = 0; s < 5 && c[s + 1] >= 0; ++s) segment(c[s], c[s + 1]);
            for (auto& a : across) segment(a[0], a[1]);
        }
    }

    if (g_maskInst.size() > kMaxInst / 2) g_maskInst.resize(kMaxInst / 2);
    if (g_uiInst.size() > kMaxInst / 2) {
        // Over budget: thin out the small outline / laser dots evenly, never handles or buttons
        // (icons) or other big markers, so those never go missing.
        auto minor = [](const Inst& u) { return u.icon == 0 && u.r < 0.0035f; };
        size_t major = 0;
        for (auto& u : g_uiInst) if (!minor(u)) ++major;
        size_t room = kMaxInst / 2 > major ? kMaxInst / 2 - major : 0, nMinor = g_uiInst.size() - major;
        std::vector<Inst> kept;
        kept.reserve(kMaxInst / 2);
        size_t seen = 0, taken = 0;
        for (auto& u : g_uiInst) {
            if (!minor(u)) { if (kept.size() < kMaxInst / 2) kept.push_back(u); continue; }
            // keep minor dot number "seen" if it brings us up to its fair share
            if (taken < room && (seen++ * room) / nMinor >= taken) { kept.push_back(u); ++taken; }
        }
        g_uiInst.swap(kept);
    }
    UpdateGuide(space, head, dt, now);
}

// Always draw, even with no cutouts: the first pass forces the game's image fully opaque, so
// passthrough only ever shows through our windows and hands. Without this, hiding the hands or
// losing hand tracking (picking up controllers) would let a game's own transparent pixels through.
// Without any way to show passthrough, only edit mode (dots, guide) is worth drawing.
static bool HasAnythingToDraw() { return CutoutsShown() || g_edit; }

// ================================================================ drawing
#ifdef _WIN32
static ID3D11RenderTargetView* GetRTV(SwapchainInfo& sc, uint32_t image, uint32_t slice) {
    uint64_t key = (uint64_t(image) << 32) | slice;
    auto it = sc.rtvs.find(key);
    if (it != sc.rtvs.end()) return it->second;
    if (image >= sc.images.size()) return nullptr;
    D3D11_TEXTURE2D_DESC td; sc.images[image]->GetDesc(&td);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = (DXGI_FORMAT)sc.format;
    if (td.ArraySize > 1) {
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rd.Texture2DArray.FirstArraySlice = slice;
        rd.Texture2DArray.ArraySize = 1;
    } else {
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    }
    ID3D11RenderTargetView* rtv = nullptr;
    HRESULT hr = g_dev->CreateRenderTargetView(sc.images[image], &rd, &rtv);
    if (FAILED(hr)) { Log("CreateRenderTargetView failed 0x%08X (format %d)\n", (unsigned)hr, (int)sc.format); rtv = nullptr; }
    sc.rtvs[key] = rtv;
    return rtv;
}
#endif

// The game's depth for one view (from XrCompositionLayerDepthInfoKHR in its last xrEndFrame).
struct ViewDepth {
    bool valid = false;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    XrRect2Di rect{};
    uint32_t slice = 0;
    float minDepth = 0, maxDepth = 1, nearZ = 0.1f, farZ = 100;
};

struct DrawView {
    XrSwapchain swapchain;
    XrRect2Di rect;
    uint32_t slice;
    XrPosef pose;
    XrFovf fov;
    ViewDepth depth;
};

// psBox's constants for one view (cbuffers B and W): the view's rects, fov and depth mapping, and
// every window in this eye's view space. Returns false if the game's depth can't be used after all
// (an empty depth range); then info.x = 0 and psBox works without it.
static bool FillBoxCB(const DrawView& pv, XMMATRIX cam, bool haveDepth, const XMFLOAT4& col, CBBox& bb, CBWin& wb) {
    const XrRect2Di& r = pv.rect;
    const ViewDepth& vd = pv.depth;
    bb = CBBox{};
    bb.rectC = { (float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height };
    bb.rectD = { (float)vd.rect.offset.x, (float)vd.rect.offset.y, (float)vd.rect.extent.width, (float)vd.rect.extent.height };
    bb.tanLRDU = { tanf(pv.fov.angleLeft), tanf(pv.fov.angleRight), tanf(pv.fov.angleDown), tanf(pv.fov.angleUp) };
    auto inv = [](float z) { return (std::isfinite(z) && z > 0) ? 1.0f / z : 0.0f; };
    float range = vd.maxDepth - vd.minDepth;
    bb.depthMap = { vd.minDepth, fabsf(range) > 1e-6f ? 1.0f / range : 0.0f, inv(vd.nearZ), inv(vd.farZ) };
    bb.col = col;
    if (fabsf(range) <= 1e-6f) haveDepth = false;
    bool anyFaces = false;
    int nw = std::min((int)g_windows.size(), kMaxWindows);
    wb.count = { (float)nw, 0, 0, 0 };
    for (int wi = 0; wi < nw; ++wi) {
        const Window& w = g_windows[wi];
        WinGPU& g = wb.win[wi];
        XMStoreFloat4x4(&g.toLocal, XMMatrixTranspose(cam * XMMatrixInverse(nullptr, WinMatrix(w))));
        float kind = w.keep ? 2.0f : (w.hd > 0 ? 1.0f : 0.0f);
        g.dim = { w.hw, w.hh, w.hd, kind };
        g.radii = { w.radius[0], w.radius[1], w.radius[2], w.radius[3] };
        int np = IsShape(w) ? (int)std::min<size_t>(w.pts.size(), kMaxShapePts) : 0;
        float faceMode = w.hd > 0 ? (float)std::clamp(w.boxMode, 0, 3) : 0.0f;
        if (faceMode > 0) anyFaces = true;
        g.shape = { (float)np, (!np && w.ellipse) ? 1.0f : 0.0f, 8 * w.hw * w.hh * w.hd, faceMode };
        for (int k = 0; k < np; ++k) {
            float* q = &g.pts[k / 2].x + (k & 1) * 2;
            q[0] = w.pts[k].x; q[1] = w.pts[k].y;
        }
    }
    bb.info = { haveDepth ? 1.0f : 0.0f, anyFaces ? 1.0f : 0.0f, 0, 0 };
    return haveDepth;
}

// Key colour mode: darken edit handles close to the key colour so the streamer doesn't key them out.
static void DarkenKeyedHandles(Inst* ui, UINT n) {
    if (!UseKeyColour()) return;
    const float* k = kKeyCol[g_keyMode];
    for (UINT i = 0; i < n; ++i) {
        float dr = ui[i].cr - k[0], dg = ui[i].cg - k[1], db = ui[i].cb - k[2];
        if (dr * dr + dg * dg + db * db < 0.5f) { ui[i].cr *= 0.45f; ui[i].cg *= 0.45f; ui[i].cb *= 0.45f; }
        float gr = ui[i].gr - k[0], gg = ui[i].gg - k[1], gb = ui[i].gb - k[2];
        if (ui[i].ga > 0 && gr * gr + gg * gg + gb * gb < 0.5f) { ui[i].gr *= 0.45f; ui[i].gg *= 0.45f; ui[i].gb *= 0.45f; }
    }
}

#ifdef _WIN32
// Typeless format to copy a depth image into, and the format our shader reads it as.
static bool DepthCopyFormats(DXGI_FORMAT f, DXGI_FORMAT& typeless, DXGI_FORMAT& srv) {
    switch (f) {
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT:
        typeless = DXGI_FORMAT_R32_TYPELESS; srv = DXGI_FORMAT_R32_FLOAT; return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS:
        typeless = DXGI_FORMAT_R24G8_TYPELESS; srv = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; return true;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM:
        typeless = DXGI_FORMAT_R16_TYPELESS; srv = DXGI_FORMAT_R16_UNORM; return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS:
        typeless = DXGI_FORMAT_R32G8X24_TYPELESS; srv = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; return true;
    default: return false;
    }
}

// Copy the game's current depth image (one array slice) into our own readable texture.
// The game renders depth together with colour, so when it releases the colour image the depth
// image it is holding is already drawn.
static ID3D11ShaderResourceView* GetGameDepth(const ViewDepth& vd) {
    if (!vd.valid) return nullptr;   // D3D12: the image is a wrapped resource, acquired in DrawMask
    auto it = g_swapchains.find(vd.swapchain);
    if (it == g_swapchains.end() || !it->second.depth) return nullptr;
    SwapchainInfo& sc = it->second;
    if (sc.lastAcquired >= sc.images.size() || !sc.images[sc.lastAcquired]) return nullptr;
    ID3D11Texture2D* src = sc.images[sc.lastAcquired];
    DepthCopy& dc = sc.depthCopies[vd.slice];
    if (dc.failed) return nullptr;
    D3D11_TEXTURE2D_DESC td; src->GetDesc(&td);
    if (!dc.tex) {
        DXGI_FORMAT tl, sf;
        if (td.SampleDesc.Count > 1 || !DepthCopyFormats(td.Format, tl, sf) || vd.slice >= td.ArraySize) {
            Log("Game depth can't be read (format %d, %u samples, slice %u of %u): boxes act as solid blocks.\n",
                (int)td.Format, td.SampleDesc.Count, vd.slice, td.ArraySize);
            dc.failed = true; return nullptr;
        }
        D3D11_TEXTURE2D_DESC cd{};
        cd.Width = td.Width; cd.Height = td.Height; cd.MipLevels = 1; cd.ArraySize = 1;
        cd.Format = tl; cd.SampleDesc.Count = 1; cd.Usage = D3D11_USAGE_DEFAULT;
        cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = sf; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        HRESULT hr = g_dev->CreateTexture2D(&cd, nullptr, &dc.tex);
        if (SUCCEEDED(hr)) hr = g_dev->CreateShaderResourceView(dc.tex, &sd, &dc.srv);
        if (FAILED(hr)) {
            Log("Depth copy texture failed 0x%08X: boxes act as solid blocks.\n", (unsigned)hr);
            SafeRelease(dc.srv); SafeRelease(dc.tex); dc.failed = true; return nullptr;
        }
        Log("Reading game depth: %ux%u format %d, slice %u, depth %.2f..%.2f maps to %.3f..%.1f m\n",
            td.Width, td.Height, (int)td.Format, vd.slice, vd.minDepth, vd.maxDepth, vd.nearZ, vd.farZ);
    }
    // Depth images must be copied whole (no sub-rectangle).
    g_ctx->CopySubresourceRegion(dc.tex, 0, 0, 0, 0, src, D3D11CalcSubresource(0, vd.slice, td.MipLevels), nullptr);
    return dc.srv;
}

static void DrawMaskD3D(const std::vector<DrawView>& dviews) {
    ID3DDeviceContextState* prev = nullptr;
    g_ctx->SwapDeviceContextState(g_state, &prev);

    UINT maskCount = (UINT)g_maskInst.size(), uiCount = (UINT)g_uiInst.size();
    if (maskCount + uiCount) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_instVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, g_maskInst.data(), maskCount * sizeof(Inst));
            Inst* ui = (Inst*)m.pData + maskCount;
            memcpy(ui, g_uiInst.data(), uiCount * sizeof(Inst));
            DarkenKeyedHandles(ui, uiCount);
            g_ctx->Unmap(g_instVB, 0);
        } else {
            maskCount = uiCount = 0;
        }
    }
    UINT winVerts = (UINT)g_winTris.size();
    if (winVerts) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_winVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, g_winTris.data(), winVerts * sizeof(XMFLOAT3));
            g_ctx->Unmap(g_winVB, 0);
        } else {
            winVerts = 0;
        }
    }

    // Edit-mode guide: upload new text, and the panel's corners for this frame.
    bool drawGuide = g_guideShow && g_guideSrvSrgb && g_guideSrvUnorm && g_panelVB && g_vsPanel && g_psPanel && g_ilPanel;
    if (drawGuide && g_guideDirty && g_guidePixels.size() == size_t(kGuideW) * kGuideH) {
        g_ctx->UpdateSubresource(g_guideTex, 0, nullptr, g_guidePixels.data(), kGuideW * 4, 0);
        g_ctx->GenerateMips(g_guideSrvUnorm);
        g_guideDirty = false;
        g_guideUploaded = true;
    }
    drawGuide = drawGuide && g_guideUploaded;
    if (drawGuide) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_panelVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            static const float uv[4][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };
            float* v = (float*)m.pData;
            for (int k = 0; k < 4; ++k) {
                *v++ = g_guideCorner[k].x; *v++ = g_guideCorner[k].y; *v++ = g_guideCorner[k].z;
                *v++ = uv[k][0]; *v++ = uv[k][1];
            }
            g_ctx->Unmap(g_panelVB, 0);
        } else drawGuide = false;
    }

    // D3D12 games: borrow the swapchain images being drawn to (and the depth images read) from
    // D3D12 for the duration.
    static std::vector<ID3D11Resource*> wrapped;
    wrapped.clear();
    if (g_on12) {
        auto borrow = [&](XrSwapchain sc) {
            auto it = g_swapchains.find(sc);
            if (it == g_swapchains.end() || it->second.lastAcquired >= it->second.images.size()) return;
            ID3D11Resource* t = it->second.images[it->second.lastAcquired];
            if (t && std::find(wrapped.begin(), wrapped.end(), t) == wrapped.end()) wrapped.push_back(t);
        };
        for (const DrawView& pv : dviews) {
            borrow(pv.swapchain);
            if (pv.depth.valid) borrow(pv.depth.swapchain);
        }
        if (!wrapped.empty()) g_on12->AcquireWrappedResources(wrapped.data(), (UINT)wrapped.size());
    }

    const bool keyed = UseKeyColour(), shown = CutoutsShown();
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->RSSetState(g_rs);
    g_ctx->OMSetDepthStencilState(g_dss, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);

    for (const DrawView& pv : dviews) {
        auto it = g_swapchains.find(pv.swapchain);
        if (it == g_swapchains.end()) continue;
        ID3D11RenderTargetView* rtv = GetRTV(it->second, it->second.lastAcquired, pv.slice);
        if (!rtv) continue;
        // The game's depth for this view, copied at most once (boxes and hands both use it).
        bool depthFetched = false;
        ID3D11ShaderResourceView* viewDepth = nullptr;
        auto ViewDepthSRV = [&]() { if (!depthFetched) { viewDepth = GetGameDepth(pv.depth); depthFetched = true; } return viewDepth; };

        const XrRect2Di& r = pv.rect;
        D3D11_VIEWPORT vp{ (float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height, 0, 1 };
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->OMSetRenderTargets(1, &rtv, nullptr);

        const XrPosef& p = pv.pose;
        XMMATRIX cam = XMMatrixAffineTransformation(XMVectorSet(1, 1, 1, 0), XMVectorZero(), Q(p.orientation),
            XMVectorSet(p.position.x, p.position.y, p.position.z, 1));
        const float n = 0.02f;
        XMMATRIX proj = XMMatrixPerspectiveOffCenterRH(tanf(pv.fov.angleLeft) * n, tanf(pv.fov.angleRight) * n,
            tanf(pv.fov.angleDown) * n, tanf(pv.fov.angleUp) * n, n, 100.0f);
        XMMATRIX viewProj = XMMatrixInverse(nullptr, cam) * proj;

        CB cb{};
        XMStoreFloat4(&cb.camR, cam.r[0]);
        XMStoreFloat4(&cb.camU, cam.r[1]);
        // 1. Alpha mode: force alpha = 1 everywhere (games often leave junk in alpha).
        //    Key colour mode keeps the game's alpha (the layer isn't alpha blended then).
        if (!keyed) {
            XMStoreFloat4x4(&cb.mvp, XMMatrixIdentity());
            cb.col = { 0, 0, 0, 1 };
            g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
            g_ctx->OMSetBlendState(g_bsAlphaOnly, nullptr, 0xffffffff);
            g_ctx->VSSetShader(g_vsFull, nullptr, 0);
            g_ctx->PSSetShader(g_psConst, nullptr, 0);
            g_ctx->Draw(3, 0);
        }

        XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(viewProj));

        // 2. Windows: alpha 0 = passthrough (green tint when visualizing), or a solid key colour.
        //    Box windows use the same colour, so depth boxes key out only where the game draws inside them.
        if (winVerts && shown) {
            cb.col = keyed ? XMFLOAT4{ kKeyCol[g_keyMode][0], kKeyCol[g_keyMode][1], kKeyCol[g_keyMode][2], 1 }
                   : g_visualize ? XMFLOAT4{ 0, 1, 0, 0.5f } : XMFLOAT4{ 0, 0, 0, 0 };
            g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
            g_ctx->OMSetBlendState(keyed || g_visualize ? g_bsAll : g_bsAlphaOnly, nullptr, 0xffffffff);
            UINT stride = sizeof(XMFLOAT3), off = 0;
            g_ctx->IASetInputLayout(g_ilMesh);
            g_ctx->IASetVertexBuffers(0, 1, &g_winVB, &stride, &off);
            g_ctx->VSSetShader(g_vsMesh, nullptr, 0);
            if (g_boxDraws.empty()) {   // flat windows only: cut their whole outline
                g_ctx->PSSetShader(g_psConst, nullptr, 0);
                g_ctx->Draw(g_flatVerts, 0);
            } else {
                // Any box: every window's triangles go through psBox, which looks at all windows per
                // pixel, so overlapping windows combine (the innermost box containing the game's
                // surface decides it: normal box = passthrough, keep box = the game stays).
                ID3D11ShaderResourceView* depth = ViewDepthSRV();
                int ds = depth ? 1 : 0;
                if (ds != g_depthRead)   // log changes only (reset each session)
                    Log(depth ? "Box windows: using the game's depth.\n"
                              : "Box windows: no game depth this view, boxes act as solid blocks.\n");
                g_depthRead = ds;
                static CBBox bb;
                static CBWin wb;   // ~12 KB: not on the stack
                if (!FillBoxCB(pv, cam, depth != nullptr, cb.col, bb, wb)) depth = nullptr;
                g_ctx->UpdateSubresource(g_cbBox, 0, nullptr, &bb, 0, 0);
                g_ctx->UpdateSubresource(g_cbWin, 0, nullptr, &wb, 0, 0);
                ID3D11Buffer* cbs[2] = { g_cbBox, g_cbWin };
                g_ctx->PSSetConstantBuffers(1, 2, cbs);
                g_ctx->PSSetShader(g_psBox, nullptr, 0);
                g_ctx->PSSetShaderResources(0, 1, &depth);
                g_ctx->Draw(winVerts, 0);
                ID3D11ShaderResourceView* none = nullptr;
                g_ctx->PSSetShaderResources(0, 1, &none);
            }
        }

        // 2b. Edit-mode guide panel: solid (alpha 1), over windows, under hands and handles.
        if (drawGuide) {
            g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
            DXGI_FORMAT f = (DXGI_FORMAT)it->second.format;
            bool linearRT = f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                            f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || f == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                            f == DXGI_FORMAT_R11G11B10_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT;
            ID3D11ShaderResourceView* srv = linearRT ? g_guideSrvSrgb : g_guideSrvUnorm;
            UINT stride = 5 * sizeof(float), off = 0;
            g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            g_ctx->IASetInputLayout(g_ilPanel);
            g_ctx->IASetVertexBuffers(0, 1, &g_panelVB, &stride, &off);
            g_ctx->VSSetShader(g_vsPanel, nullptr, 0);
            g_ctx->PSSetShader(g_psPanel, nullptr, 0);
            g_ctx->PSSetShaderResources(1, 1, &srv);
            g_ctx->PSSetSamplers(0, 1, &g_sampLinear);
            g_ctx->OMSetBlendState(g_bsAll, nullptr, 0xffffffff);
            g_ctx->Draw(4, 0);
            ID3D11ShaderResourceView* none = nullptr;
            g_ctx->PSSetShaderResources(1, 1, &none);
            g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        // 3 + 4. Hand cutouts, then visible edit handles on top.
        if (maskCount || uiCount) {
            g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
            ID3D11Buffer* vbs[2] = { g_sphereVB, g_instVB };
            UINT strides[2] = { 12, sizeof(Inst) }, offs[2] = { 0, 0 };
            g_ctx->IASetInputLayout(g_ilSphere);
            g_ctx->IASetVertexBuffers(0, 2, vbs, strides, offs);
            g_ctx->IASetIndexBuffer(g_sphereIB, DXGI_FORMAT_R16_UINT, 0);
            g_ctx->VSSetShader(g_vsSphere, nullptr, 0);
            g_ctx->PSSetShader(g_psColor, nullptr, 0);
            if (maskCount && shown) {
                g_ctx->OMSetBlendState(keyed || g_visualize ? g_bsAll : g_bsAlphaOnly, nullptr, 0xffffffff);
                // Hands behind game objects: test each pixel against the game's depth.
                ID3D11ShaderResourceView* hdepth = (g_handOcclude && g_vsSphereD && g_psHandD) ? ViewDepthSRV() : nullptr;
                const ViewDepth& vd = pv.depth;
                float range = vd.maxDepth - vd.minDepth;
                if (hdepth && fabsf(range) > 1e-6f) {
                    auto inv = [](float z) { return (std::isfinite(z) && z > 0) ? 1.0f / z : 0.0f; };
                    CBBox hb{};
                    hb.rectC = { (float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height };
                    hb.rectD = { (float)vd.rect.offset.x, (float)vd.rect.offset.y, (float)vd.rect.extent.width, (float)vd.rect.extent.height };
                    hb.depthMap = { vd.minDepth, 1.0f / range, inv(vd.nearZ), inv(vd.farZ) };
                    hb.info = { 1, 0, 0, 0 };
                    g_ctx->UpdateSubresource(g_cbBox, 0, nullptr, &hb, 0, 0);
                    g_ctx->PSSetConstantBuffers(1, 1, &g_cbBox);
                    g_ctx->PSSetShaderResources(0, 1, &hdepth);
                    g_ctx->VSSetShader(g_vsSphereD, nullptr, 0);
                    g_ctx->PSSetShader(g_psHandD, nullptr, 0);
                    g_ctx->DrawIndexedInstanced(g_sphereIndexCount, maskCount, 0, 0, 0);
                    ID3D11ShaderResourceView* none = nullptr;
                    g_ctx->PSSetShaderResources(0, 1, &none);
                    g_ctx->VSSetShader(g_vsSphere, nullptr, 0);
                    g_ctx->PSSetShader(g_psColor, nullptr, 0);
                    if (g_handDepthUsed != 1) Log("Hands behind game objects: using the game's depth.\n");
                    g_handDepthUsed = 1;
                } else {
                    if (g_handOcclude && g_handDepthUsed != 0) Log("Hands behind game objects: no game depth in this view, hands drawn in front.\n");
                    g_handDepthUsed = g_handOcclude ? 0 : -1;
                    g_ctx->DrawIndexedInstanced(g_sphereIndexCount, maskCount, 0, 0, 0);
                }
            }
            if (uiCount && g_vsIcon && g_psIcon && g_ilIcon && g_quadVB && g_bsBlend) {
                ID3D11Buffer* ivbs[2] = { g_quadVB, g_instVB };
                UINT istrides[2] = { 8, sizeof(Inst) };
                g_ctx->IASetInputLayout(g_ilIcon);
                g_ctx->IASetVertexBuffers(0, 2, ivbs, istrides, offs);
                g_ctx->VSSetShader(g_vsIcon, nullptr, 0);
                g_ctx->PSSetShader(g_psIcon, nullptr, 0);
                g_ctx->OMSetBlendState(g_bsBlend, nullptr, 0xffffffff);
                g_ctx->DrawInstanced(6, uiCount, 0, maskCount);
            } else if (uiCount) {
                g_ctx->OMSetBlendState(g_bsAll, nullptr, 0xffffffff);
                g_ctx->DrawIndexedInstanced(g_sphereIndexCount, uiCount, 0, 0, maskCount);
            }
        }
    }

    if (g_on12) {
        // Hand the images back and submit now, so our drawing lands on the game's queue
        // before the runtime reads the image.
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (!wrapped.empty()) g_on12->ReleaseWrappedResources(wrapped.data(), (UINT)wrapped.size());
        g_ctx->Flush();
    }
    g_ctx->SwapDeviceContextState(prev, nullptr);
    if (prev) prev->Release();
}
#endif

#include "render_vk.h"

// The layer is active in this session: D3D11 / D3D12 or Vulkan set up.
#ifdef _WIN32
static bool Active() { return g_dev || VkActive(); }
#else
static bool Active() { return VkActive(); }
#endif
static void DrawMask(const std::vector<DrawView>& dviews) {
    if (VkActive()) { DrawMaskVk(dviews); return; }
#ifdef _WIN32
    if (g_dev) DrawMaskD3D(dviews);
#endif
}
static void ReleaseGraphics() {
    ReleaseVk();
    ReleaseD3D();
    g_swapchains.clear();
}

// ================================================================ system pinch + eye gaze actions
static void CreateLayerActions(XrInstance instance, bool eyeSupported) {
    bool eyes = g_eyeExtEnabled && eyeSupported;
    if ((!g_handInteractionEnabled && !eyes) || !nCreateActionSet) return;
    XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy_s(asci.actionSetName, "gmo_alpha_views_xr");
    strcpy_s(asci.localizedActionSetName, "Alpha Views XR");
    if (XR_FAILED(nCreateActionSet(instance, &asci, &g_act.set))) { Log("Action set create failed\n"); g_act.set = XR_NULL_HANDLE; return; }
    nStringToPath(instance, "/user/hand/left", &g_act.hand[0]);
    nStringToPath(instance, "/user/hand/right", &g_act.hand[1]);

    bool anyBound = false;
    if (eyes) {
        XrActionCreateInfo eci{ XR_TYPE_ACTION_CREATE_INFO };
        eci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(eci.actionName, "eye_gaze");
        strcpy_s(eci.localizedActionName, "Eye gaze");
        nCreateAction(g_act.set, &eci, &g_act.eyeGaze);
        XrPath eprof, epath;
        nStringToPath(instance, "/interaction_profiles/ext/eye_gaze_interaction", &eprof);
        nStringToPath(instance, "/user/eyes_ext/input/gaze_ext/pose", &epath);
        XrActionSuggestedBinding eb{ g_act.eyeGaze, epath };
        XrInteractionProfileSuggestedBinding esb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        esb.interactionProfile = eprof;
        esb.countSuggestedBindings = 1;
        esb.suggestedBindings = &eb;
        XrResult er = nSuggestInteractionProfileBindings(instance, &esb);
        Log("Eye gaze bindings: %s (%d)\n", XR_SUCCEEDED(er) ? "ok" : "failed", (int)er);
        if (XR_SUCCEEDED(er)) anyBound = true; else g_act.eyeGaze = XR_NULL_HANDLE;
    } else {
        Log("Eye gaze not available (no eye tracking): direct hand touch only.\n");
    }
    if (!g_handInteractionEnabled) {
        if (!anyBound) { nDestroyActionSet(g_act.set); g_act.set = XR_NULL_HANDLE; }
        return;
    }

    XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
    aci.countSubactionPaths = 2;
    aci.subactionPaths = g_act.hand;
    aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    strcpy_s(aci.actionName, "pinch_value");
    strcpy_s(aci.localizedActionName, "Pinch");
    nCreateAction(g_act.set, &aci, &g_act.pinchValue);
    aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strcpy_s(aci.actionName, "pinch_pose");
    strcpy_s(aci.localizedActionName, "Pinch pose");
    nCreateAction(g_act.set, &aci, &g_act.pinchPose);
    // The pinch bindings are only suggested later, if the game turns out to have no controls of
    // its own (see MaybeSelfAttach). Suggesting the hand-interaction profile next to a game's
    // controller bindings makes the runtime pick it for /user/hand/*, which cuts off the game's
    // controller input.
    Log("System pinch: used only in apps without their own controls.\n");
}

static void SuggestPinchBindings(XrInstance instance) {
    XrPath profile, p[4];
    nStringToPath(instance, "/interaction_profiles/ext/hand_interaction_ext", &profile);
    nStringToPath(instance, "/user/hand/left/input/pinch_ext/value", &p[0]);
    nStringToPath(instance, "/user/hand/right/input/pinch_ext/value", &p[1]);
    nStringToPath(instance, "/user/hand/left/input/pinch_ext/pose", &p[2]);
    nStringToPath(instance, "/user/hand/right/input/pinch_ext/pose", &p[3]);
    XrActionSuggestedBinding b[4] = { { g_act.pinchValue, p[0] }, { g_act.pinchValue, p[1] },
                                      { g_act.pinchPose, p[2] }, { g_act.pinchPose, p[3] } };
    XrInteractionProfileSuggestedBinding sb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    sb.interactionProfile = profile;
    sb.countSuggestedBindings = 4;
    sb.suggestedBindings = b;
    XrResult r = nSuggestInteractionProfileBindings(instance, &sb);
    Log("System pinch bindings: %s (%d)\n", XR_SUCCEEDED(r) ? "ok" : "failed", (int)r);
    if (XR_FAILED(r)) g_act.pinchValue = g_act.pinchPose = XR_NULL_HANDLE;
}

// ---------------------------------------------------------------- controllers
// Per controller profile: input paths (after /user/hand/<side>/input/) for aim pose, trigger,
// grip, B/Y-style button, thumbstick (2D) and thumbstick press. Empty = this controller has no such input.
// Only paths that exist in each profile: one wrong path makes the runtime reject the whole
// suggestion, and that suggestion includes the game's own bindings.
struct CtlProfile { const char* profile; const char* trigger; const char* squeeze; const char* buttonL; const char* buttonR; const char* stick; const char* stickClick; };
static const CtlProfile kCtlProfiles[] = {
    { "/interaction_profiles/oculus/touch_controller",       "trigger/value", "squeeze/value", "y/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/meta/touch_controller_plus",    "trigger/value", "squeeze/value", "y/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/meta/touch_pro_controller",     "trigger/value", "squeeze/value", "y/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/bytedance/pico4_controller",    "trigger/value", "squeeze/value", "y/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/bytedance/pico4s_controller",   "trigger/value", "squeeze/value", "y/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/bytedance/pico_neo3_controller","trigger/value", "squeeze/value", "y/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/valve/index_controller",        "trigger/value", "squeeze/value", "b/click", "b/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/htc/vive_controller",           "trigger/value", "squeeze/click", "menu/click", "menu/click", "trackpad", "trackpad/click" },
    { "/interaction_profiles/microsoft/motion_controller",   "trigger/value", "squeeze/click", "menu/click", "menu/click", "thumbstick", "thumbstick/click" },
    { "/interaction_profiles/khr/simple_controller",         "select/click",  "",              "menu/click", "menu/click", "", "" },
};

static void CreateCtlActions(XrInstance instance) {
    if (!nCreateActionSet || !nCreateAction || !nStringToPath) return;
    XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy_s(asci.actionSetName, "gmo_alpha_views_xr_controllers");
    strcpy_s(asci.localizedActionSetName, "Alpha Views XR controllers");
    if (XR_FAILED(nCreateActionSet(instance, &asci, &g_ctl.set))) { g_ctl.set = XR_NULL_HANDLE; Log("Controller action set failed\n"); return; }
    nStringToPath(instance, "/user/hand/left", &g_ctl.hand[0]);
    nStringToPath(instance, "/user/hand/right", &g_ctl.hand[1]);
    auto make = [&](XrAction& a, XrActionType type, const char* name, const char* label) {
        XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
        ci.actionType = type;
        ci.countSubactionPaths = 2;
        ci.subactionPaths = g_ctl.hand;
        strcpy_s(ci.actionName, name);
        strcpy_s(ci.localizedActionName, label);
        if (XR_FAILED(nCreateAction(g_ctl.set, &ci, &a))) a = XR_NULL_HANDLE;
    };
    make(g_ctl.pose, XR_ACTION_TYPE_POSE_INPUT, "aim", "Alpha Views aim");
    make(g_ctl.trigger, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Alpha Views grab");
    make(g_ctl.squeeze, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Alpha Views grip");
    make(g_ctl.button, XR_ACTION_TYPE_BOOLEAN_INPUT, "delete", "Alpha Views delete");
    make(g_ctl.stick, XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Alpha Views thumbstick");   // whole stick: some runtimes (VDXR) ignore thumbstick/y
    make(g_ctl.stickClick, XR_ACTION_TYPE_BOOLEAN_INPUT, "colour", "Alpha Views cutout colour");
    if (!g_ctl.pose || !g_ctl.trigger) {
        Log("Controller actions failed; controllers won't work in edit mode.\n");
        nDestroyActionSet(g_ctl.set); g_ctl = CtlActions();
    }
}

// Our bindings for one profile, added to `out`.
static void CtlBindings(XrInstance instance, const CtlProfile& p, std::vector<XrActionSuggestedBinding>& out) {
    const char* side[2] = { "left", "right" };
    for (int s = 0; s < 2; ++s) {
        auto add = [&](XrAction a, const char* input) {
            if (!a || !input || !*input) return;
            char buf[XR_MAX_PATH_LENGTH];
            snprintf(buf, sizeof buf, "/user/hand/%s/input/%s", side[s], input);
            XrPath path;
            if (XR_SUCCEEDED(nStringToPath(instance, buf, &path))) out.push_back({ a, path });
        };
        add(g_ctl.pose, "aim/pose");
        add(g_ctl.trigger, p.trigger);
        add(g_ctl.squeeze, p.squeeze);
        add(g_ctl.button, s ? p.buttonR : p.buttonL);
        add(g_ctl.stick, p.stick);
        add(g_ctl.stickClick, p.stickClick);
    }
}
static const CtlProfile* FindCtlProfile(XrInstance instance, XrPath profile) {
    if (!nPathToString) return nullptr;
    char buf[XR_MAX_PATH_LENGTH]; uint32_t n = 0;
    if (XR_FAILED(nPathToString(instance, profile, sizeof buf, &n, buf))) return nullptr;
    for (auto& p : kCtlProfiles) if (strcmp(p.profile, buf) == 0) return &p;
    return nullptr;
}

// Profiles the game suggested bindings for itself: our own suggestions must never replace them.
static std::vector<XrPath> g_gameProfiles;
// Frames since the game last called xrSyncActions. Some games attach input but never sync it
// (AC EVO: eye gaze only); then our actions never update unless we sync them ourselves.
static int g_framesSinceGameSync = 0;

static XrResult XRAPI_CALL Hook_xrSuggestInteractionProfileBindings(XrInstance instance,
    const XrInteractionProfileSuggestedBinding* sb) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (sb) g_gameProfiles.push_back(sb->interactionProfile);
    const CtlProfile* p = (g_ctl.set && sb) ? FindCtlProfile(instance, sb->interactionProfile) : nullptr;
    if (!p) return nSuggestInteractionProfileBindings(instance, sb);
    std::vector<XrActionSuggestedBinding> all(sb->suggestedBindings, sb->suggestedBindings + sb->countSuggestedBindings);
    CtlBindings(instance, *p, all);
    XrInteractionProfileSuggestedBinding mine = *sb;
    mine.countSuggestedBindings = (uint32_t)all.size();
    mine.suggestedBindings = all.data();
    XrResult r = nSuggestInteractionProfileBindings(instance, &mine);
    if (XR_SUCCEEDED(r)) {
        g_ctl.merged = true;
        Log("Controller edit input added to the game's %s bindings.\n", p->profile);
        return r;
    }
    // Never break the game's own bindings: resend them untouched.
    Log("Adding controller edit input to %s failed (%d); game bindings sent unchanged.\n", p->profile, (int)r);
    return nSuggestInteractionProfileBindings(instance, sb);
}

// Apps with no input of their own: suggest our controller bindings for common controllers.
static void SuggestOwnCtlBindings(XrInstance instance) {
    if (!g_ctl.set) return;
    for (auto& p : kCtlProfiles) {
        std::vector<XrActionSuggestedBinding> b;
        CtlBindings(instance, p, b);
        XrPath prof;
        if (b.empty() || XR_FAILED(nStringToPath(instance, p.profile, &prof))) continue;
        if (std::find(g_gameProfiles.begin(), g_gameProfiles.end(), prof) != g_gameProfiles.end()) continue;
        XrInteractionProfileSuggestedBinding sb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        sb.interactionProfile = prof;
        sb.countSuggestedBindings = (uint32_t)b.size();
        sb.suggestedBindings = b.data();
        if (XR_SUCCEEDED(nSuggestInteractionProfileBindings(instance, &sb))) g_ctl.merged = true;
    }
    Log("Controller edit input: own bindings %s.\n", g_ctl.merged ? "ok" : "all refused");
}

static void CreateCtlSpaces(XrSession session) {
    for (int h = 0; h < 2; ++h) {
        if (g_ctl.space[h] && nDestroySpace) nDestroySpace(g_ctl.space[h]);
        g_ctl.space[h] = XR_NULL_HANDLE;
        XrActionSpaceCreateInfo ci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        ci.action = g_ctl.pose;
        ci.subactionPath = g_ctl.hand[h];
        ci.poseInActionSpace.orientation.w = 1;
        if (XR_FAILED(nCreateActionSpace(session, &ci, &g_ctl.space[h]))) g_ctl.space[h] = XR_NULL_HANDLE;
    }
}

static void CreateActionSpaces(XrSession session) {
    if (g_act.eyeGaze) {
        XrActionSpaceCreateInfo ci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        ci.action = g_act.eyeGaze;
        ci.poseInActionSpace.orientation.w = 1;
        if (XR_FAILED(nCreateActionSpace(session, &ci, &g_act.eyeSpace))) g_act.eyeSpace = XR_NULL_HANDLE;
    }
    if (!g_act.pinchPose) return;
    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo ci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        ci.action = g_act.pinchPose;
        ci.subactionPath = g_act.hand[h];
        ci.poseInActionSpace.orientation.w = 1;
        if (XR_FAILED(nCreateActionSpace(session, &ci, &g_act.space[h]))) g_act.space[h] = XR_NULL_HANDLE;
    }
}

static XrResult XRAPI_CALL Hook_xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo* info) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (session != g_session || g_act.attached || g_ctl.attached) return nAttachSessionActionSets(session, info);
    // Games with their own controls only get our eye gaze (its own /user/eyes_ext path, so it
    // can't take over the hands) and our controller actions, which were merged into the game's
    // own controller bindings. Never the hand-interaction pinch.
    g_act.pinchValue = g_act.pinchPose = XR_NULL_HANDLE;
    // The game attaches before suggesting any controller bindings we could join (AC EVO sometimes
    // attaches with only eye gaze): suggest ours for the controller profiles it hasn't claimed.
    // Attaching happens once per session, so this is the last chance to get controller input.
    if (g_ctl.set && !g_ctl.merged) {
        Log("Game attached its input before any controller bindings; adding our own.\n");
        SuggestOwnCtlBindings(g_instance);
    }
    bool addEyes = g_act.set && g_act.eyeGaze;
    bool addCtl = g_ctl.set && g_ctl.merged;
    if (!addEyes && !addCtl) {
        Log("Game has its own controls: left untouched, using finger-distance pinch.\n");
        return nAttachSessionActionSets(session, info);
    }
    std::vector<XrActionSet> sets(info->actionSets, info->actionSets + info->countActionSets);
    if (addEyes) sets.push_back(g_act.set);
    if (addCtl) sets.push_back(g_ctl.set);
    XrSessionActionSetsAttachInfo ai = *info;
    ai.countActionSets = (uint32_t)sets.size();
    ai.actionSets = sets.data();
    XrResult r = nAttachSessionActionSets(session, &ai);
    if (XR_SUCCEEDED(r)) {
        if (addEyes) { g_act.attached = true; CreateActionSpaces(session); }
        if (addCtl) { g_ctl.attached = true; CreateCtlSpaces(session); }
        Log("Added alongside the game's controls:%s%s; using finger-distance pinch.\n",
            addEyes ? " eye gaze" : "", addCtl ? " controller edit input" : "");
        return r;
    }
    Log("Could not add our actions to the game's (%d); game input left untouched.\n", (int)r);
    return nAttachSessionActionSets(session, info);
}

static XrResult XRAPI_CALL Hook_xrSyncActions(XrSession session, const XrActionsSyncInfo* info) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_framesSinceGameSync = 0;
    if (session != g_session || (!g_act.attached && !g_ctl.attached) || g_act.selfAttached) return nSyncActions(session, info);
    std::vector<XrActiveActionSet> sets(info->activeActionSets, info->activeActionSets + info->countActiveActionSets);
    if (g_act.attached) sets.push_back({ g_act.set, XR_NULL_PATH });
    if (g_ctl.attached) sets.push_back({ g_ctl.set, XR_NULL_PATH });
    XrActionsSyncInfo si = *info;
    si.countActiveActionSets = (uint32_t)sets.size();
    si.activeActionSets = sets.data();
    return nSyncActions(session, &si);
}

static bool g_appUsesActions = false;
static XrResult XRAPI_CALL Hook_xrCreateActionSet(XrInstance instance, const XrActionSetCreateInfo* info, XrActionSet* set) {
    g_appUsesActions = true; // the game has input of its own; never attach on our own then
    return nCreateActionSet(instance, info, set);
}

// Game never created any input actions after a few seconds: attach ours alone and sync each
// frame. Games that do have input get our set added to their own attach call instead, so we
// can never block a game's own attach.
static void MaybeSelfAttach() {
    if ((!g_act.set && !g_ctl.set) || g_act.attached || g_ctl.attached || g_appUsesActions || ++g_act.frames < 300) return;
    if (g_act.set && g_act.pinchValue) SuggestPinchBindings(g_instance);
    SuggestOwnCtlBindings(g_instance);
    std::vector<XrActionSet> sets;
    if (g_act.set) sets.push_back(g_act.set);
    if (g_ctl.set && g_ctl.merged) sets.push_back(g_ctl.set);
    if (sets.empty()) { g_act.frames = INT_MIN; return; }
    XrSessionActionSetsAttachInfo ai{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    ai.countActionSets = (uint32_t)sets.size();
    ai.actionSets = sets.data();
    XrResult r = nAttachSessionActionSets(g_session, &ai);
    if (XR_SUCCEEDED(r)) {
        g_act.selfAttached = true;
        if (g_act.set) { g_act.attached = true; CreateActionSpaces(g_session); }
        if (g_ctl.set && g_ctl.merged) { g_ctl.attached = true; CreateCtlSpaces(g_session); }
        Log("Game has no actions; system pinch / controller input attached on its own.\n");
    } else {
        Log("Self attach failed (%d); using joint pinch.\n", (int)r);
        g_act.frames = INT_MIN; // don't retry
    }
}
static void SelfSync() {
    if (!g_act.selfAttached) {
        if ((!g_act.attached && !g_ctl.attached) || ++g_framesSinceGameSync < 30) return;
        static bool logged = false;
        if (!logged) { Log("Game doesn't sync its input: syncing our controller input ourselves.\n"); logged = true; }
    }
    std::vector<XrActiveActionSet> as;
    if (g_act.attached) as.push_back({ g_act.set, XR_NULL_PATH });
    if (g_ctl.attached) as.push_back({ g_ctl.set, XR_NULL_PATH });
    if (as.empty()) return;
    XrActionsSyncInfo si{ XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = (uint32_t)as.size();
    si.activeActionSets = as.data();
    nSyncActions(g_session, &si);
}

// ================================================================ hooks
// No hand joints: gestures can't work. Say what to do, and what still works.
static void HandTrackingHint() {
    if (g_runtimeName.find("VirtualDesktop") != std::string::npos || g_viaVD)
        Log("Virtual Desktop: hand gestures need hand tracking forwarded to the PC.\n"
            "  Quest headsets: turn on forwarding of hand tracking data in Virtual Desktop's settings\n"
            "  (VD 1.30 or newer), then restart the game. Until then your hands act as pretend\n"
            "  controllers: pinch = trigger, fist = grip.\n"
            "  PICO headsets: Virtual Desktop doesn't forward PICO hand tracking, so use controllers.\n"
            "  (Virtual Desktop may report a PICO headset as a Quest.)\n");
    Log("Without hand tracking, use controllers in edit mode (laser pointer when there's no eye tracking).\n");
}

static XrResult XRAPI_CALL Hook_xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    XrResult r = nCreateSession(instance, info, session);
    if (XR_FAILED(r)) return r;
    std::lock_guard<std::mutex> lk(g_mutex);

    const XrGraphicsBindingVulkanKHR* vk = nullptr;   // also XR_KHR_vulkan_enable2 (same structure type)
    for (auto* p = (const XrBaseInStructure*)info->next; p; p = p->next)
        if (p->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) vk = (const XrGraphicsBindingVulkanKHR*)p;
#ifdef _WIN32
    const XrGraphicsBindingD3D11KHR* d3d = nullptr;
    const XrGraphicsBindingD3D12KHR* d3d12 = nullptr;
    for (auto* p = (const XrBaseInStructure*)info->next; p; p = p->next) {
        if (p->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) d3d = (const XrGraphicsBindingD3D11KHR*)p;
        if (p->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) d3d12 = (const XrGraphicsBindingD3D12KHR*)p;
    }
    if (!d3d && !vk && d3d12 && !g_d3d12Enabled) {
        Log("D3D12 game: layer inactive. To try experimental D3D12 support press Ctrl+Alt+D, then restart the game.\n");
        return r;
    }
    if (!d3d && !d3d12 && !vk) { Log("Session is not D3D11, D3D12 or Vulkan (OpenGL?), layer inactive.\n"); return r; }
#else
    if (!vk) { Log("Session is not Vulkan (OpenGL?), layer inactive.\n"); return r; }
#endif

    uint32_t n = 0;
    nEnumerateEnvironmentBlendModes(instance, info->systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr);
    std::vector<XrEnvironmentBlendMode> modes(n);
    nEnumerateEnvironmentBlendModes(instance, info->systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, modes.data());
    g_alphaSupported = false;
    for (auto m : modes) if (m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) g_alphaSupported = true;
    Log("Runtime offers ALPHA_BLEND: %s\n", g_alphaSupported ? "yes" : "no");
    if (UseKeyColour())
        Log("Cutouts painted %ls (key colour for %s): set your streamer to key out %ls. Change it with " KEY_COLOUR_HOW ".\n",
            kKeyName[g_keyMode], g_runtimeName.c_str(), kKeyName[g_keyMode]);
    else if (!g_alphaSupported && g_forceAlpha)
        Log("Force alpha ON: cutouts are transparent and layer alpha flags are set; the game's own blend mode is kept (Ctrl+Alt+K turns this off).\n");
    else if (!g_alphaSupported)
        // Stay active (hotkeys and edit mode work) but draw no cutouts until a key colour is chosen.
        Log("%s has no alpha passthrough, so cutouts are hidden. Edit mode still works. Use " KEY_COLOUR_HOW " for a key colour "
            "(green / blue / pink) if your streamer can key it out (e.g. Virtual Desktop).\n",
            g_runtimeName.c_str());

    ReleaseGraphics();
    if (vk) {
        if (!InitVk(vk)) { Log("Vulkan init failed, layer inactive.\n"); ReleaseGraphics(); return r; }
        Log("Vulkan session hooked.\n");
    }
#ifdef _WIN32
    else if (d3d) {
        if (!InitD3D(d3d->device)) { Log("D3D11 init failed, layer inactive.\n"); ReleaseGraphics(); return r; }
        Log("D3D11 session hooked.\n");
    } else {
        ID3D11Device* dev11 = nullptr;
        IUnknown* queues[] = { d3d12->queue };
        HRESULT hr = D3D11On12CreateDevice(d3d12->device, 0, nullptr, 0, queues, 1, 0, &dev11, nullptr, nullptr);
        if (FAILED(hr) || !dev11) { Log("D3D11On12 device failed 0x%08X, layer inactive.\n", (unsigned)hr); return r; }
        bool ok = SUCCEEDED(dev11->QueryInterface(__uuidof(ID3D11On12Device), (void**)&g_on12)) && InitD3D(dev11);
        dev11->Release();
        if (!ok) { Log("D3D12 init failed, layer inactive.\n"); ReleaseGraphics(); return r; }
        Log("D3D12 session hooked (EXPERIMENTAL, via D3D11On12). Ctrl+Alt+D turns it off.\n");
    }
#endif
    g_session = *session;

    ReleaseHands();
    if (g_handExtEnabled && nCreateHandTrackerEXT) {
        XrSystemHandTrackingPropertiesEXT htp{ XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
        XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
        sp.next = &htp;
        nGetSystemProperties(instance, info->systemId, &sp);
        Log("Headset system: %s\n", sp.systemName);
        if (htp.supportsHandTracking) {
            for (int h = 0; h < 2; ++h) {
                XrHandTrackerCreateInfoEXT hci{ XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
                hci.hand = h ? XR_HAND_RIGHT_EXT : XR_HAND_LEFT_EXT;
                hci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
                // Ask for hand-tracked and controller-made joints, and get told which each frame.
                XrHandTrackingDataSourceEXT src[2] = { XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT, XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT };
                XrHandTrackingDataSourceInfoEXT dsi{ XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT };
                dsi.requestedDataSourceCount = 2;
                dsi.requestedDataSources = src;
                if (g_handSourceEnabled) hci.next = &dsi;
                XrResult hr = nCreateHandTrackerEXT(*session, &hci, &g_hand[h].tracker);
                if (XR_FAILED(hr)) { g_hand[h].tracker = XR_NULL_HANDLE; Log("%s hand tracker refused: %d\n", h ? "Right" : "Left", (int)hr); }
            }
            if (g_hand[0].tracker || g_hand[1].tracker)
                Log("Hand trackers created%s. Put controllers down to use hand tracking.\n",
                    g_handSourceEnabled ? " (runtime says where the joints come from)" : "");
            else HandTrackingHint();
        } else {
            Log("System says hand tracking not supported.\n");
            HandTrackingHint();
        }
    } else {
        Log("Hand tracking extension not available.\n");
        HandTrackingHint();
    }
    bool eyeSupported = false;
    if (g_eyeExtEnabled) {
        XrSystemEyeGazeInteractionPropertiesEXT ep{ XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
        XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
        sp.next = &ep;
        if (XR_SUCCEEDED(nGetSystemProperties(instance, info->systemId, &sp))) eyeSupported = ep.supportsEyeGazeInteraction;
        Log("Headset eye tracking: %s\n", eyeSupported ? "yes" : "no");
    }
    ReleaseActions();
    CreateLayerActions(instance, eyeSupported);
    return r;
}

// The runtime may read an image as soon as the game releases it, so cutouts are drawn just
// before release, using the game's latest xrLocateViews and last frame's swapchain->view mapping.
struct ViewSlot { uint32_t viewIndex; XrRect2Di rect; uint32_t slice; ViewDepth depth; };
static std::map<XrSwapchain, std::vector<ViewSlot>> g_scSlots;
static XrSpace g_locSpace = XR_NULL_HANDLE;
static XrTime g_locTime = 0;
static std::vector<XrView> g_locViews;
static XrTime g_updatedFor = -1;
static bool g_drewThisFrame = false;
// Last submitted frame's projection views (used by the D3D12 path, see the release hook).
static XrTime g_endTime = 0, g_endPeriod = 0;

static XMVECTOR HeadFromViews(const XrView* v, uint32_t n) {
    XMVECTOR p = XMVectorZero();
    for (uint32_t i = 0; i < n; ++i) p += V(v[i].pose.position);
    return n ? p / float(n) : p;
}

static XrResult XRAPI_CALL Hook_xrDestroySession(XrSession session) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (session == g_session) {
            ReleaseActions(); ReleaseHands(); ReleaseGraphics(); g_session = XR_NULL_HANDLE;
            g_scSlots.clear(); g_locViews.clear(); g_endViews.clear(); g_endTime = g_endPeriod = 0;
            ResetDepthStatus();
        }
    }
    return nDestroySession(session);
}

static XrResult XRAPI_CALL Hook_xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* sc) {
    bool color = (info->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) != 0;
    bool depth = !color && (info->usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    if (VkActive() && session == g_session && (color || depth)) {
        // Vulkan: depth images get the SAMPLED usage bit too, so psBox can read them (without it if refused).
        XrResult r = XR_ERROR_RUNTIME_FAILURE;
        bool sampled = depth && (info->usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) != 0, made = false;
        if (depth && !sampled) {
            XrSwapchainCreateInfo ci = *info;
            ci.usageFlags |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            r = nCreateSwapchain(session, &ci, sc);
            made = sampled = XR_SUCCEEDED(r);
            if (!made) Log("Depth swapchain with the sampled bit refused (%d): trying without (boxes act as solid blocks).\n", (int)r);
        }
        if (!made) r = nCreateSwapchain(session, info, sc);
        if (XR_FAILED(r)) return r;
        std::lock_guard<std::mutex> lk(g_mutex);
        SwapchainInfo si;
        si.format = info->format;
        si.depth = depth;
        si.vk.width = info->width; si.vk.height = info->height;
        si.vk.layers = std::max(1u, info->arraySize); si.vk.samples = std::max(1u, info->sampleCount);
        si.vk.sampled = sampled;
        uint32_t n = 0;
        nEnumerateSwapchainImages(*sc, 0, &n, nullptr);
        std::vector<XrSwapchainImageVulkanKHR> imgs(n, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
        nEnumerateSwapchainImages(*sc, n, &n, (XrSwapchainImageBaseHeader*)imgs.data());
        for (auto& i : imgs) si.vk.images.push_back(i.image);
        if (depth) {
            g_gameMadeDepth = true;
            VkFormatProperties fp{};
            g_vk.f.vkGetPhysicalDeviceFormatProperties(g_vk.phys, (VkFormat)info->format, &fp);
            if (sampled && !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                Log("Depth format %d can't be sampled on this GPU: boxes act as solid blocks.\n", (int)info->format);
                si.vk.sampled = false;
            }
        }
        Log("%s swapchain %dx%d format %d, %u images, %u samples, %u layers (Vulkan)\n", depth ? "Depth" : "Colour",
            info->width, info->height, (int)info->format, n, info->sampleCount, info->arraySize);
        g_swapchains[*sc] = std::move(si);
        return r;
    }
    XrResult r = nCreateSwapchain(session, info, sc);
#ifndef _WIN32
    return r;
#else
    if (XR_FAILED(r) || session != g_session || !g_dev) return r;
    if (!color && !depth) return r;
    std::lock_guard<std::mutex> lk(g_mutex);
    SwapchainInfo si;
    si.format = info->format;
    si.depth = depth;
    uint32_t n = 0;
    nEnumerateSwapchainImages(*sc, 0, &n, nullptr);
    if (depth) {
        g_gameMadeDepth = true;
        // Game depth: only read (copied for box windows).
        if (g_on12) {
            // OpenXR hands D3D12 depth images over in DEPTH_WRITE state and expects them back in it.
            // Wrapped as depth-stencil: we only ever copy from them.
            std::vector<XrSwapchainImageD3D12KHR> imgs(n, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
            nEnumerateSwapchainImages(*sc, n, &n, (XrSwapchainImageBaseHeader*)imgs.data());
            D3D11_RESOURCE_FLAGS flags{ D3D11_BIND_DEPTH_STENCIL };
            int failed = 0;
            for (auto& i : imgs) {
                ID3D11Texture2D* t = nullptr;
                HRESULT hr = g_on12->CreateWrappedResource(i.texture, &flags, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE, __uuidof(ID3D11Texture2D), (void**)&t);
                if (FAILED(hr)) { if (!failed++) Log("Wrapping D3D12 depth image failed 0x%08X: boxes act as solid blocks.\n", (unsigned)hr); t = nullptr; }
                si.images.push_back(t);
            }
        } else {
            std::vector<XrSwapchainImageD3D11KHR> imgs(n, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
            nEnumerateSwapchainImages(*sc, n, &n, (XrSwapchainImageBaseHeader*)imgs.data());
            for (auto& i : imgs) si.images.push_back(i.texture);
        }
        g_swapchains[*sc] = std::move(si);
        Log("Depth swapchain %dx%d format %d, %u images, %u samples%s\n", info->width, info->height, (int)info->format, n,
            info->sampleCount, g_on12 ? " (D3D12, read via D3D11On12)" : "");
        return r;
    }
    if (g_on12) {
        // OpenXR hands D3D12 swapchain images over in RENDER_TARGET state and expects them back in it.
        std::vector<XrSwapchainImageD3D12KHR> imgs(n, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
        nEnumerateSwapchainImages(*sc, n, &n, (XrSwapchainImageBaseHeader*)imgs.data());
        D3D11_RESOURCE_FLAGS flags{ D3D11_BIND_RENDER_TARGET };
        for (auto& i : imgs) {
            ID3D11Texture2D* t = nullptr;
            HRESULT hr = g_on12->CreateWrappedResource(i.texture, &flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_RENDER_TARGET, __uuidof(ID3D11Texture2D), (void**)&t);
            if (FAILED(hr)) Log("Wrapping D3D12 image failed 0x%08X\n", (unsigned)hr);
            si.images.push_back(t);
        }
    } else {
        std::vector<XrSwapchainImageD3D11KHR> imgs(n, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
        nEnumerateSwapchainImages(*sc, n, &n, (XrSwapchainImageBaseHeader*)imgs.data());
        for (auto& i : imgs) si.images.push_back(i.texture);
    }
    g_swapchains[*sc] = std::move(si);
    Log("Swapchain %dx%d format %d, %u images\n", info->width, info->height, (int)info->format, n);
    return r;
#endif
}

static XrResult XRAPI_CALL Hook_xrDestroySwapchain(XrSwapchain sc) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_swapchains.find(sc);
        if (it != g_swapchains.end()) {
            if (VkActive()) { VkWaitAll(); VkReleaseSwapchain(it->second); }
#ifdef _WIN32
            ReleaseSwapchainInfo(it->second);
            if (g_on12 && g_ctx) g_ctx->Flush(); // let D3D11On12 drop its references now
#endif
            g_swapchains.erase(it);
        }
        g_scSlots.erase(sc);
    }
    return nDestroySwapchain(sc);
}

static XrResult XRAPI_CALL Hook_xrAcquireSwapchainImage(XrSwapchain sc, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    XrResult r = nAcquireSwapchainImage(sc, info, index);
    if (XR_SUCCEEDED(r)) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_swapchains.find(sc);
        if (it != g_swapchains.end()) it->second.lastAcquired = *index;
    }
    return r;
}

static XrResult XRAPI_CALL Hook_xrLocateViews(XrSession session, const XrViewLocateInfo* li, XrViewState* vs,
    uint32_t cap, uint32_t* count, XrView* views) {
    XrResult r = nLocateViews(session, li, vs, cap, count, views);
    if (XR_SUCCEEDED(r) && session == g_session && views && cap > 0 &&
        li->viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_locSpace = li->space;
        g_locTime = li->displayTime;
        g_locViews.assign(views, views + *count);
    }
    return r;
}

// Dev only: with ALPHA_VIEWS_DUMP set, save the alpha (passthrough mask) of one eye image as
// mask_dump.ppm (colour image, passthrough tinted green) after ~200 frames, to check cutouts
// without looking through the headset.
#ifdef _WIN32
static void MaybeDumpMask(XrSwapchain sc) {
    static int frames = 0;
    static bool on = !EnvVar("ALPHA_VIEWS_DUMP").empty();
    static bool editOnly = [] { char v[16] = {}; GetEnvironmentVariableA("ALPHA_VIEWS_DUMP", v, sizeof v); return strcmp(v, "edit") == 0; }();
    if (!on || !g_dev || g_on12 || (editOnly && !g_edit) || ++frames != 200) return;   // "edit": count only in edit mode
    auto it = g_swapchains.find(sc);
    if (it == g_swapchains.end() || it->second.lastAcquired >= it->second.images.size()) return;
    ID3D11Texture2D* src = it->second.images[it->second.lastAcquired];
    D3D11_TEXTURE2D_DESC td; src->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    sd.MipLevels = 1; sd.ArraySize = 1;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&sd, nullptr, &st))) { Log("Mask dump: staging failed\n"); return; }
    g_ctx->CopySubresourceRegion(st, 0, 0, 0, 0, src, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        const int step = 4;   // quarter size
        // Colour image (R/B order ignored), passthrough pixels tinted green.
        FILE* f = fopen((g_dir + PATH_SEP "mask_dump.ppm").c_str(), "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", td.Width / step, td.Height / step);
            for (UINT y = 0; y + step <= td.Height; y += step)
                for (UINT x = 0; x + step <= td.Width; x += step) {
                    const uint8_t* p = (const uint8_t*)m.pData + y * m.RowPitch + x * 4;
                    bool pass = p[3] < 128;
                    fputc(pass ? p[0] / 3 : p[0], f); fputc(pass ? 255 : p[1], f); fputc(pass ? p[2] / 3 : p[2], f);
                }
            fclose(f);
            Log("Mask dump written (format %d).\n", (int)td.Format);
        }
        g_ctx->Unmap(st, 0);
    }
    st->Release();
}
#else
static void MaybeDumpMask(XrSwapchain) {}
#endif

static XrResult XRAPI_CALL Hook_xrReleaseSwapchainImage(XrSwapchain sc, const XrSwapchainImageReleaseInfo* info) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_scSlots.find(sc);
        // Views: the game's latest xrLocateViews. D3D12 only: if the game hasn't located views
        // since the last submitted frame, use that frame's views instead (one frame old), because
        // drawing after release is likely too late there.
        bool fresh = !g_locViews.empty() && (!g_on12 || g_locTime > g_endTime);
        const std::vector<XrView>* viewsP = fresh ? &g_locViews : &g_endViews;
        XrSpace vspace = fresh ? g_locSpace : g_endSpace;
        XrTime vtime = fresh ? g_locTime : g_endTime + g_endPeriod;
        // Windows live in the space the game submits its frames in (its world). Some engines (Unreal Engine 4)
        // locate views in another space, e.g. relative to the head: windows kept there would follow the head.
        // Convert those views into the submitted space.
        static std::vector<XrView> converted;
        if (fresh && g_endSpace != XR_NULL_HANDLE && g_locSpace != g_endSpace) {
            XrSpaceLocation base{ XR_TYPE_SPACE_LOCATION };
            const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (XR_SUCCEEDED(nLocateSpace(g_locSpace, g_endSpace, g_locTime, &base)) && (base.locationFlags & need) == need) {
                XMVECTOR bq = Q(base.pose.orientation), bp = V(base.pose.position);
                converted = g_locViews;
                for (auto& v : converted) {
                    XMVECTOR p = bp + XMVector3Rotate(V(v.pose.position), bq);
                    XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(Q(v.pose.orientation), bq));   // base * view
                    v.pose.position = { XMVectorGetX(p), XMVectorGetY(p), XMVectorGetZ(p) };
                    v.pose.orientation = { XMVectorGetX(q), XMVectorGetY(q), XMVectorGetZ(q), XMVectorGetW(q) };
                }
                viewsP = &converted;
                vspace = g_endSpace;
                static bool loggedConvert = false;
                if (!loggedConvert) {
                    Log("Game locates views in a different space from its frames (offset %.2f, %.2f, %.2f m): using the frame's space, so windows stay put in the world.\n",
                        base.pose.position.x, base.pose.position.y, base.pose.position.z);
                    loggedConvert = true;
                }
            }
        }
        const std::vector<XrView>& views = *viewsP;
        if (Active() && it != g_scSlots.end() && !views.empty()) {
            static bool loggedStale = false;
            if (!fresh && !loggedStale) { Log("D3D12: game doesn't locate views before release; using last frame's views.\n"); loggedStale = true; }
            if (g_updatedFor != vtime) {
                SelfSync();
                UpdateFrame(vspace, vtime, HeadFromViews(views.data(), (uint32_t)views.size()));
                g_updatedFor = vtime;
            }
            if (HasAnythingToDraw()) {
                static std::vector<DrawView> dv; dv.clear();
                for (auto& s : it->second)
                    if (s.viewIndex < views.size())
                        dv.push_back({ sc, s.rect, s.slice, views[s.viewIndex].pose, views[s.viewIndex].fov, s.depth });
                if (!dv.empty()) { DrawMask(dv); g_drewThisFrame = true; MaybeDumpMask(sc); }
            }
        }
    }
    return nReleaseSwapchainImage(sc, info);
}

static XrResult XRAPI_CALL Hook_xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
    std::lock_guard<std::mutex> lk(g_mutex);
    StartHotkeys();
    HandleD3D12Key();
    HandleForceAlphaKey();
    if (session != g_session || !Active()) {
        g_hotkeyHits.fetch_and((1u << HK_D) | (1u << HK_K));   // idle: don't keep other presses for later
        return nEndFrame(session, info);
    }
    bool drewAtRelease = g_drewThisFrame;
    g_drewThisFrame = false;
    HandleKeys();
    MaybeSelfAttach();

    int projIdx = -1;
    for (uint32_t i = 0; i < info->layerCount; ++i)
        if (info->layers[i] && info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) { projIdx = (int)i; break; }
    if (projIdx < 0) return nEndFrame(session, info);

    XrCompositionLayerProjection proj = *(const XrCompositionLayerProjection*)info->layers[projIdx];
    g_scSlots.clear();
    std::vector<ViewDepth> vdepth(proj.viewCount);
    for (uint32_t i = 0; i < proj.viewCount; ++i) {
        for (auto* p = (const XrBaseInStructure*)proj.views[i].next; p; p = p->next) {
            if (p->type != XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR) continue;
            auto* di = (const XrCompositionLayerDepthInfoKHR*)p;
            ViewDepth& vd = vdepth[i];
            vd.valid = di->subImage.swapchain != XR_NULL_HANDLE;
            vd.swapchain = di->subImage.swapchain;
            vd.rect = di->subImage.imageRect;
            vd.slice = di->subImage.imageArrayIndex;
            vd.minDepth = di->minDepth; vd.maxDepth = di->maxDepth;
            vd.nearZ = di->nearZ; vd.farZ = di->farZ;
            break;
        }
        g_scSlots[proj.views[i].subImage.swapchain].push_back(
            { i, proj.views[i].subImage.imageRect, proj.views[i].subImage.imageArrayIndex, vdepth[i] });
    }
    // Tell the log whether this game sends depth (once per session, and again if it changes).
    {
        bool sends = !vdepth.empty() && vdepth[0].valid;
        // No depth on the layer we draw on: check the game's other projection layers too.
        int depthLayer = -1;
        for (uint32_t l = projIdx + 1; !sends && depthLayer < 0 && l < info->layerCount; ++l) {
            auto* lb = info->layers[l];
            if (!lb || lb->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
            auto* pl = (const XrCompositionLayerProjection*)lb;
            for (uint32_t i = 0; depthLayer < 0 && i < pl->viewCount; ++i)
                for (auto* p = (const XrBaseInStructure*)pl->views[i].next; p; p = p->next)
                    if (p->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR &&
                        ((const XrCompositionLayerDepthInfoKHR*)p)->subImage.swapchain != XR_NULL_HANDLE) { depthLayer = (int)l; break; }
        }
        int now = sends ? 1 : depthLayer >= 0 ? 3 : g_gameMadeDepth ? 2 : 0;
        // Depth made but not found: describe what the game submits (a few times), to see where it went.
        static int describeLogs = 0;
        if (now == 2 && g_noDepthFrames == 90 && describeLogs < 3) {
            ++describeLogs;
            std::string d;
            char b[160];
            for (uint32_t l = 0; l < info->layerCount; ++l) {
                auto* lb = info->layers[l];
                if (!lb) { d += " [null]"; continue; }
                snprintf(b, sizeof b, " [%u: type %d", l, (int)lb->type); d += b;
                if (lb->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                    auto* pl = (const XrCompositionLayerProjection*)lb;
                    snprintf(b, sizeof b, ", flags 0x%llx, %u views", (unsigned long long)pl->layerFlags, pl->viewCount); d += b;
                    for (auto* p = (const XrBaseInStructure*)pl->next; p; p = p->next) { snprintf(b, sizeof b, ", layer next %d", (int)p->type); d += b; }
                    for (uint32_t i = 0; i < pl->viewCount; ++i) {
                        snprintf(b, sizeof b, "; view %u rect %d,%d %dx%d slice %u next:", i,
                            pl->views[i].subImage.imageRect.offset.x, pl->views[i].subImage.imageRect.offset.y,
                            pl->views[i].subImage.imageRect.extent.width, pl->views[i].subImage.imageRect.extent.height,
                            pl->views[i].subImage.imageArrayIndex); d += b;
                        int n = 0;
                        for (auto* p = (const XrBaseInStructure*)pl->views[i].next; p && n < 8; p = p->next, ++n) { snprintf(b, sizeof b, " %d", (int)p->type); d += b; }
                        if (!n) d += " none";
                    }
                }
                d += "]";
            }
            Log("Frame has %u layer(s):%s\n", info->layerCount, d.c_str());
        }
        if (sends) { g_gameNearZ = vdepth[0].nearZ; g_gameFarZ = vdepth[0].farZ; }
        if (now != g_gameDepth && (now == 1 || now == 3 || ++g_noDepthFrames > 90)) {
            g_gameDepth = now;
            if (now == 1)
                Log("Game sends depth (XR_KHR_composition_layer_depth): near %.3f far %.1f, depth %.2f..%.2f%s. Box windows can use it.\n",
                    vdepth[0].nearZ, vdepth[0].farZ, vdepth[0].minDepth, vdepth[0].maxDepth,
                    vdepth[0].nearZ > vdepth[0].farZ ? " (reversed)" : "");
            else if (now == 3)
                Log("Game sends depth, but on projection layer %d, not layer %d where cutouts are drawn: box windows act as solid blocks.\n",
                    depthLayer, projIdx);
            else if (now == 2)
                Log("Game made a depth image but doesn't send it with its frames: box windows act as solid blocks. "
                    "UEVR: turn on Enable Depth in its VR settings, then restart the game.\n");
            else
                Log("Game sends NO depth: box windows act as solid blocks in this game.\n");
        }
    }
    g_endViews.resize(proj.viewCount, { XR_TYPE_VIEW });
    for (uint32_t i = 0; i < proj.viewCount; ++i) { g_endViews[i].pose = proj.views[i].pose; g_endViews[i].fov = proj.views[i].fov; }
    g_endSpace = proj.space;
    if (g_endTime && info->displayTime > g_endTime) g_endPeriod = info->displayTime - g_endTime;
    g_endTime = info->displayTime;

    if (!drewAtRelease) {
        // Fallback (first frame, or the game doesn't use xrLocateViews): draw now.
        std::vector<XrView> vv(proj.viewCount, { XR_TYPE_VIEW });
        for (uint32_t i = 0; i < proj.viewCount; ++i) { vv[i].pose = proj.views[i].pose; vv[i].fov = proj.views[i].fov; }
        if (g_updatedFor != info->displayTime) {
            SelfSync();
            UpdateFrame(proj.space, info->displayTime, HeadFromViews(vv.data(), proj.viewCount));
            g_updatedFor = info->displayTime;
        }
        if (!HasAnythingToDraw()) return nEndFrame(session, info);
        static std::vector<DrawView> dv; dv.clear();
        for (uint32_t i = 0; i < proj.viewCount; ++i)
            dv.push_back({ proj.views[i].subImage.swapchain, proj.views[i].subImage.imageRect,
                           proj.views[i].subImage.imageArrayIndex, proj.views[i].pose, proj.views[i].fov, vdepth[i] });
        DrawMask(dv);
        static int fallbackLogs = 0;
        if (fallbackLogs < 3) { Log("Drew cutouts at end of frame (fallback).\n"); ++fallbackLogs; }
    } else {
        static bool loggedRelease = false;
        if (!loggedRelease) { Log("Drawing cutouts before image release (normal path).\n"); loggedRelease = true; }
    }

    // Alpha mode: the game image's alpha shows passthrough. Key colour mode (or a runtime with no way to
    // show alpha) leaves the game's layer flags and blend mode alone: the colour itself is the signal.
    bool alphaOut = !UseKeyColour() && (g_alphaSupported || g_forceAlpha);
    if (!alphaOut) return nEndFrame(session, info);
    proj.layerFlags |= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    static std::vector<const XrCompositionLayerBaseHeader*> layers; layers.assign(info->layers, info->layers + info->layerCount);
    layers[projIdx] = (const XrCompositionLayerBaseHeader*)&proj;
    XrFrameEndInfo fei = *info;
    fei.layers = layers.data();
    if (g_alphaSupported) fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;

    static bool logged = false;
    if (!logged) { Log("First masked frame submitted (blend %s).\n", g_alphaSupported ? "ALPHA_BLEND" : "game default"); logged = true; }
    return nEndFrame(session, &fei);
}

static XrResult XRAPI_CALL Hook_xrDestroyInstance(XrInstance instance) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        ReleaseActions();
        if (g_ctl.set && nDestroyActionSet) nDestroyActionSet(g_ctl.set);
        g_ctl = CtlActions();
        g_gameProfiles.clear();
        ReleaseHands();
        ReleaseGraphics();
        g_session = XR_NULL_HANDLE;
        StopHotkeys();
    }
    return nDestroyInstance(instance);
}

static XrResult XRAPI_CALL Layer_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    XrResult r = nGetInstanceProcAddr(instance, name, fn);
    if (XR_FAILED(r)) return r;
#define HOOK(x) if (strcmp(name, #x) == 0) { *fn = (PFN_xrVoidFunction)Hook_##x; return r; }
    HOOK(xrCreateSession) HOOK(xrDestroySession) HOOK(xrCreateSwapchain) HOOK(xrDestroySwapchain)
    HOOK(xrAcquireSwapchainImage) HOOK(xrReleaseSwapchainImage) HOOK(xrLocateViews) HOOK(xrEndFrame)
    HOOK(xrDestroyInstance) HOOK(xrAttachSessionActionSets) HOOK(xrSyncActions) HOOK(xrCreateActionSet)
    HOOK(xrSuggestInteractionProfileBindings)
#undef HOOK
    if (strcmp(name, "xrGetInstanceProcAddr") == 0) *fn = (PFN_xrVoidFunction)Layer_xrGetInstanceProcAddr;
    return r;
}

static XrResult XRAPI_CALL Layer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* layerInfo, XrInstance* instance) {
    InitDirAndLog();
    if (!layerInfo || !layerInfo->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;

    XrApiLayerCreateInfo next = *layerInfo;
    next.nextInfo = layerInfo->nextInfo->next;

    // Ask for hand tracking, system pinch and eye gaze on the game's behalf; drop what the
    // runtime refuses, most optional first.
    auto tryCreate = [&](bool hands, bool interaction, bool eyes, bool source) {
        std::vector<const char*> exts(info->enabledExtensionNames, info->enabledExtensionNames + info->enabledExtensionCount);
        auto want = [&](const char* name) {
            for (auto* e : exts) if (strcmp(e, name) == 0) return;
            exts.push_back(name);
        };
        if (hands) want(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if (interaction) want(XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
        if (eyes) want(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
        if (source) want(XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME);
        XrInstanceCreateInfo ici = *info;
        ici.enabledExtensionCount = (uint32_t)exts.size();
        ici.enabledExtensionNames = exts.data();
        return layerInfo->nextInfo->nextCreateApiLayerInstance(&ici, &next, instance);
    };
    static const bool combos[][4] = { { true, true, true, true }, { true, true, true, false }, { true, true, false, false },
                                      { true, false, false, false }, { false, false, false, false } };
    XrResult r = XR_ERROR_INITIALIZATION_FAILED;
    for (auto& c : combos) {
        r = tryCreate(c[0], c[1], c[2], c[3]);
        if (XR_SUCCEEDED(r)) {
            g_handExtEnabled = c[0]; g_handInteractionEnabled = c[1]; g_eyeExtEnabled = c[2]; g_handSourceEnabled = c[3];
            break;
        }
        Log("Runtime refused extensions (hands %d, pinch %d, eyes %d, hand source %d): %d, trying fewer.\n", c[0], c[1], c[2], c[3], (int)r);
    }
    if (XR_FAILED(r)) { Log("Next xrCreateInstance failed %d\n", (int)r); return r; }
    g_instance = *instance;

    nGetInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
#define GET(x) nGetInstanceProcAddr(*instance, "xr" #x, (PFN_xrVoidFunction*)&n##x)
    GET(DestroyInstance); GET(CreateSession); GET(DestroySession); GET(CreateSwapchain); GET(DestroySwapchain);
    GET(EnumerateSwapchainImages); GET(AcquireSwapchainImage); GET(ReleaseSwapchainImage); GET(LocateViews);
    GET(EndFrame); GET(EnumerateEnvironmentBlendModes); GET(GetSystemProperties);
    if (g_handExtEnabled) { GET(CreateHandTrackerEXT); GET(DestroyHandTrackerEXT); GET(LocateHandJointsEXT); }
    GET(StringToPath); GET(CreateActionSet); GET(CreateAction); GET(SuggestInteractionProfileBindings);
    GET(AttachSessionActionSets); GET(SyncActions); GET(GetActionStateFloat); GET(CreateActionSpace);
    GET(LocateSpace); GET(DestroySpace); GET(DestroyActionSet); GET(PathToString); GET(GetActionStateBoolean); GET(GetActionStateVector2f); GET(GetCurrentInteractionProfile);
#undef GET
    LoadSettings();
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        CreateCtlActions(*instance);   // before the game suggests its bindings
    }

    g_appName = info->applicationInfo.applicationName;
    XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
    PFN_xrGetInstanceProperties gip = nullptr;
    nGetInstanceProcAddr(*instance, "xrGetInstanceProperties", (PFN_xrVoidFunction*)&gip);
    if (gip) gip(*instance, &ip);
    g_runtimeName = ip.runtimeName;
    g_viaVD = g_runtimeName.find("SteamVR") != std::string::npos && VirtualDesktopRunning();
    if (g_viaVD) Log("SteamVR is streamed by Virtual Desktop: sharing Virtual Desktop's cutout colour setting.\n");
    LoadKeyMode();
    Log("Alpha Views XR " LAYER_VERSION " loaded. Game: %s  Runtime: %s\n", g_appName.c_str(), ip.runtimeName);
    LoadWindows();
    return r;
}

// 32-bit builds: __stdcall decorates the name (_xrNegotiateLoaderApiLayerInterface@12); the OpenXR
// loader looks it up undecorated, so export that name too.
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:xrNegotiateLoaderApiLayerInterface=_xrNegotiateLoaderApiLayerInterface@12")
#endif
LAYER_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* req) {
    if (!loaderInfo || !req || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        req->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (layerName && strcmp(layerName, LAYER_NAME) != 0) return XR_ERROR_INITIALIZATION_FAILED;
    req->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    req->layerApiVersion = XR_CURRENT_API_VERSION;
    req->getInstanceProcAddr = Layer_xrGetInstanceProcAddr;
    req->createApiLayerInstance = Layer_xrCreateApiLayerInstance;
    return XR_SUCCESS;
}
