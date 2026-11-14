// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GmoLargey. Part of Alpha Views XR (GNU GPL version 3, see LICENSE.txt).
//
// Test harness (not shipped): a pretend OpenXR runtime and game around the layer, to check its drawing
// without a headset. It loads the layer library, creates a session (Vulkan, or D3D11 on Windows), a
// stereo colour swapchain (2 array slices) and a depth swapchain, "renders" each frame (clears colour,
// and depth to a flat wall 2 m away), runs the layer's hooks like a game would (locate views, release,
// end frame) and saves the last frame's left eye as a PNG: RGB, and alpha (passthrough) as grey.
//
//   xr_harness <layer library> <vulkan|d3d11> <out.png> [frames] [edit]
//
// "edit" presses Ctrl+Alt+E (Windows) after a few frames, for the guide panel and handles.
// The windows come from %LOCALAPPDATA%\AlphaViewsXR\windows_AVXR_Harness.cfg (written by the test script).
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#define XR_USE_GRAPHICS_API_D3D11
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool g_vulkanMode = true;
static const uint32_t W = 960, H = 960;
static const float kNear = 0.1f, kFar = 100.0f, kWall = 2.0f;
static int g_errors = 0;

// ---------------------------------------------------------------- Vulkan "game"
static VkInstance g_inst;
static VkPhysicalDevice g_phys;
static VkDevice g_dev;
static VkQueue g_queue;
static uint32_t g_family = 0;
static VkCommandPool g_pool;

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "VALIDATION %s: %s\n\n", sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "ERROR" : "warning", d->pMessage);
        if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++g_errors;
    }
    return VK_FALSE;
}

static uint32_t MemType(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0;
}
static bool InitVulkanGame() {
    VkApplicationInfo ai{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.apiVersion = VK_API_VERSION_1_1;
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char* exts[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    uint32_t nl = 0; vkEnumerateInstanceLayerProperties(&nl, nullptr);
    std::vector<VkLayerProperties> lp(nl); vkEnumerateInstanceLayerProperties(&nl, lp.data());
    bool haveVal = false;
    for (auto& l : lp) if (!strcmp(l.layerName, layers[0])) haveVal = true;
    printf("Validation layer: %s\n", haveVal ? "on" : "NOT FOUND");
    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &ai;
    ci.enabledLayerCount = haveVal ? 1 : 0; ci.ppEnabledLayerNames = layers;
    ci.enabledExtensionCount = haveVal ? 1 : 0; ci.ppEnabledExtensionNames = exts;
    if (vkCreateInstance(&ci, nullptr, &g_inst) != VK_SUCCESS) { printf("vkCreateInstance failed\n"); return false; }
    if (haveVal) {
        auto mk = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_inst, "vkCreateDebugUtilsMessengerEXT");
        VkDebugUtilsMessengerCreateInfoEXT mi{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = DebugCb;
        VkDebugUtilsMessengerEXT m; if (mk) mk(g_inst, &mi, nullptr, &m);
    }
    uint32_t n = 0; vkEnumeratePhysicalDevices(g_inst, &n, nullptr);
    std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(g_inst, &n, pds.data());
    if (!n) { printf("No Vulkan device\n"); return false; }
    g_phys = pds[0];
    for (auto pd : pds) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd, &p); if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { g_phys = pd; break; } }
    VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(g_phys, &pp);
    printf("Vulkan device: %s\n", pp.deviceName);
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qp(nq); vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &nq, qp.data());
    for (uint32_t i = 0; i < nq; ++i) if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_family = i; break; }
    float prio = 1;
    VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qi.queueFamilyIndex = g_family; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo di{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    if (vkCreateDevice(g_phys, &di, nullptr, &g_dev) != VK_SUCCESS) { printf("vkCreateDevice failed\n"); return false; }
    vkGetDeviceQueue(g_dev, g_family, 0, &g_queue);
    VkCommandPoolCreateInfo pi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = g_family;
    vkCreateCommandPool(g_dev, &pi, nullptr, &g_pool);
    return true;
}
static VkCommandBuffer BeginCmd() {
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = g_pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer c; vkAllocateCommandBuffers(g_dev, &ai, &c);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(c, &bi);
    return c;
}
static void SubmitCmd(VkCommandBuffer c, bool wait) {
    vkEndCommandBuffer(c);
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &c;
    vkQueueSubmit(g_queue, 1, &si, VK_NULL_HANDLE);
    if (wait) vkQueueWaitIdle(g_queue);
}
static void Barrier(VkCommandBuffer c, VkImage img, VkImageAspectFlags asp, uint32_t layers, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = { asp, 0, 1, 0, layers };
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// ---------------------------------------------------------------- pretend runtime
struct FakeSc {
    bool depth = false; int64_t format = 0; uint32_t layers = 1, next = 0, acquired = 0;
    std::vector<VkImage> vk; std::vector<VkDeviceMemory> mem;
    bool everUsed[3] = {};
#ifdef _WIN32
    std::vector<ID3D11Texture2D*> d3d;
#endif
};
static std::vector<FakeSc*> g_sc;
#ifdef _WIN32
static ID3D11Device* g_d3d = nullptr;
static ID3D11DeviceContext* g_d3dCtx = nullptr;
#endif
static uint64_t g_handle = 100;

static XrResult XRAPI_CALL F_Success() { return XR_SUCCESS; }
static XrResult XRAPI_CALL F_CreateHandle(uint64_t, const void*, uint64_t* out) { *out = ++g_handle; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_StringToPath(XrInstance, const char*, XrPath* p) { *p = ++g_handle; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_PathToString(XrInstance, XrPath, uint32_t cap, uint32_t* n, char* b) { *n = 2; if (cap >= 2) strcpy(b, "x"); return XR_SUCCESS; }
static XrResult XRAPI_CALL F_GetActionStateFloat(XrSession, const XrActionStateGetInfo*, XrActionStateFloat* s) { s->currentState = 0; s->isActive = XR_FALSE; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_GetActionStateBoolean(XrSession, const XrActionStateGetInfo*, XrActionStateBoolean* s) { s->currentState = XR_FALSE; s->isActive = XR_FALSE; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_GetActionStateVector2f(XrSession, const XrActionStateGetInfo*, XrActionStateVector2f* s) { s->currentState = { 0, 0 }; s->isActive = XR_FALSE; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_LocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* l) { l->locationFlags = 0; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_GetCurrentInteractionProfile(XrSession, XrPath, XrInteractionProfileState* s) { s->interactionProfile = XR_NULL_PATH; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* p) {
    strcpy(p->systemName, "Harness HMD");
    for (auto* n = (XrBaseOutStructure*)p->next; n; n = n->next)
        if (n->type == XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT) ((XrSystemHandTrackingPropertiesEXT*)n)->supportsHandTracking = XR_TRUE;
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_GetInstanceProperties(XrInstance, XrInstanceProperties* p) { strcpy(p->runtimeName, "AVXR Harness Runtime"); p->runtimeVersion = 1; return XR_SUCCESS; }
static XrResult XRAPI_CALL F_EnumerateBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t cap, uint32_t* n, XrEnvironmentBlendMode* m) {
    *n = 2; if (cap >= 2) { m[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; m[1] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND; } return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* s) { *s = (XrSession)(uintptr_t)++g_handle; return XR_SUCCESS; }

static XrResult XRAPI_CALL F_CreateSwapchain(XrSession, const XrSwapchainCreateInfo* ci, XrSwapchain* out) {
    FakeSc* s = new FakeSc;
    s->depth = (ci->usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    s->format = ci->format; s->layers = ci->arraySize;
    for (int i = 0; i < 3; ++i) {
        if (g_vulkanMode) {
            VkImageCreateInfo ic{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            ic.imageType = VK_IMAGE_TYPE_2D; ic.format = (VkFormat)ci->format; ic.extent = { ci->width, ci->height, 1 };
            ic.mipLevels = 1; ic.arrayLayers = ci->arraySize; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
            ic.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (ci->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) ic.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (ci->usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ic.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (ci->usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) ic.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            VkImage img; if (vkCreateImage(g_dev, &ic, nullptr, &img) != VK_SUCCESS) return XR_ERROR_RUNTIME_FAILURE;
            VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, img, &mr);
            VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            ai.allocationSize = mr.size; ai.memoryTypeIndex = MemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkDeviceMemory m; vkAllocateMemory(g_dev, &ai, nullptr, &m); vkBindImageMemory(g_dev, img, m, 0);
            s->vk.push_back(img); s->mem.push_back(m);
        }
#ifdef _WIN32
        else {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = ci->width; td.Height = ci->height; td.MipLevels = 1; td.ArraySize = ci->arraySize;
            td.Format = (DXGI_FORMAT)ci->format; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = s->depth ? D3D11_BIND_DEPTH_STENCIL : (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            ID3D11Texture2D* t = nullptr;
            if (FAILED(g_d3d->CreateTexture2D(&td, nullptr, &t))) return XR_ERROR_RUNTIME_FAILURE;
            s->d3d.push_back(t);
        }
#endif
    }
    g_sc.push_back(s);
    *out = (XrSwapchain)s;
    printf("Runtime: swapchain %s format %lld usage 0x%llx\n", s->depth ? "depth" : "colour", (long long)ci->format, (unsigned long long)ci->usageFlags);
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_DestroySwapchain(XrSwapchain) { return XR_SUCCESS; }
static XrResult XRAPI_CALL F_EnumerateSwapchainImages(XrSwapchain sc, uint32_t cap, uint32_t* n, XrSwapchainImageBaseHeader* imgs) {
    FakeSc* s = (FakeSc*)sc;
    *n = 3;
    if (!cap) return XR_SUCCESS;
    for (uint32_t i = 0; i < 3 && i < cap; ++i) {
        if (g_vulkanMode) ((XrSwapchainImageVulkanKHR*)imgs)[i].image = s->vk[i];
#ifdef _WIN32
        else ((XrSwapchainImageD3D11KHR*)imgs)[i].texture = s->d3d[i];
#endif
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_AcquireSwapchainImage(XrSwapchain sc, const XrSwapchainImageAcquireInfo*, uint32_t* i) {
    FakeSc* s = (FakeSc*)sc; s->acquired = s->next; s->next = (s->next + 1) % 3; *i = s->acquired; return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) { return XR_SUCCESS; }
static float g_eyeX[2] = { -0.032f, 0.032f };
static XrResult XRAPI_CALL F_LocateViews(XrSession, const XrViewLocateInfo*, XrViewState* vs, uint32_t cap, uint32_t* n, XrView* v) {
    *n = 2;
    vs->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for (uint32_t i = 0; i < 2 && i < cap; ++i) {
        v[i].pose.orientation = { 0, 0, 0, 1 }; v[i].pose.position = { g_eyeX[i], 0, 0 };
        v[i].fov = { -0.785f, 0.785f, 0.785f, -0.785f };
        v[i].fov.angleLeft = -0.785f; v[i].fov.angleRight = 0.785f; v[i].fov.angleUp = 0.785f; v[i].fov.angleDown = -0.785f;
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_EndFrame(XrSession, const XrFrameEndInfo* fi) {
    static int logged = 0;
    if (logged++ < 1) printf("Runtime: xrEndFrame with %u layer(s), blend mode %d, flags 0x%llx\n", fi->layerCount, (int)fi->environmentBlendMode,
                             fi->layerCount ? (unsigned long long)((const XrCompositionLayerProjection*)fi->layers[0])->layerFlags : 0ull);
    return XR_SUCCESS;
}

// Pretend hand tracking (XR_EXT_hand_tracking): two loosely closed hands in front, below eye level.
static XrResult XRAPI_CALL F_CreateHandTracker(XrSession, const XrHandTrackerCreateInfoEXT* ci, XrHandTrackerEXT* t) {
    *t = (XrHandTrackerEXT)(uintptr_t)(ci->hand == XR_HAND_LEFT_EXT ? 501 : 502); return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_LocateHandJoints(XrHandTrackerEXT t, const XrHandJointsLocateInfoEXT*, XrHandJointLocationsEXT* l) {
    float hx = (uintptr_t)t == 501 ? -0.14f : 0.14f;
    l->isActive = XR_TRUE;
    for (uint32_t k = 0; k < l->jointCount; ++k) {
        XrHandJointLocationEXT& j = l->jointLocations[k];
        j.locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                          XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        j.pose.orientation = { 0, 0, 0, 1 };
        j.pose.position = { hx + 0.035f * cosf(k * 0.7f), -0.12f + 0.025f * sinf(k * 0.7f), -0.45f + 0.01f * sinf(k * 1.3f) };
        j.radius = 0.009f;
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_GetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* fn);
static XrResult XRAPI_CALL F_CreateApiLayerInstance(const XrInstanceCreateInfo* ci, const XrApiLayerCreateInfo*, XrInstance* inst) {
    for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i) {
        const char* e = ci->enabledExtensionNames[i];
        if (strcmp(e, XR_EXT_HAND_TRACKING_EXTENSION_NAME) && (strstr(e, "hand") || strstr(e, "eye"))) return XR_ERROR_EXTENSION_NOT_PRESENT;   // hand joints only
    }
    *inst = (XrInstance)(uintptr_t)1;
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL F_GetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* fn) {
    struct E { const char* n; PFN_xrVoidFunction f; };
#define FN(x) (PFN_xrVoidFunction)(x)
    static const E t[] = {
        { "xrDestroyInstance", FN(F_Success) }, { "xrCreateSession", FN(F_CreateSession) }, { "xrDestroySession", FN(F_Success) },
        { "xrCreateSwapchain", FN(F_CreateSwapchain) }, { "xrDestroySwapchain", FN(F_DestroySwapchain) },
        { "xrEnumerateSwapchainImages", FN(F_EnumerateSwapchainImages) }, { "xrAcquireSwapchainImage", FN(F_AcquireSwapchainImage) },
        { "xrReleaseSwapchainImage", FN(F_ReleaseSwapchainImage) }, { "xrLocateViews", FN(F_LocateViews) }, { "xrEndFrame", FN(F_EndFrame) },
        { "xrEnumerateEnvironmentBlendModes", FN(F_EnumerateBlendModes) }, { "xrGetSystemProperties", FN(F_GetSystemProperties) },
        { "xrStringToPath", FN(F_StringToPath) }, { "xrCreateActionSet", FN(F_CreateHandle) }, { "xrCreateAction", FN(F_CreateHandle) },
        { "xrSuggestInteractionProfileBindings", FN(F_Success) }, { "xrAttachSessionActionSets", FN(F_Success) },
        { "xrSyncActions", FN(F_Success) }, { "xrGetActionStateFloat", FN(F_GetActionStateFloat) },
        { "xrCreateActionSpace", FN(F_CreateHandle) }, { "xrLocateSpace", FN(F_LocateSpace) }, { "xrDestroySpace", FN(F_Success) },
        { "xrDestroyActionSet", FN(F_Success) }, { "xrPathToString", FN(F_PathToString) },
        { "xrGetActionStateBoolean", FN(F_GetActionStateBoolean) }, { "xrGetActionStateVector2f", FN(F_GetActionStateVector2f) },
        { "xrGetCurrentInteractionProfile", FN(F_GetCurrentInteractionProfile) }, { "xrGetInstanceProperties", FN(F_GetInstanceProperties) },
        { "xrGetInstanceProcAddr", FN(F_GetInstanceProcAddr) }, { "xrCreateHandTrackerEXT", FN(F_CreateHandTracker) },
        { "xrDestroyHandTrackerEXT", FN(F_Success) }, { "xrLocateHandJointsEXT", FN(F_LocateHandJoints) },
    };
#undef FN
    for (auto& e : t) if (!strcmp(e.n, name)) { *fn = e.f; return XR_SUCCESS; }
    *fn = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

// ---------------------------------------------------------------- the "game"
static float DepthFor(float dist) { return (1 / kNear - 1 / dist) / (1 / kNear - 1 / kFar); }

static void RenderGameFrame(FakeSc* col, FakeSc* dep) {
    if (g_vulkanMode) {
        VkCommandBuffer c = BeginCmd();
        VkImage ci = col->vk[col->acquired], di = dep->vk[dep->acquired];
        Barrier(c, ci, VK_IMAGE_ASPECT_COLOR_BIT, 2, col->everUsed[col->acquired] ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue cc{}; cc.float32[0] = 0.25f; cc.float32[1] = 0.35f; cc.float32[2] = 0.6f; cc.float32[3] = 0.3f;
        VkImageSubresourceRange cr{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2 };
        vkCmdClearColorImage(c, ci, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &cr);
        Barrier(c, ci, VK_IMAGE_ASPECT_COLOR_BIT, 2, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Barrier(c, di, VK_IMAGE_ASPECT_DEPTH_BIT, 2, dep->everUsed[dep->acquired] ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearDepthStencilValue dv{ DepthFor(kWall), 0 };
        VkImageSubresourceRange dr{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 2 };
        vkCmdClearDepthStencilImage(c, di, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &dv, 1, &dr);
        Barrier(c, di, VK_IMAGE_ASPECT_DEPTH_BIT, 2, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        SubmitCmd(c, false);   // no wait: the layer's drawing must queue up after it
        col->everUsed[col->acquired] = dep->everUsed[dep->acquired] = true;
    }
#ifdef _WIN32
    else {
        for (uint32_t s = 0; s < 2; ++s) {
            D3D11_RENDER_TARGET_VIEW_DESC rd{}; rd.Format = (DXGI_FORMAT)col->format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rd.Texture2DArray.FirstArraySlice = s; rd.Texture2DArray.ArraySize = 1;
            ID3D11RenderTargetView* rtv; g_d3d->CreateRenderTargetView(col->d3d[col->acquired], &rd, &rtv);
            float cc[4] = { 0.25f, 0.35f, 0.6f, 0.3f };
            g_d3dCtx->ClearRenderTargetView(rtv, cc); rtv->Release();
            D3D11_DEPTH_STENCIL_VIEW_DESC dd{}; dd.Format = DXGI_FORMAT_D32_FLOAT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            dd.Texture2DArray.FirstArraySlice = s; dd.Texture2DArray.ArraySize = 1;
            ID3D11DepthStencilView* dsv; g_d3d->CreateDepthStencilView(dep->d3d[dep->acquired], &dd, &dsv);
            g_d3dCtx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, DepthFor(kWall), 0); dsv->Release();
        }
    }
#endif
}

// Left eye (slice 0) of the image last drawn: RGBA8.
static std::vector<uint8_t> ReadBack(FakeSc* col, uint32_t img) {
    std::vector<uint8_t> px(size_t(W) * H * 4);
    if (g_vulkanMode) {
        vkQueueWaitIdle(g_queue);
        VkBuffer buf; VkDeviceMemory mem;
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; bi.size = px.size(); bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(g_dev, &bi, nullptr, &buf);
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_dev, buf, &mr);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; ai.allocationSize = mr.size;
        ai.memoryTypeIndex = MemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(g_dev, &ai, nullptr, &mem); vkBindBufferMemory(g_dev, buf, mem, 0);
        VkCommandBuffer c = BeginCmd();
        Barrier(c, col->vk[img], VK_IMAGE_ASPECT_COLOR_BIT, 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy r{}; r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; r.imageExtent = { W, H, 1 };
        vkCmdCopyImageToBuffer(c, col->vk[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &r);
        Barrier(c, col->vk[img], VK_IMAGE_ASPECT_COLOR_BIT, 2, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        SubmitCmd(c, true);
        void* p; vkMapMemory(g_dev, mem, 0, VK_WHOLE_SIZE, 0, &p); memcpy(px.data(), p, px.size()); vkUnmapMemory(g_dev, mem);
    }
#ifdef _WIN32
    else {
        D3D11_TEXTURE2D_DESC td; col->d3d[img]->GetDesc(&td);
        td.ArraySize = 1; td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st; g_d3d->CreateTexture2D(&td, nullptr, &st);
        g_d3dCtx->CopySubresourceRegion(st, 0, 0, 0, 0, col->d3d[img], 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m; g_d3dCtx->Map(st, 0, D3D11_MAP_READ, 0, &m);
        for (uint32_t y = 0; y < H; ++y) memcpy(&px[size_t(y) * W * 4], (uint8_t*)m.pData + y * m.RowPitch, W * 4);
        g_d3dCtx->Unmap(st, 0); st->Release();
    }
#endif
    return px;
}

int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: xr_harness <layer library> <vulkan|d3d11> <out.png> [frames] [edit]\n"); return 2; }
    g_vulkanMode = strcmp(argv[2], "d3d11") != 0;
    int frames = argc > 4 ? atoi(argv[4]) : 12;
    bool edit = argc > 5 && !strcmp(argv[5], "edit");
#ifndef _WIN32
    if (edit) setenv("ALPHA_VIEWS_EDIT", "4", 1);   // no Ctrl+Alt+E on Linux
#endif
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(argv[1]);
    auto negotiate = lib ? (PFN_xrNegotiateLoaderApiLayerInterface)(void*)GetProcAddress(lib, "xrNegotiateLoaderApiLayerInterface") : nullptr;
#else
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) printf("%s\n", dlerror());
    auto negotiate = lib ? (PFN_xrNegotiateLoaderApiLayerInterface)dlsym(lib, "xrNegotiateLoaderApiLayerInterface") : nullptr;
#endif
    if (!negotiate) { printf("Layer not loaded / no xrNegotiateLoaderApiLayerInterface\n"); return 1; }
    XrNegotiateLoaderInfo li{ XR_LOADER_INTERFACE_STRUCT_LOADER_INFO, XR_LOADER_INFO_STRUCT_VERSION, sizeof(XrNegotiateLoaderInfo),
                              1, XR_CURRENT_LOADER_API_LAYER_VERSION, XR_MAKE_VERSION(1, 0, 0), XR_CURRENT_API_VERSION };
    XrNegotiateApiLayerRequest req{ XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST, XR_API_LAYER_INFO_STRUCT_VERSION, sizeof(XrNegotiateApiLayerRequest) };
    if (XR_FAILED(negotiate(&li, "XR_APILAYER_GMO_alpha_views_xr", &req))) { printf("Negotiation failed\n"); return 1; }

    if (g_vulkanMode) { if (!InitVulkanGame()) return 1; }
#ifdef _WIN32
    else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_d3d, nullptr, &g_d3dCtx))) {
            printf("D3D11 device failed\n"); return 1;
        }
    }
#else
    else { printf("d3d11 is Windows only\n"); return 2; }
#endif

    XrApiLayerNextInfo next{ XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO, XR_API_LAYER_NEXT_INFO_STRUCT_VERSION, sizeof(XrApiLayerNextInfo) };
    strcpy(next.layerName, "XR_APILAYER_GMO_alpha_views_xr");
    next.nextGetInstanceProcAddr = F_GetInstanceProcAddr;
    next.nextCreateApiLayerInstance = F_CreateApiLayerInstance;
    XrApiLayerCreateInfo lci{ XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO, XR_API_LAYER_CREATE_INFO_STRUCT_VERSION, sizeof(XrApiLayerCreateInfo) };
    lci.nextInfo = &next;
    XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
    strcpy(ici.applicationInfo.applicationName, "AVXR_Harness");
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    XrInstance inst = XR_NULL_HANDLE;
    if (XR_FAILED(req.createApiLayerInstance(&ici, &lci, &inst))) { printf("Layer instance failed\n"); return 1; }
    auto gpa = req.getInstanceProcAddr;
#define GETX(name, var) PFN_##name var = nullptr; gpa(inst, #name, (PFN_xrVoidFunction*)&var); if (!var) { printf("no %s\n", #name); return 1; }
    GETX(xrCreateSession, xCreateSession) GETX(xrCreateSwapchain, xCreateSwapchain) GETX(xrEnumerateSwapchainImages, xEnum)
    GETX(xrAcquireSwapchainImage, xAcquire) GETX(xrReleaseSwapchainImage, xRelease) GETX(xrLocateViews, xLocate)
    GETX(xrEndFrame, xEndFrame) GETX(xrDestroySession, xDestroySession) GETX(xrDestroyInstance, xDestroyInstance)
    GETX(xrDestroySwapchain, xDestroySwapchain)
#undef GETX

    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    XrGraphicsBindingVulkanKHR vb{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
#ifdef _WIN32
    XrGraphicsBindingD3D11KHR db{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
#endif
    if (g_vulkanMode) { vb.instance = g_inst; vb.physicalDevice = g_phys; vb.device = g_dev; vb.queueFamilyIndex = g_family; vb.queueIndex = 0; sci.next = &vb; }
#ifdef _WIN32
    else { db.device = g_d3d; sci.next = &db; }
#endif
    XrSession session;
    if (XR_FAILED(xCreateSession(inst, &sci, &session))) { printf("Session failed\n"); return 1; }

    XrSwapchainCreateInfo cc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    cc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
#ifdef _WIN32
    cc.format = g_vulkanMode ? VK_FORMAT_R8G8B8A8_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
#else
    cc.format = VK_FORMAT_R8G8B8A8_SRGB;
#endif
    cc.sampleCount = 1; cc.width = W; cc.height = H; cc.faceCount = 1; cc.arraySize = 2; cc.mipCount = 1;
    XrSwapchain colSc, depSc;
    if (XR_FAILED(xCreateSwapchain(session, &cc, &colSc))) { printf("Colour swapchain failed\n"); return 1; }
    XrSwapchainCreateInfo dc = cc;
    dc.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
#ifdef _WIN32
    dc.format = g_vulkanMode ? VK_FORMAT_D32_SFLOAT : DXGI_FORMAT_D32_FLOAT;
#else
    dc.format = VK_FORMAT_D32_SFLOAT;
#endif
    if (XR_FAILED(xCreateSwapchain(session, &dc, &depSc))) { printf("Depth swapchain failed\n"); return 1; }
    // The fake swapchain objects (the layer hands our own handles back, as a runtime's would be).
    FakeSc* col = g_sc[0];
    FakeSc* dep = g_sc[1];
    uint32_t n = 0; xEnum(colSc, 0, &n, nullptr);
    XrSpace space = (XrSpace)(uintptr_t)7777;
    uint32_t lastImg = 0;
    for (int f = 0; f < frames; ++f) {
#ifdef _WIN32
        if (edit && f == 3) {   // Ctrl+Alt+E
            INPUT in[6] = {};
            WORD keys[3] = { VK_CONTROL, VK_MENU, 'E' };
            for (int k = 0; k < 3; ++k) { in[k].type = INPUT_KEYBOARD; in[k].ki.wVk = keys[k]; }
            for (int k = 0; k < 3; ++k) { in[3 + k].type = INPUT_KEYBOARD; in[3 + k].ki.wVk = keys[2 - k]; in[3 + k].ki.dwFlags = KEYEVENTF_KEYUP; }
            SendInput(6, in, sizeof(INPUT));
        }
        if (edit) Sleep(30);   // let the hotkey thread see it; and time pass for the guide to settle
#else
        if (edit) usleep(30000);   // edit mode by ALPHA_VIEWS_EDIT (set in main); time for the guide to settle
#endif
        XrTime t = 1000000000LL + XrTime(f) * 11111111LL;
        XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; vli.displayTime = t; vli.space = space;
        XrViewState vs{ XR_TYPE_VIEW_STATE };
        XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
        uint32_t vc = 0;
        xLocate(session, &vli, &vs, 2, &vc, views);
        uint32_t ci = 0, di = 0;
        xAcquire(colSc, nullptr, &ci); xAcquire(depSc, nullptr, &di);
        RenderGameFrame(col, dep);
        xRelease(depSc, nullptr);
        xRelease(colSc, nullptr);
        lastImg = ci;
        XrCompositionLayerDepthInfoKHR depthInfo[2];
        XrCompositionLayerProjectionView pv[2];
        for (int e = 0; e < 2; ++e) {
            depthInfo[e] = { XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR };
            depthInfo[e].subImage.swapchain = depSc; depthInfo[e].subImage.imageRect = { { 0, 0 }, { (int32_t)W, (int32_t)H } };
            depthInfo[e].subImage.imageArrayIndex = e;
            depthInfo[e].minDepth = 0; depthInfo[e].maxDepth = 1; depthInfo[e].nearZ = kNear; depthInfo[e].farZ = kFar;
            pv[e] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            if (!getenv("HARNESS_NODEPTH")) pv[e].next = &depthInfo[e];
            pv[e].pose = views[e].pose; pv[e].fov = views[e].fov;
            pv[e].subImage.swapchain = colSc; pv[e].subImage.imageRect = { { 0, 0 }, { (int32_t)W, (int32_t)H } };
            pv[e].subImage.imageArrayIndex = e;
        }
        XrCompositionLayerProjection proj{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        proj.space = space; proj.viewCount = 2; proj.views = pv;
        const XrCompositionLayerBaseHeader* layers[1] = { (const XrCompositionLayerBaseHeader*)&proj };
        XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
        fei.displayTime = t; fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; fei.layerCount = 1; fei.layers = layers;
        xEndFrame(session, &fei);
    }
    std::vector<uint8_t> px = ReadBack(col, lastImg);
    // Save: left half RGB as drawn, right half the alpha (white = game, black = passthrough).
    std::vector<uint8_t> out(size_t(W) * 2 * H * 3);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const uint8_t* p = &px[(size_t(y) * W + x) * 4];
            uint8_t* a = &out[(size_t(y) * W * 2 + x) * 3];
            uint8_t* b = &out[(size_t(y) * W * 2 + W + x) * 3];
            a[0] = p[0]; a[1] = p[1]; a[2] = p[2];
            b[0] = b[1] = b[2] = p[3];
        }
    stbi_write_png(argv[3], W * 2, H, 3, out.data(), W * 2 * 3);
    // Also the raw RGBA, for comparing D3D11 and Vulkan.
    std::string raw = std::string(argv[3]) + ".rgba";
    if (FILE* f = fopen(raw.c_str(), "wb")) { fwrite(px.data(), 1, px.size(), f); fclose(f); }
    xDestroySwapchain(colSc); xDestroySwapchain(depSc);
    xDestroySession(session);
    xDestroyInstance(inst);
    if (g_vulkanMode) vkDeviceWaitIdle(g_dev);
    printf("Saved %s; validation errors: %d\n", argv[3], g_errors);
    return g_errors ? 3 : 0;
}
