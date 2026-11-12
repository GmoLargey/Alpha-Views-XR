// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GmoLargey. Alpha Views XR test scene (GNU GPL version 3, see LICENSE.txt).
//
// Alpha passthrough test for SteamVR + PICO Connect.
// Draws a solid spinning cube 60 cm in front of you, just below eye level. Everything else is drawn
// with alpha = 0. If PICO Connect's "alpha" option works through SteamVR,
// you will see your real room around the cube.
//
// Keys (work even when the console is not focused):
//   1  background transparent black   (alpha 0)
//   2  background transparent magenta (alpha 0, tells alpha vs. color-key)
//   3  background solid blue          (alpha 1, control)
//   4  background half transparent    (alpha 0.5)
//   O  toggle blend mode OPAQUE / ALPHA_BLEND (if the runtime offers both)
//   R  place the scene in front of you again (also after a recentre)
//   D  toggle sending the depth buffer (XR_KHR_composition_layer_depth), for box windows
//   Esc quit
// A grey panel 1.8 m away stays still behind the cube (stands in for a dashboard).

static const float kCubeY = -0.10f, kCubeZ = -0.60f;   // cube centre from the eyes (m): below, ahead

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

using namespace DirectX;

static FILE* g_log = nullptr;
static void Log(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt); vprintf(fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
}

#define XR_CHECK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { Log("FAILED %s -> %d (line %d)\n", #x, (int)r_, __LINE__); return 1; } } while (0)
#define HR_CHECK(x) do { HRESULT h_ = (x); if (FAILED(h_)) { Log("FAILED %s -> 0x%08X (line %d)\n", #x, (unsigned)h_, __LINE__); return 1; } } while (0)

static const char* BlendName(XrEnvironmentBlendMode m) {
    switch (m) {
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
    default: return "UNKNOWN";
    }
}

static const char* kShader = R"(
cbuffer C : register(b0) { float4x4 mvp; };
struct VI { float3 pos : POSITION; float3 col : COLOR; };
struct VO { float4 pos : SV_POSITION; float3 col : COLOR; };
VO vs(VI i) { VO o; o.pos = mul(float4(i.pos, 1), mvp); o.col = i.col; return o; }
float4 ps(VO i) : SV_TARGET { return float4(i.col, 1); }
)";

struct Vtx { float x, y, z, r, g, b; };

struct Eye {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int32_t w = 0, h = 0;
    std::vector<ID3D11RenderTargetView*> rtvs;
    ID3D11DepthStencilView* dsv = nullptr;          // own depth buffer (no depth swapchain)
    XrSwapchain depthSwapchain = XR_NULL_HANDLE;    // depth sent to the runtime
    std::vector<ID3D11DepthStencilView*> depthDsvs;
};

int main() {
    // Log to %LOCALAPPDATA%\AlphaViewsXR (the install folder may not be writable).
    char dirBuf[MAX_PATH];
    DWORD dn = GetEnvironmentVariableA("LOCALAPPDATA", dirBuf, MAX_PATH);
    std::string dir = (dn ? std::string(dirBuf) : std::string(".")) + "\\AlphaViewsXR";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string logPath = dir + "\\test_scene_log.txt";
    g_log = fopen(logPath.c_str(), "w");

    Log("=== Alpha Views XR test scene ===\nLog file: %s\n\n", logPath.c_str());

    // ---------- Instance ----------
    bool depthExt = false;
    {
        uint32_t ne = 0;
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &ne, nullptr);
        std::vector<XrExtensionProperties> ep(ne, { XR_TYPE_EXTENSION_PROPERTIES });
        xrEnumerateInstanceExtensionProperties(nullptr, ne, &ne, ep.data());
        for (auto& e : ep) if (strcmp(e.extensionName, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) == 0) depthExt = true;
    }
    Log("Runtime depth layer extension: %s\n", depthExt ? "yes" : "no");
    const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME };
    XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
    strcpy_s(ici.applicationInfo.applicationName, "AlphaPassthroughTest");
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ici.enabledExtensionCount = depthExt ? 2 : 1;
    ici.enabledExtensionNames = exts;
    XrInstance instance;
    XR_CHECK(xrCreateInstance(&ici, &instance));

    XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
    xrGetInstanceProperties(instance, &ip);
    Log("Runtime: %s %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
        XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId systemId;
    XR_CHECK(xrGetSystem(instance, &sgi, &systemId));
    XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
    xrGetSystemProperties(instance, systemId, &sp);
    Log("System: %s\n", sp.systemName);

    // ---------- Blend modes (key result #1) ----------
    uint32_t nb = 0;
    xrEnumerateEnvironmentBlendModes(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nb, nullptr);
    std::vector<XrEnvironmentBlendMode> blends(nb);
    xrEnumerateEnvironmentBlendModes(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nb, &nb, blends.data());
    bool hasAlpha = false, hasOpaque = false;
    Log("Blend modes offered by runtime:");
    for (auto m : blends) {
        Log(" %s", BlendName(m));
        if (m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) hasAlpha = true;
        if (m == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) hasOpaque = true;
    }
    Log("\n");
    XrEnvironmentBlendMode blend = hasAlpha ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : blends[0];
    Log(hasAlpha ? "-> ALPHA_BLEND is offered. Using it.\n"
                 : "-> ALPHA_BLEND NOT offered. Submitting alpha anyway in case PICO Connect reads it.\n");

    // ---------- D3D11 device on the runtime's adapter ----------
    PFN_xrGetD3D11GraphicsRequirementsKHR getReqs = nullptr;
    XR_CHECK(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReqs));
    XrGraphicsRequirementsD3D11KHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
    XR_CHECK(getReqs(instance, systemId, &req));

    IDXGIFactory1* factory = nullptr;
    HR_CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory));
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d; adapter->GetDesc1(&d);
        if (memcmp(&d.AdapterLuid, &req.adapterLuid, sizeof(LUID)) == 0) {
            Log("GPU: %ls\n", d.Description);
            break;
        }
        adapter->Release(); adapter = nullptr;
    }
    D3D_FEATURE_LEVEL fl = std::max(req.minFeatureLevel, D3D_FEATURE_LEVEL_11_0);
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HR_CHECK(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx));

    // ---------- Session ----------
    XrGraphicsBindingD3D11KHR gb{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
    gb.device = dev;
    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = systemId;
    XrSession session;
    XR_CHECK(xrCreateSession(instance, &sci, &session));

    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1;
    XrSpace space;
    XR_CHECK(xrCreateReferenceSpace(session, &rsci, &space));

    // ---------- Swapchain format (must have an alpha channel) ----------
    uint32_t nf = 0;
    xrEnumerateSwapchainFormats(session, 0, &nf, nullptr);
    std::vector<int64_t> formats(nf);
    xrEnumerateSwapchainFormats(session, nf, &nf, formats.data());
    const DXGI_FORMAT preferred[] = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                                      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                      DXGI_FORMAT_R16G16B16A16_FLOAT };
    DXGI_FORMAT colorFmt = DXGI_FORMAT_UNKNOWN;
    for (auto p : preferred) {
        for (auto f : formats) if (f == p) { colorFmt = p; break; }
        if (colorFmt != DXGI_FORMAT_UNKNOWN) break;
    }
    if (colorFmt == DXGI_FORMAT_UNKNOWN) { Log("No RGBA swapchain format available.\n"); return 1; }
    Log("Swapchain format: DXGI %d\n", (int)colorFmt);
    DXGI_FORMAT depthFmt = DXGI_FORMAT_UNKNOWN;
    if (depthExt) {
        const DXGI_FORMAT dpref[] = { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D16_UNORM };
        for (auto p : dpref) {
            for (auto f : formats) if (f == p) { depthFmt = p; break; }
            if (depthFmt != DXGI_FORMAT_UNKNOWN) break;
        }
        Log("Depth swapchain format: %s\n", depthFmt ? std::to_string((int)depthFmt).c_str() : "none offered");
    }

    // ---------- Per-eye swapchains ----------
    uint32_t nv = 0;
    xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, nullptr);
    std::vector<XrViewConfigurationView> vcv(nv, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nv, &nv, vcv.data());

    std::vector<Eye> eyes(nv);
    for (uint32_t e = 0; e < nv; ++e) {
        Eye& eye = eyes[e];
        eye.w = vcv[e].recommendedImageRectWidth;
        eye.h = vcv[e].recommendedImageRectHeight;
        XrSwapchainCreateInfo sc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = colorFmt;
        sc.sampleCount = 1;
        sc.width = eye.w; sc.height = eye.h;
        sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
        XR_CHECK(xrCreateSwapchain(session, &sc, &eye.swapchain));

        uint32_t ni = 0;
        xrEnumerateSwapchainImages(eye.swapchain, 0, &ni, nullptr);
        std::vector<XrSwapchainImageD3D11KHR> imgs(ni, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
        xrEnumerateSwapchainImages(eye.swapchain, ni, &ni, (XrSwapchainImageBaseHeader*)imgs.data());
        for (auto& img : imgs) {
            D3D11_RENDER_TARGET_VIEW_DESC rd{};
            rd.Format = colorFmt;
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            ID3D11RenderTargetView* rtv = nullptr;
            HR_CHECK(dev->CreateRenderTargetView(img.texture, &rd, &rtv));
            eye.rtvs.push_back(rtv);
        }
        D3D11_TEXTURE2D_DESC dd{};
        dd.Width = eye.w; dd.Height = eye.h; dd.MipLevels = 1; dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_D32_FLOAT; dd.SampleDesc.Count = 1;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D* dt = nullptr;
        HR_CHECK(dev->CreateTexture2D(&dd, nullptr, &dt));
        HR_CHECK(dev->CreateDepthStencilView(dt, nullptr, &eye.dsv));
        dt->Release();

        if (depthFmt != DXGI_FORMAT_UNKNOWN) {
            XrSwapchainCreateInfo dsc = sc;
            dsc.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            dsc.format = depthFmt;
            if (XR_SUCCEEDED(xrCreateSwapchain(session, &dsc, &eye.depthSwapchain))) {
                uint32_t nd = 0;
                xrEnumerateSwapchainImages(eye.depthSwapchain, 0, &nd, nullptr);
                std::vector<XrSwapchainImageD3D11KHR> dimgs(nd, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
                xrEnumerateSwapchainImages(eye.depthSwapchain, nd, &nd, (XrSwapchainImageBaseHeader*)dimgs.data());
                for (auto& img : dimgs) {
                    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
                    dvd.Format = depthFmt;
                    dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                    ID3D11DepthStencilView* d = nullptr;
                    HR_CHECK(dev->CreateDepthStencilView(img.texture, &dvd, &d));
                    eye.depthDsvs.push_back(d);
                }
            } else {
                Log("Depth swapchain create failed; not sending depth.\n");
                eye.depthSwapchain = XR_NULL_HANDLE;
            }
        }
    }
    bool sendDepth = !eyes.empty() && eyes[0].depthSwapchain != XR_NULL_HANDLE;
    Log("Sending depth to the runtime: %s (D toggles)\n", sendDepth ? "yes" : "no");
    Log("Eye size: %d x %d\n", eyes[0].w, eyes[0].h);

    // ---------- Cube pipeline ----------
    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err))) {
        Log("Shader compile failed: %s\n", err ? (char*)err->GetBufferPointer() : "?");
        return 1;
    }
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    HR_CHECK(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs));
    HR_CHECK(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps));
    D3D11_INPUT_ELEMENT_DESC ied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11InputLayout* il = nullptr;
    HR_CHECK(dev->CreateInputLayout(ied, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &il));

    const float s = 0.15f; // 30 cm cube
    Vtx verts[] = {
        {-s,-s,-s, 1,0,0}, { s,-s,-s, 0,1,0}, { s, s,-s, 0,0,1}, {-s, s,-s, 1,1,0},
        {-s,-s, s, 1,0,1}, { s,-s, s, 0,1,1}, { s, s, s, 1,1,1}, {-s, s, s, 1,.5f,0},
        // back panel (drawn with its own still model matrix), 1.2 x 0.8 m
        {-0.6f,-0.4f,0, .3f,.3f,.35f}, { 0.6f,-0.4f,0, .3f,.3f,.35f}, { 0.6f,0.4f,0, .5f,.5f,.6f}, {-0.6f,0.4f,0, .5f,.5f,.6f},
    };
    uint16_t idx[] = { 0,1,2, 0,2,3,  4,6,5, 4,7,6,  0,4,5, 0,5,1,  3,2,6, 3,6,7,  0,3,7, 0,7,4,  1,5,6, 1,6,2,
                       8,9,10, 8,10,11 };
    D3D11_BUFFER_DESC bd{ sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA sd{ verts };
    ID3D11Buffer *vb = nullptr, *ib = nullptr, *cb = nullptr;
    HR_CHECK(dev->CreateBuffer(&bd, &sd, &vb));
    bd = { sizeof(idx), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER };
    sd = { idx };
    HR_CHECK(dev->CreateBuffer(&bd, &sd, &ib));
    bd = { sizeof(XMFLOAT4X4), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    HR_CHECK(dev->CreateBuffer(&bd, nullptr, &cb));

    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode = D3D11_FILL_SOLID; rsd.CullMode = D3D11_CULL_NONE; rsd.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    HR_CHECK(dev->CreateRasterizerState(&rsd, &rs));
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_LESS;
    ID3D11DepthStencilState* dss = nullptr;
    HR_CHECK(dev->CreateDepthStencilState(&dsd, &dss));

    // ---------- Main loop ----------
    Log("\nKeys: 1 transparent black | 2 transparent magenta | 3 solid blue | 4 half alpha | O toggle blend | Esc quit\n");
    Log("Put the headset on now.\n\n");

    float bg[4] = { 0, 0, 0, 0 };
    int bgMode = 1;
    bool running = true, sessionRunning = false;
    bool prevKeys[256] = {};
    auto pressed = [&](int vk) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool hit = down && !prevKeys[vk];
        prevKeys[vk] = down;
        return hit;
    };
    LARGE_INTEGER freq, t0; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    bool loggedFirstFrame = false;

    bool placed = false;   // scene anchored to the head; false = place it again next frame
    while (running) {
        if (pressed('R')) { placed = false; Log("R -> scene re-placed in front of you\n"); }
        // Keys
        if (pressed(VK_ESCAPE)) { Log("Esc -> exiting\n"); xrRequestExitSession(session); }
        int newMode = 0;
        if (pressed('1')) newMode = 1;
        if (pressed('2')) newMode = 2;
        if (pressed('3')) newMode = 3;
        if (pressed('4')) newMode = 4;
        if (newMode) {
            bgMode = newMode;
            const float modes[5][4] = { {}, {0,0,0,0}, {1,0,1,0}, {0.05f,0.05f,0.3f,1}, {0,0,0,0.5f} };
            memcpy(bg, modes[bgMode], sizeof(bg));
            const char* names[] = { "", "transparent black", "transparent magenta", "solid blue (control)", "half alpha" };
            Log("Background: %s\n", names[bgMode]);
        }
        if (pressed('D') && !eyes.empty() && eyes[0].depthSwapchain) {
            sendDepth = !sendDepth;
            Log("Sending depth: %s\n", sendDepth ? "yes" : "no");
        }
        if (pressed('O') && hasAlpha && hasOpaque) {
            blend = (blend == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE
                                                                    : XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
            Log("Blend mode: %s\n", BlendName(blend));
        }

        // Events
        XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
        while (xrPollEvent(instance, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto* sc = (XrEventDataSessionStateChanged*)&ev;
                Log("Session state: %d\n", (int)sc->state);
                if (sc->state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo sbi{ XR_TYPE_SESSION_BEGIN_INFO };
                    sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR_CHECK(xrBeginSession(session, &sbi));
                    sessionRunning = true;
                } else if (sc->state == XR_SESSION_STATE_FOCUSED) {
                    placed = false;   // headset put on: place the scene where you are now
                } else if (sc->state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    sessionRunning = false;
                } else if (sc->state == XR_SESSION_STATE_EXITING || sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                    running = false;
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                placed = false;   // recentred: put the scene back in front of the head
                Log("Recentred -> scene re-placed\n");
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                running = false;
            }
            ev = { XR_TYPE_EVENT_DATA_BUFFER };
        }
        if (!sessionRunning) { Sleep(50); continue; }

        // Frame
        XrFrameState fs{ XR_TYPE_FRAME_STATE };
        XR_CHECK(xrWaitFrame(session, nullptr, &fs));
        XR_CHECK(xrBeginFrame(session, nullptr));

        std::vector<XrCompositionLayerProjectionView> pviews(nv, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
        std::vector<XrCompositionLayerDepthInfoKHR> dinfo(nv, { XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR });
        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        bool haveLayer = false;

        if (fs.shouldRender) {
            std::vector<XrView> views(nv, { XR_TYPE_VIEW });
            XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = space;
            XrViewState vs_{ XR_TYPE_VIEW_STATE };
            uint32_t vcount = 0;
            XR_CHECK(xrLocateViews(session, &vli, &vs_, nv, &vcount, views.data()));

            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            float t = float(now.QuadPart - t0.QuadPart) / float(freq.QuadPart);
            // Cube: 10 cm below eye level, 60 cm ahead: within reach, so a hand can go behind it
            // (tests "hands behind objects"). The scene is placed in front of where the headset faces when
            // the app gets focus, after a recentre, or on R
            // (level, yaw only). Only once the headset is really tracked: at startup some runtimes
            // report a not-yet-tracked pose at the space origin (often the floor) for a while.
            static XMMATRIX anchor = XMMatrixIdentity();
            static float anchorY = 0, placedAt = -100;
            // (A runtime that never sets the "tracked" bit is trusted after 3 s.)
            const XrViewStateFlags need = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                                          (t < 3 ? XR_VIEW_STATE_POSITION_TRACKED_BIT : 0);
            const bool tracked = vcount > 0 && (vs_.viewStateFlags & need) == need &&
                                 fabsf(views[0].pose.position.x) + fabsf(views[0].pose.position.y) + fabsf(views[0].pose.position.z) > 1e-4f;
            // Soon after placing, the head moved far up or down (headset picked up / put on): place again.
            if (placed && tracked && t - placedAt < 15 && fabsf(views[0].pose.position.y - anchorY) > 0.3f) {
                placed = false;
                Log("Head moved %.2f m up / down since placing: scene re-placed\n", views[0].pose.position.y - anchorY);
            }
            if (!placed && tracked) {
                const XrPosef& hp = views[0].pose;
                anchorY = hp.position.y; placedAt = t;
                XMVECTOR q = XMVectorSet(hp.orientation.x, hp.orientation.y, hp.orientation.z, hp.orientation.w);
                XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), q);
                float yaw = atan2f(-XMVectorGetX(fwd), -XMVectorGetZ(fwd));
                anchor = XMMatrixRotationY(yaw) * XMMatrixTranslation(hp.position.x, hp.position.y, hp.position.z);
                XMFLOAT3 c; XMStoreFloat3(&c, XMVector3TransformCoord(XMVectorSet(0, kCubeY, kCubeZ, 1), anchor));
                XMFLOAT4 r; XMStoreFloat4(&r, XMQuaternionRotationRollPitchYaw(0, yaw, 0));
                Log("Scene anchor: cube centre %.4f %.4f %.4f, facing quat %.4f %.4f %.4f %.4f\n", c.x, c.y, c.z, r.x, r.y, r.z, r.w);
                placed = true;
            }
            XMMATRIX model = XMMatrixRotationRollPitchYaw(t * 0.4f, t * 0.7f, 0) * XMMatrixTranslation(0, kCubeY, kCubeZ) * anchor;

            for (uint32_t e = 0; e < nv; ++e) {
                Eye& eye = eyes[e];
                uint32_t ii;
                XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                XR_CHECK(xrAcquireSwapchainImage(eye.swapchain, &ai, &ii));
                XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wi.timeout = XR_INFINITE_DURATION;
                XR_CHECK(xrWaitSwapchainImage(eye.swapchain, &wi));
                uint32_t di = 0;
                if (eye.depthSwapchain) {
                    XR_CHECK(xrAcquireSwapchainImage(eye.depthSwapchain, &ai, &di));
                    XR_CHECK(xrWaitSwapchainImage(eye.depthSwapchain, &wi));
                }
                ID3D11DepthStencilView* dsv = eye.depthSwapchain ? eye.depthDsvs[di] : eye.dsv;

                const XrPosef& p = views[e].pose;
                const XrFovf& f = views[e].fov;
                XMMATRIX camWorld = XMMatrixAffineTransformation(XMVectorSet(1, 1, 1, 0), XMVectorZero(),
                    XMVectorSet(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w),
                    XMVectorSet(p.position.x, p.position.y, p.position.z, 1));
                XMMATRIX view = XMMatrixInverse(nullptr, camWorld);
                const float n = 0.05f, fa = 100.0f;
                XMMATRIX proj = XMMatrixPerspectiveOffCenterRH(tanf(f.angleLeft) * n, tanf(f.angleRight) * n,
                    tanf(f.angleDown) * n, tanf(f.angleUp) * n, n, fa);
                XMFLOAT4X4 mvp;
                XMStoreFloat4x4(&mvp, XMMatrixTranspose(model * view * proj));
                ctx->UpdateSubresource(cb, 0, nullptr, &mvp, 0, 0);

                ID3D11RenderTargetView* rtv = eye.rtvs[ii];
                ctx->OMSetRenderTargets(1, &rtv, dsv);
                ctx->ClearRenderTargetView(rtv, bg);
                ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
                D3D11_VIEWPORT vp{ 0, 0, (float)eye.w, (float)eye.h, 0, 1 };
                ctx->RSSetViewports(1, &vp);
                ctx->RSSetState(rs);
                ctx->OMSetDepthStencilState(dss, 0);
                ctx->IASetInputLayout(il);
                UINT stride = sizeof(Vtx), off = 0;
                ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
                ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ctx->VSSetShader(vs, nullptr, 0);
                ctx->VSSetConstantBuffers(0, 1, &cb);
                ctx->PSSetShader(ps, nullptr, 0);
                if (placed) ctx->DrawIndexed(36, 0, 0);   // nothing until placed in front of you
                // still back panel, 1.8 m ahead and a little low
                XMStoreFloat4x4(&mvp, XMMatrixTranspose(XMMatrixTranslation(0, -0.1f, -1.8f) * anchor * view * proj));
                ctx->UpdateSubresource(cb, 0, nullptr, &mvp, 0, 0);
                if (placed) ctx->DrawIndexed(6, 36, 0);

                // Depth is released first, like most engines (it's drawn together with colour).
                XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                if (eye.depthSwapchain) XR_CHECK(xrReleaseSwapchainImage(eye.depthSwapchain, &ri));
                XR_CHECK(xrReleaseSwapchainImage(eye.swapchain, &ri));

                pviews[e].pose = p;
                pviews[e].fov = f;
                pviews[e].subImage.swapchain = eye.swapchain;
                pviews[e].subImage.imageRect = { {0, 0}, {eye.w, eye.h} };
                if (eye.depthSwapchain && sendDepth) {
                    dinfo[e].subImage.swapchain = eye.depthSwapchain;
                    dinfo[e].subImage.imageRect = { {0, 0}, {eye.w, eye.h} };
                    dinfo[e].minDepth = 0; dinfo[e].maxDepth = 1;
                    dinfo[e].nearZ = n; dinfo[e].farZ = fa;
                    pviews[e].next = &dinfo[e];
                }
            }
            layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                               XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            layer.space = space;
            layer.viewCount = nv;
            layer.views = pviews.data();
            haveLayer = true;
        }

        const XrCompositionLayerBaseHeader* layers[] = { (XrCompositionLayerBaseHeader*)&layer };
        XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = blend;
        fei.layerCount = haveLayer ? 1 : 0;
        fei.layers = layers;
        XrResult er = xrEndFrame(session, &fei);
        if (XR_FAILED(er)) { Log("xrEndFrame failed: %d (blend %s)\n", (int)er, BlendName(blend)); return 1; }
        if (haveLayer && !loggedFirstFrame) {
            Log("First frame submitted. Blend mode %s, background transparent black.\n", BlendName(blend));
            loggedFirstFrame = true;
        }
    }

    xrDestroySession(session);
    xrDestroyInstance(instance);
    Log("Done.\n");
    if (g_log) fclose(g_log);
    return 0;
}
