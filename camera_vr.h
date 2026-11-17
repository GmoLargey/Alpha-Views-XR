// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GmoLargey. Part of Alpha Views XR (GNU GPL version 3, see LICENSE.txt).
//
// Camera mode (1.6.0): the headset's own cameras through SteamVR (Valve Index, Vive Pro, Reverb G2
// with the Oasis driver, ...). Only with SteamVR as the OpenXR runtime: the layer connects to SteamVR
// from inside the game as a background OpenVR app (as openxr-steamvr-passthrough does), reads the
// camera pictures on its own thread, and the cutouts are painted with the camera picture instead of
// being made transparent. The game's frames stay solid, so no alpha channel is needed.
//
// openvr_api (BSD-3-Clause, from the OpenVR SDK) is loaded at run time from beside the layer, only
// when the runtime is SteamVR: nothing is linked, and other runtimes never load it.
//
// camera.cfg (optional, in the settings folder; as in Alpha Views VR), for testing new headsets:
//   frame=1|2    1 Undistorted, 2 MaximumUndistorted (default)
//   pose=0|1     0 the frame header's pose (default), 1 always the pose at exposure time
//   camera=N     which camera's picture to use (default 0)
// Dev / tests only: ALPHA_VIEWS_FAKE_CAMERA=1 makes up a camera (a test pattern seen from each eye).

#include <openvr.h>
#include <atomic>
#include <thread>

// The newest camera picture and how to look it up.
struct CamPicture {
    std::vector<uint8_t> px;            // RGBA bytes
    uint32_t w = 0, h = 0;
    uint32_t seq = 0;                   // 0: none yet
    XMFLOAT4X4 toClip{};                // standing space (or the eye's view space, eyeRelative) -> camera clip, row vectors
    XMFLOAT4 rect{ 0, 0, 1, 1 };        // the chosen camera's part of the picture: u0, v0, du, dv
    bool alphaMask = false;             // alpha 0 marks pixels with no camera data
    bool eyeRelative = false;           // fake camera: toClip starts from the eye's view space
};

static bool g_camAvailable = false;     // SteamVR reports a headset camera (or the fake one)
static bool g_camFake = false;
static bool g_camTried = false;         // OpenVR connection attempted (once per process)
static vr::IVRSystem* g_vrSys = nullptr;
static vr::IVRTrackedCamera* g_vrCam = nullptr;
static std::mutex g_camMx;              // guards g_camMid, g_camStatus
static CamPicture g_camMid;             // handed over from the camera thread
static bool g_camMidNew = false;
static std::string g_camStatus = "off";
// A pointer, not a std::thread object: a game that quits without ending its session must not hit
// std::thread's "still joinable" terminate when the layer is unloaded.
static std::thread* g_camThread = nullptr;
static std::atomic<bool> g_camRun{ false };
static vr::EVRTrackedCameraFrameType g_camFrameType = vr::VRTrackedCameraFrameType_MaximumUndistorted;
static int g_camPoseMode = 0, g_camIndex = 0;

static void CamSetStatus(const std::string& s) { std::lock_guard<std::mutex> l(g_camMx); g_camStatus = s; }
static std::string CamStatus() { std::lock_guard<std::mutex> l(g_camMx); return g_camStatus; }

static const char* kCamFrameName[3] = { "Distorted", "Undistorted", "MaximumUndistorted" };

// Path of a file beside this layer (the DLL / .so).
static std::string BesideLayer(const char* name) {
#ifdef _WIN32
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&BesideLayer, &self);
    char p[MAX_PATH] = {};
    GetModuleFileNameA(self, p, MAX_PATH);
    std::string s = p;
    size_t k = s.find_last_of("\\/");
    return (k == std::string::npos ? std::string(".") : s.substr(0, k)) + "\\" + name;
#else
    Dl_info di{};
    std::string s = dladdr((void*)&BesideLayer, &di) && di.dli_fname ? di.dli_fname : "./x";
    size_t k = s.find_last_of('/');
    return (k == std::string::npos ? std::string(".") : s.substr(0, k)) + "/" + name;
#endif
}

static void LoadCameraConfig() {
    FILE* f = fopen((g_dir + PATH_SEP "camera.cfg").c_str(), "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        int v = 0;
        if (sscanf(line, " frame = %d", &v) == 1 && v >= 1 && v <= 2) g_camFrameType = (vr::EVRTrackedCameraFrameType)v;
        else if (sscanf(line, " pose = %d", &v) == 1) g_camPoseMode = v ? 1 : 0;
        else if (sscanf(line, " camera = %d", &v) == 1) g_camIndex = std::clamp(v, 0, 3);
    }
    fclose(f);
    Log("camera.cfg: frame type %s, pose mode %d, camera %d\n", kCamFrameName[g_camFrameType], g_camPoseMode, g_camIndex);
}

// Connect to SteamVR (once) and ask whether the headset has a camera. SteamVR runtime only.
static bool CamDetect(bool steamvr) {
    if (g_camTried) return g_camAvailable;
    g_camTried = true;
    if (!EnvVar("ALPHA_VIEWS_FAKE_CAMERA").empty()) {
        g_camFake = g_camAvailable = true;
        Log("Headset camera: FAKE (ALPHA_VIEWS_FAKE_CAMERA, a test pattern)\n");
        return true;
    }
    if (!steamvr) return false;
    typedef uint32_t(VR_CALLTYPE * PFN_Init)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
    typedef void*(VR_CALLTYPE * PFN_Iface)(const char*, vr::EVRInitError*);
    std::string path = BesideLayer(
#ifdef _WIN32
        "openvr_api.dll");
    HMODULE lib = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto sym = [&](const char* n) { return lib ? (void*)GetProcAddress(lib, n) : nullptr; };
#else
        "libopenvr_api.so");
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto sym = [&](const char* n) { return lib ? dlsym(lib, n) : nullptr; };
#endif
    auto init = (PFN_Init)sym("VR_InitInternal2");
    auto iface = (PFN_Iface)sym("VR_GetGenericInterface");
    if (!init || !iface) { Log("Headset camera: can't load %s, camera mode unavailable.\n", path.c_str()); return false; }
    vr::EVRInitError e = vr::VRInitError_None;
    init(&e, vr::VRApplication_Background, nullptr);
    if (e != vr::VRInitError_None) { Log("Headset camera: SteamVR connection failed (%d), camera mode unavailable.\n", (int)e); return false; }
    g_vrSys = (vr::IVRSystem*)iface(vr::IVRSystem_Version, &e);
    g_vrCam = (vr::IVRTrackedCamera*)iface(vr::IVRTrackedCamera_Version, &e);
    if (!g_vrSys || !g_vrCam) { Log("Headset camera: SteamVR has no camera interface (%d).\n", (int)e); return false; }
    bool has = false;
    g_vrCam->HasCamera(vr::k_unTrackedDeviceIndex_Hmd, &has);
    const auto hmd = vr::k_unTrackedDeviceIndex_Hmd;
    char model[128] = "", drv[128] = "";
    g_vrSys->GetStringTrackedDeviceProperty(hmd, vr::Prop_ModelNumber_String, model, sizeof model);
    g_vrSys->GetStringTrackedDeviceProperty(hmd, vr::Prop_TrackingSystemName_String, drv, sizeof drv);
    int32_t nCams = g_vrSys->GetInt32TrackedDeviceProperty(hmd, vr::Prop_NumCameras_Int32);
    int32_t layout = g_vrSys->GetInt32TrackedDeviceProperty(hmd, vr::Prop_CameraFrameLayout_Int32);
    Log("Headset camera: %s (%s, driver %s, %d camera(s), layout 0x%x)\n", has ? "yes" : "none for SteamVR", model, drv, nCams, layout);
    if (has) LoadCameraConfig();
    g_camAvailable = has;
    return has;
}

// How long ago a frame's exposure was (the performance counter on Windows, CLOCK_MONOTONIC ns on
// Linux). 0 when the value doesn't look sane.
static float CamSecondsSince(uint64_t t) {
    if (!t) return 0;
#ifdef _WIN32
    LARGE_INTEGER now, fq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq);
    double s = ((double)now.QuadPart - (double)t) / (double)fq.QuadPart;
#else
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double s = ((double)ts.tv_sec * 1e9 + ts.tv_nsec - (double)t) / 1e9;
#endif
    return s >= 0 && s <= 0.2 ? (float)s : 0.0f;
}

static XMMATRIX CamFromHmd(const vr::HmdMatrix34_t& m) {   // OpenVR's column vectors -> row vectors
    return XMMATRIX(m.m[0][0], m.m[1][0], m.m[2][0], 0, m.m[0][1], m.m[1][1], m.m[2][1], 0,
                    m.m[0][2], m.m[1][2], m.m[2][2], 0, m.m[0][3], m.m[1][3], m.m[2][3], 1);
}

static void CamPublish(CamPicture& back) {
    std::lock_guard<std::mutex> l(g_camMx);
    std::swap(back, g_camMid);
    g_camMidNew = true;
}

// Fake camera: a test pattern (checks, colour ramps, a moving bar, an invalid border) seen through a
// 100 x 80 degree camera at each eye.
static void CamFakeLoop() {
    CamPicture back;
    uint32_t seq = 0;
    CamSetStatus("showing (fake)");
    while (g_camRun) {
        const uint32_t W = 640, H = 480;
        back.px.resize(size_t(W) * H * 4);
        ++seq;
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t* p = &back.px[(size_t(y) * W + x) * 4];
                bool chk = ((x / 40) + (y / 40)) & 1;
                bool bar = ((x + seq * 4) % W) < 16;
                p[0] = (uint8_t)(chk ? 40 + x * 200 / W : 230);
                p[1] = (uint8_t)(chk ? 40 + y * 200 / H : 230);
                p[2] = (uint8_t)(bar ? 255 : 60);
                p[3] = (x < 8 || y < 8 || x >= W - 8 || y >= H - 8) ? 0 : 255;
            }
        back.w = W; back.h = H; back.seq = seq;
        back.rect = { 0, 0, 1, 1 };
        back.alphaMask = true;
        back.eyeRelative = true;
        XMStoreFloat4x4(&back.toClip, XMMatrixPerspectiveFovRH(80 * XM_PI / 180, tanf(50 * XM_PI / 180) / tanf(40 * XM_PI / 180), 0.05f, 100.0f));
        CamPublish(back);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

static void CamLoop() {
    auto* tc = g_vrCam;
    auto* sys = g_vrSys;
    const auto hmd = vr::k_unTrackedDeviceIndex_Hmd;
    vr::TrackedCameraHandle_t h = INVALID_TRACKED_CAMERA_HANDLE;
    tc->SetCameraTrackingSpace(vr::TrackingUniverseStanding);
    vr::EVRTrackedCameraError e = tc->AcquireVideoStreamingService(hmd, &h);
    if (e != vr::VRTrackedCameraError_None) {
        CamSetStatus(std::string("couldn't start: ") + tc->GetCameraErrorNameFromEnum(e));
        Log("Camera: couldn't start streaming (%s)\n", tc->GetCameraErrorNameFromEnum(e));
        return;
    }
    Log("Camera streaming started.\n");
    CamSetStatus("waiting for pictures");
    CamPicture back;
    std::vector<uint8_t> raw;
    uint32_t lastSeq = 0;
    int lastErr = -1;
    bool logged = false, loggedPose = false;
    auto type = g_camFrameType;
    while (g_camRun) {
        uint32_t fw = 0, fh = 0, fbytes = 0;
        e = tc->GetCameraFrameSize(hmd, type, &fw, &fh, &fbytes);
        if (type == vr::VRTrackedCameraFrameType_MaximumUndistorted && (e != vr::VRTrackedCameraError_None || !fw || !fh)) {
            Log("Camera: no %s frames (%s), using Undistorted\n", kCamFrameName[type], tc->GetCameraErrorNameFromEnum(e));
            type = vr::VRTrackedCameraFrameType_Undistorted;
            continue;
        }
        vr::CameraVideoStreamFrameHeader_t hdr{};
        if (e == vr::VRTrackedCameraError_None && fw && fh && fbytes >= fw * fh * 3) {
            // Header first: copy the picture only when it's new.
            e = tc->GetVideoStreamFrameBuffer(h, type, nullptr, 0, &hdr, sizeof hdr);
            if (e == vr::VRTrackedCameraError_None && hdr.nFrameSequence == lastSeq) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            raw.resize(fbytes);
            e = tc->GetVideoStreamFrameBuffer(h, type, raw.data(), fbytes, &hdr, sizeof hdr);
        } else if (e == vr::VRTrackedCameraError_None) e = vr::VRTrackedCameraError_NoFrameAvailable;
        if (e != vr::VRTrackedCameraError_None) {
            if ((int)e != lastErr) { Log("Camera frame: %s\n", tc->GetCameraErrorNameFromEnum(e)); lastErr = (int)e; }
            CamSetStatus(e == vr::VRTrackedCameraError_NoFrameAvailable ? "waiting for pictures" : tc->GetCameraErrorNameFromEnum(e));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        lastErr = -1;
        if (hdr.nFrameSequence == lastSeq) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        lastSeq = hdr.nFrameSequence;
        // RGBA (or RGB, padded to RGBA).
        uint32_t bpp = fbytes / (fw * fh) >= 4 ? 4 : 3;
        back.px.resize(size_t(fw) * fh * 4);
        if (bpp == 4) memcpy(back.px.data(), raw.data(), back.px.size());
        else for (size_t i = 0, n = size_t(fw) * fh; i < n; ++i) {
            back.px[i * 4] = raw[i * 3]; back.px[i * 4 + 1] = raw[i * 3 + 1]; back.px[i * 4 + 2] = raw[i * 3 + 2]; back.px[i * 4 + 3] = 255;
        }
        back.w = fw; back.h = fh; back.seq = lastSeq ? lastSeq : 1;
        back.alphaMask = bpp == 4 && type == vr::VRTrackedCameraFrameType_MaximumUndistorted;
        back.eyeRelative = false;
        // The chosen camera's part of a stereo frame.
        int32_t layout = sys->GetInt32TrackedDeviceProperty(hmd, vr::Prop_CameraFrameLayout_Int32);
        int32_t nCams = std::max(1, sys->GetInt32TrackedDeviceProperty(hmd, vr::Prop_NumCameras_Int32));
        int ci = std::min(g_camIndex, nCams - 1);
        back.rect = { 0, 0, 1, 1 };
        if (layout & vr::EVRTrackedCameraFrameLayout_Stereo) {
            if (layout & vr::EVRTrackedCameraFrameLayout_VerticalLayout) back.rect = { 0, 0.5f * (ci & 1), 1, 0.5f };
            else back.rect = { 0.5f * (ci & 1), 0, 0.5f, 1 };
        }
        // Camera pose: the headset at capture (the header's, or predicted back to the exposure) times
        // the camera's place on the headset.
        vr::HmdMatrix34_t c2hArr[4]{};
        vr::ETrackedPropertyError pe = vr::TrackedProp_Success;
        uint32_t got = sys->GetArrayTrackedDeviceProperty(hmd, vr::Prop_CameraToHeadTransforms_Matrix34_Array,
                                                          vr::k_unHmdMatrix34PropertyTag, c2hArr, sizeof c2hArr, &pe);
        XMMATRIX c2h = got >= (ci + 1) * sizeof(vr::HmdMatrix34_t) ? CamFromHmd(c2hArr[ci])
                     : CamFromHmd(sys->GetMatrix34TrackedDeviceProperty(hmd, vr::Prop_CameraToHeadTransform_Matrix34));
        bool hdrPose = g_camPoseMode == 0 && hdr.trackedDevicePose.bPoseIsValid;
        float ago = CamSecondsSince(hdr.ulFrameExposureTime);
        XMMATRIX head = XMMatrixIdentity();
        bool poseOk = hdrPose;
        if (hdrPose) head = CamFromHmd(hdr.trackedDevicePose.mDeviceToAbsoluteTracking);
        else {
            vr::TrackedDevicePose_t past{};
            sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, -ago, &past, 1);
            if (past.bPoseIsValid) { head = CamFromHmd(past.mDeviceToAbsoluteTracking); poseOk = true; }
        }
        if (!loggedPose) {
            Log("Camera pose: %s (header pose %s, frame %.0f ms old)\n", hdrPose ? "frame header" : "pose at exposure time",
                hdr.trackedDevicePose.bPoseIsValid ? "valid" : "missing", ago * 1000);
            loggedPose = true;
        }
        if (!poseOk) continue;
        vr::HmdMatrix44_t pr{};
        tc->GetCameraProjection(hmd, ci, type, 0.05f, 100.0f, &pr);
        XMFLOAT4X4 prt;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) prt.m[j][i] = pr.m[i][j];
        XMStoreFloat4x4(&back.toClip, XMMatrixInverse(nullptr, c2h * head) * XMLoadFloat4x4(&prt));
        if (!logged) {
            Log("Camera pictures arriving: %s %ux%u (%u bytes a pixel), camera %d in u %.2f+%.2f v %.2f+%.2f\n",
                kCamFrameName[type], fw, fh, bpp, ci, back.rect.x, back.rect.z, back.rect.y, back.rect.w);
            logged = true;
        }
        CamPublish(back);
        CamSetStatus("showing");
    }
    tc->ReleaseVideoStreamingService(h);
    Log("Camera streaming stopped.\n");
}

// Start or stop the camera thread (camera mode in a running session).
static void CamSetWanted(bool want) {
    if (want == g_camRun.load() || (want && !g_camAvailable)) return;
    if (want) {
        g_camRun = true;
        g_camThread = new std::thread(g_camFake ? CamFakeLoop : CamLoop);
    } else {
        g_camRun = false;
        if (g_camThread) { g_camThread->join(); delete g_camThread; g_camThread = nullptr; }
        std::lock_guard<std::mutex> l(g_camMx);
        g_camMid = CamPicture();
        g_camMidNew = false;
        g_camStatus = "off";
    }
}

// The newest picture, if it's newer than the one in `cur`.
static bool CamTakeLatest(CamPicture& cur) {
    std::lock_guard<std::mutex> l(g_camMx);
    if (!g_camMidNew) return false;
    std::swap(cur, g_camMid);
    g_camMidNew = false;
    return true;
}

// Must match cbuffer K in shaders.hlsl.
struct CBCam {
    XMFLOAT4X4 worldToView;             // the game's world (frame space) -> this eye's view space
    XMFLOAT4X4 viewToCamClip;           // this eye's view space -> camera clip
    XMFLOAT4 camRect;                   // the camera's part of the picture: u0, v0, du, dv
    XMFLOAT4 camInfo;                   // x = 1 camera mode, y = 1 linear target (decode sRGB), z = 1 alpha marks no data, w = 1 picture usable
};

// Where SteamVR's standing space (its OpenXR STAGE space) is in the game's frame space, this frame.
static XrSpace g_stageSpace = XR_NULL_HANDLE;   // the layer's own STAGE reference space (SteamVR: its standing space)
static XMFLOAT4X4 g_stageToWorld{};
static bool g_stageOk = false;

// The current picture as the renderers see it (render thread only).
static CamPicture g_camCur;

// cbuffer K for one view (eye pose `cam`, view -> world).
static CBCam MakeCBCam(XMMATRIX cam, bool linearTarget) {
    CBCam k{};
    XMStoreFloat4x4(&k.worldToView, XMMatrixTranspose(XMMatrixInverse(nullptr, cam)));
    bool usable = g_camCur.seq && (g_camCur.eyeRelative || g_stageOk);
    XMMATRIX toClip = XMLoadFloat4x4(&g_camCur.toClip);
    XMMATRIX m = g_camCur.eyeRelative ? toClip : cam * XMMatrixInverse(nullptr, XMLoadFloat4x4(&g_stageToWorld)) * toClip;
    XMStoreFloat4x4(&k.viewToCamClip, XMMatrixTranspose(m));
    k.camRect = g_camCur.rect;
    k.camInfo = { 1, linearTarget ? 1.0f : 0.0f, g_camCur.alphaMask ? 1.0f : 0.0f, usable ? 1.0f : 0.0f };
    return k;
}
