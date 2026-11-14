// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GmoLargey. Part of Alpha Views XR (GNU GPL version 3, see LICENSE.txt).
//
// Vulkan games (1.5.0): the same drawing as DrawMaskD3D in layer.cpp, with the same shaders
// (shaders.hlsl compiled to SPIR-V by dxc), recorded into our own command buffer and submitted on the
// game's queue just before it releases the swapchain image. Included by layer.cpp (one translation
// unit): it uses the layer's state (windows, handle instances, guide pixels, ...).
//
// What OpenXR promises (XR_KHR_vulkan_enable): at xrReleaseSwapchainImage the image is in
// COLOR_ATTACHMENT_OPTIMAL (depth images: DEPTH_STENCIL_ATTACHMENT_OPTIMAL) with the game's work on it
// already submitted to the queue named in the session's graphics binding, and the queue is
// externally synchronized around OpenXR calls, so submitting there from inside the release call
// lands in order after the game's drawing and before the runtime reads the image.
// The game's depth images get the SAMPLED usage bit added when they're created, so psBox can read
// them directly (moved to a read-only layout for the draw and back).

#define AVXR_VK_INSTANCE_FNS(X) \
    X(vkGetDeviceProcAddr) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceProperties)
#define AVXR_VK_DEVICE_FNS(X) \
    X(vkGetDeviceQueue) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) \
    X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkResetFences) X(vkQueueSubmit) X(vkCreateBuffer) X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) \
    X(vkUnmapMemory) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) \
    X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) \
    X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDraw) X(vkCmdDrawIndexed) \
    X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdBlitImage) \
    X(vkCmdClearColorImage)

#define AVXR_VK_DECL(f) PFN_##f f = nullptr;
struct VkFns {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    AVXR_VK_INSTANCE_FNS(AVXR_VK_DECL)
    AVXR_VK_DEVICE_FNS(AVXR_VK_DECL)
};
#undef AVXR_VK_DECL

#include "vsFull.spv.h"
#include "vsMesh.spv.h"
#include "vsSphere.spv.h"
#include "psConst.spv.h"
#include "psColor.spv.h"
#include "psBox.spv.h"
#include "vsPanel.spv.h"
#include "psPanel.spv.h"
#include "vsSphereD.spv.h"
#include "psHandD.spv.h"
#include "vsIcon.spv.h"
#include "psIcon.spv.h"

// Programs (vertex + pixel shader, vertex input) and blend modes, as in DrawMaskD3D.
enum VkProg { VP_FULL, VP_MESH_CONST, VP_MESH_BOX, VP_PANEL, VP_SPHERE, VP_SPHERE_D, VP_ICON, VP_COUNT };
enum VkBlend { VB_ALPHA_ONLY, VB_ALL, VB_BLEND };
enum VkShader { VS_FULL, VS_MESH, VS_SPHERE, PS_CONST, PS_COLOR, PS_BOX, VS_PANEL, PS_PANEL, VS_SPHERE_D, PS_HAND_D,
                VS_ICON, PS_ICON, SH_COUNT };

struct VkBuf { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; uint8_t* map = nullptr; VkDeviceSize size = 0; };
struct VkImg { VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; };

// One frame in flight: its own command buffer, fence, descriptor sets and a host-visible buffer for
// this draw's vertices (handles, window triangles, guide corners) and constants.
static const int kVkSlots = 4;
static const VkDeviceSize kVkInstBytes = VkDeviceSize(kMaxInst) * sizeof(Inst);
static const VkDeviceSize kVkWinBytes = VkDeviceSize(kMaxWinVerts) * sizeof(XMFLOAT3);
static const VkDeviceSize kVkPanelBytes = 256;
static const VkDeviceSize kVkUboBytes = 512 * 1024;
struct VkSlot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool pending = false;     // submitted, fence not waited for yet
    uint64_t serial = 0;      // which submission it last held
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkBuf buf;
    VkDeviceSize uboUsed = 0;
};

static struct VkState {
    bool ready = false;
    VkFns f;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkPhysicalDeviceMemoryProperties memProps{};
    VkDeviceSize uboAlign = 256;
    VkShaderModule shaders[SH_COUNT] = {};
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkBuf geo;                                  // sphere vertices + indices, handle quad (never changes)
    VkDeviceSize geoSphereIdx = 0, geoQuad = 0;
    VkSlot slots[kVkSlots];
    int nextSlot = 0;
    uint64_t serial = 0;
    VkImg guide;                                // the edit-mode guide panel, with mips
    VkImageView guideUnorm = VK_NULL_HANDLE, guideSrgb = VK_NULL_HANDLE;
    uint32_t guideMips = 1;
    bool guideEverUploaded = false;
    VkBuf guideStaging;
    int guideStagingSlot = -1;                  // the submission that last read the staging buffer
    uint64_t guideStagingSerial = 0;
    VkImg dummy;                                // 1x1 stand-in for a missing depth image / guide
    VkImageView dummyView = VK_NULL_HANDLE;
    bool dummyReady = false;
    std::map<uint64_t, VkRenderPass> passes;    // key: format << 8 | samples
    std::map<uint64_t, VkPipeline> pipes;       // key: render pass key << 16 | program << 4 | blend
    bool loggedFirstDraw = false;
} g_vk;

static bool VkActive() { return g_vk.ready; }

static uint32_t VkMemType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < g_vk.memProps.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (g_vk.memProps.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static bool VkMakeBuffer(VkBuf& b, VkDeviceSize size, VkBufferUsageFlags usage) {
    auto& f = g_vk.f;
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (f.vkCreateBuffer(g_vk.device, &bi, nullptr, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr; f.vkGetBufferMemoryRequirements(g_vk.device, b.buf, &mr);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = VkMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || f.vkAllocateMemory(g_vk.device, &ai, nullptr, &b.mem) != VK_SUCCESS) return false;
    if (f.vkBindBufferMemory(g_vk.device, b.buf, b.mem, 0) != VK_SUCCESS) return false;
    void* p = nullptr;
    if (f.vkMapMemory(g_vk.device, b.mem, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) return false;
    b.map = (uint8_t*)p; b.size = size;
    return true;
}
static void VkFreeBuffer(VkBuf& b) {
    auto& f = g_vk.f;
    if (b.map) f.vkUnmapMemory(g_vk.device, b.mem);
    if (b.buf) f.vkDestroyBuffer(g_vk.device, b.buf, nullptr);
    if (b.mem) f.vkFreeMemory(g_vk.device, b.mem, nullptr);
    b = VkBuf();
}
static bool VkMakeImage(VkImg& im, const VkImageCreateInfo& ci) {
    auto& f = g_vk.f;
    if (f.vkCreateImage(g_vk.device, &ci, nullptr, &im.img) != VK_SUCCESS) return false;
    VkMemoryRequirements mr; f.vkGetImageMemoryRequirements(g_vk.device, im.img, &mr);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = VkMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = VkMemType(mr.memoryTypeBits, 0);
    if (ai.memoryTypeIndex == UINT32_MAX || f.vkAllocateMemory(g_vk.device, &ai, nullptr, &im.mem) != VK_SUCCESS) return false;
    return f.vkBindImageMemory(g_vk.device, im.img, im.mem, 0) == VK_SUCCESS;
}
static void VkFreeImage(VkImg& im) {
    if (im.img) g_vk.f.vkDestroyImage(g_vk.device, im.img, nullptr);
    if (im.mem) g_vk.f.vkFreeMemory(g_vk.device, im.mem, nullptr);
    im = VkImg();
}
static VkImageView VkMakeView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t layer, uint32_t mips) {
    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    vi.subresourceRange = { aspect, 0, mips, layer, 1 };
    VkImageView v = VK_NULL_HANDLE;
    if (g_vk.f.vkCreateImageView(g_vk.device, &vi, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
    return v;
}

// Wait for every draw still on the GPU (before freeing anything they use).
static void VkWaitAll() {
    for (auto& s : g_vk.slots)
        if (s.pending && s.fence) { g_vk.f.vkWaitForFences(g_vk.device, 1, &s.fence, VK_TRUE, UINT64_MAX); s.pending = false; }
}

static void VkReleaseSwapchain(SwapchainInfo& si) {
    if (!g_vk.device) { si.vk = VkSwapchainData(); return; }
    for (auto& [k, fb] : si.vk.fbs) if (fb) g_vk.f.vkDestroyFramebuffer(g_vk.device, fb, nullptr);
    for (auto& [k, v] : si.vk.views) if (v) g_vk.f.vkDestroyImageView(g_vk.device, v, nullptr);
    si.vk = VkSwapchainData();
}

static void ReleaseVk() {
    if (!g_vk.device) { g_vk = VkState(); return; }
    auto& f = g_vk.f;
    VkDevice d = g_vk.device;
    VkWaitAll();
    for (auto& [sc, info] : g_swapchains) VkReleaseSwapchain(info);
    for (auto& [k, p] : g_vk.pipes) if (p) f.vkDestroyPipeline(d, p, nullptr);
    for (auto& [k, p] : g_vk.passes) if (p) f.vkDestroyRenderPass(d, p, nullptr);
    for (auto& s : g_vk.slots) {
        if (s.dpool) f.vkDestroyDescriptorPool(d, s.dpool, nullptr);
        if (s.fence) f.vkDestroyFence(d, s.fence, nullptr);
        if (s.pool) f.vkDestroyCommandPool(d, s.pool, nullptr);
        VkFreeBuffer(s.buf);
    }
    if (g_vk.guideUnorm) f.vkDestroyImageView(d, g_vk.guideUnorm, nullptr);
    if (g_vk.guideSrgb) f.vkDestroyImageView(d, g_vk.guideSrgb, nullptr);
    if (g_vk.dummyView) f.vkDestroyImageView(d, g_vk.dummyView, nullptr);
    VkFreeImage(g_vk.guide);
    VkFreeImage(g_vk.dummy);
    VkFreeBuffer(g_vk.guideStaging);
    VkFreeBuffer(g_vk.geo);
    if (g_vk.sampler) f.vkDestroySampler(d, g_vk.sampler, nullptr);
    if (g_vk.layout) f.vkDestroyPipelineLayout(d, g_vk.layout, nullptr);
    if (g_vk.dsl) f.vkDestroyDescriptorSetLayout(d, g_vk.dsl, nullptr);
    for (auto& m : g_vk.shaders) if (m) f.vkDestroyShaderModule(d, m, nullptr);
    g_vk = VkState();
    g_guideUploaded = false; g_guideText.clear(); g_guidePixels.clear();
}

static PFN_vkGetInstanceProcAddr VkLoader() {
#ifdef _WIN32
    HMODULE lib = GetModuleHandleW(L"vulkan-1.dll");
    if (!lib) lib = LoadLibraryW(L"vulkan-1.dll");
    return lib ? (PFN_vkGetInstanceProcAddr)(void*)GetProcAddress(lib, "vkGetInstanceProcAddr") : nullptr;
#else
    void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
    if (!lib) lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    return lib ? (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr") : nullptr;
#endif
}

static bool InitVk(const XrGraphicsBindingVulkanKHR* b) {
    g_vk = VkState();
    auto& f = g_vk.f;
    f.vkGetInstanceProcAddr = VkLoader();
    if (!f.vkGetInstanceProcAddr) { Log("Vulkan: no Vulkan loader found.\n"); return false; }
    g_vk.instance = b->instance; g_vk.phys = b->physicalDevice; g_vk.device = b->device; g_vk.queueFamily = b->queueFamilyIndex;
#define AVXR_VK_LOAD_I(fn) f.fn = (PFN_##fn)f.vkGetInstanceProcAddr(g_vk.instance, #fn); if (!f.fn) { Log("Vulkan: no %s\n", #fn); return false; }
    AVXR_VK_INSTANCE_FNS(AVXR_VK_LOAD_I)
#undef AVXR_VK_LOAD_I
#define AVXR_VK_LOAD_D(fn) f.fn = (PFN_##fn)f.vkGetDeviceProcAddr(g_vk.device, #fn); if (!f.fn) { Log("Vulkan: no %s\n", #fn); return false; }
    AVXR_VK_DEVICE_FNS(AVXR_VK_LOAD_D)
#undef AVXR_VK_LOAD_D
    VkDevice d = g_vk.device;
    f.vkGetDeviceQueue(d, b->queueFamilyIndex, b->queueIndex, &g_vk.queue);
    if (!g_vk.queue) { Log("Vulkan: the game's queue wasn't found.\n"); return false; }
    f.vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &g_vk.memProps);
    VkPhysicalDeviceProperties pp; f.vkGetPhysicalDeviceProperties(g_vk.phys, &pp);
    g_vk.uboAlign = std::max<VkDeviceSize>(16, pp.limits.minUniformBufferOffsetAlignment);
    Log("Vulkan device: %s (queue family %u, queue %u)\n", pp.deviceName, b->queueFamilyIndex, b->queueIndex);

    // Shaders.
    struct Code { const unsigned char* p; size_t n; };
    const Code code[SH_COUNT] = {
        { g_vsFull_spv, sizeof g_vsFull_spv }, { g_vsMesh_spv, sizeof g_vsMesh_spv }, { g_vsSphere_spv, sizeof g_vsSphere_spv },
        { g_psConst_spv, sizeof g_psConst_spv }, { g_psColor_spv, sizeof g_psColor_spv }, { g_psBox_spv, sizeof g_psBox_spv },
        { g_vsPanel_spv, sizeof g_vsPanel_spv }, { g_psPanel_spv, sizeof g_psPanel_spv }, { g_vsSphereD_spv, sizeof g_vsSphereD_spv },
        { g_psHandD_spv, sizeof g_psHandD_spv }, { g_vsIcon_spv, sizeof g_vsIcon_spv }, { g_psIcon_spv, sizeof g_psIcon_spv },
    };
    for (int i = 0; i < SH_COUNT; ++i) {
        std::vector<uint32_t> words((code[i].n + 3) / 4);
        memcpy(words.data(), code[i].p, code[i].n);
        VkShaderModuleCreateInfo si{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        si.codeSize = code[i].n; si.pCode = words.data();
        if (f.vkCreateShaderModule(d, &si, nullptr, &g_vk.shaders[i]) != VK_SUCCESS) { Log("Vulkan: shader %d failed\n", i); return false; }
    }

    // One descriptor set for everything: cbuffers C, B, W (dynamic offsets), the game's depth, the
    // guide picture and its sampler.
    {
        VkDescriptorSetLayoutBinding lb[6] = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
            { 10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 20, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
        };
        VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        li.bindingCount = 6; li.pBindings = lb;
        if (f.vkCreateDescriptorSetLayout(d, &li, nullptr, &g_vk.dsl) != VK_SUCCESS) return false;
        VkPipelineLayoutCreateInfo pi{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pi.setLayoutCount = 1; pi.pSetLayouts = &g_vk.dsl;
        if (f.vkCreatePipelineLayout(d, &pi, nullptr, &g_vk.layout) != VK_SUCCESS) return false;
        VkSamplerCreateInfo smp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        smp.magFilter = smp.minFilter = VK_FILTER_LINEAR; smp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        smp.addressModeU = smp.addressModeV = smp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        smp.maxLod = 16;
        if (f.vkCreateSampler(d, &smp, nullptr, &g_vk.sampler) != VK_SUCCESS) return false;
    }

    // Fixed geometry: low-poly unit sphere (as InitD3D) and the handle quad.
    {
        std::vector<float> sv; std::vector<uint16_t> si;
        MakeSphere(sv, si);
        static const float quad[12] = { -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1 };
        g_vk.geoSphereIdx = (sv.size() * 4 + 15) & ~VkDeviceSize(15);
        g_vk.geoQuad = (g_vk.geoSphereIdx + si.size() * 2 + 15) & ~VkDeviceSize(15);
        if (!VkMakeBuffer(g_vk.geo, g_vk.geoQuad + sizeof quad, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
        memcpy(g_vk.geo.map, sv.data(), sv.size() * 4);
        memcpy(g_vk.geo.map + g_vk.geoSphereIdx, si.data(), si.size() * 2);
        memcpy(g_vk.geo.map + g_vk.geoQuad, quad, sizeof quad);
        g_sphereIndexCount = (UINT)si.size();
    }

    // Frames in flight.
    for (auto& s : g_vk.slots) {
        VkCommandPoolCreateInfo ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; ci.queueFamilyIndex = b->queueFamilyIndex;
        if (f.vkCreateCommandPool(d, &ci, nullptr, &s.pool) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = s.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        if (f.vkAllocateCommandBuffers(d, &ai, &s.cmd) != VK_SUCCESS) return false;
        VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (f.vkCreateFence(d, &fi, nullptr, &s.fence) != VK_SUCCESS) return false;
        VkDescriptorPoolSize ps[3] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3 * 16 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2 * 16 },
                                       { VK_DESCRIPTOR_TYPE_SAMPLER, 16 } };
        VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpi.maxSets = 16; dpi.poolSizeCount = 3; dpi.pPoolSizes = ps;
        if (f.vkCreateDescriptorPool(d, &dpi, nullptr, &s.dpool) != VK_SUCCESS) return false;
        if (!VkMakeBuffer(s.buf, kVkInstBytes + kVkWinBytes + kVkPanelBytes + kVkUboBytes,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) return false;
    }

    // Guide panel picture: BGRA with mips, read as sRGB or plain to suit the game's image format.
    {
        g_vk.guideMips = 1;
        for (int s = std::max(kGuideW, kGuideH); s > 1; s >>= 1) ++g_vk.guideMips;
        VkImageCreateInfo ic{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ic.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_B8G8R8A8_UNORM;
        ic.extent = { (uint32_t)kGuideW, (uint32_t)kGuideH, 1 }; ic.mipLevels = g_vk.guideMips; ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool ok = VkMakeImage(g_vk.guide, ic) &&
                  VkMakeBuffer(g_vk.guideStaging, VkDeviceSize(kGuideW) * kGuideH * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (ok) {
            g_vk.guideUnorm = VkMakeView(g_vk.guide.img, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, g_vk.guideMips);
            g_vk.guideSrgb = VkMakeView(g_vk.guide.img, VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 0, g_vk.guideMips);
        }
        if (!ok || !g_vk.guideUnorm || !g_vk.guideSrgb) {
            Log("Edit-mode guide unavailable (Vulkan setup failed).\n");
            if (g_vk.guideUnorm) f.vkDestroyImageView(d, g_vk.guideUnorm, nullptr);
            if (g_vk.guideSrgb) f.vkDestroyImageView(d, g_vk.guideSrgb, nullptr);
            g_vk.guideUnorm = g_vk.guideSrgb = VK_NULL_HANDLE;
            VkFreeImage(g_vk.guide); VkFreeBuffer(g_vk.guideStaging);
        }
    }
    {
        VkImageCreateInfo ic{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_R8G8B8A8_UNORM;
        ic.extent = { 1, 1, 1 }; ic.mipLevels = 1; ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!VkMakeImage(g_vk.dummy, ic)) return false;
        g_vk.dummyView = VkMakeView(g_vk.dummy.img, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        if (!g_vk.dummyView) return false;
    }
    g_vk.ready = true;
    return true;
}

static VkRenderPass VkPass(VkFormat fmt, uint32_t samples) {
    uint64_t key = (uint64_t(fmt) << 8) | samples;
    auto it = g_vk.passes.find(key);
    if (it != g_vk.passes.end()) return it->second;
    VkAttachmentDescription ad{};
    ad.format = fmt; ad.samples = (VkSampleCountFlagBits)samples;
    ad.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad.initialLayout = ad.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ar{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ar;
    // After whatever the game (or the runtime's copy) last did to the image; before the runtime reads it.
    VkSubpassDependency dep[2] = {};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ri{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    ri.attachmentCount = 1; ri.pAttachments = &ad; ri.subpassCount = 1; ri.pSubpasses = &sp;
    ri.dependencyCount = 2; ri.pDependencies = dep;
    VkRenderPass rp = VK_NULL_HANDLE;
    if (g_vk.f.vkCreateRenderPass(g_vk.device, &ri, nullptr, &rp) != VK_SUCCESS) { Log("Vulkan: render pass for format %d failed\n", (int)fmt); rp = VK_NULL_HANDLE; }
    g_vk.passes[key] = rp;
    return rp;
}

static VkPipeline VkPipe(VkFormat fmt, uint32_t samples, VkProg prog, VkBlend blend) {
    uint64_t key = (((uint64_t(fmt) << 8) | samples) << 16) | (uint64_t(prog) << 4) | uint64_t(blend);
    auto it = g_vk.pipes.find(key);
    if (it != g_vk.pipes.end()) return it->second;
    VkRenderPass rp = VkPass(fmt, samples);
    VkPipeline pipe = VK_NULL_HANDLE;
    if (!rp) { g_vk.pipes[key] = pipe; return pipe; }

    static const VkShader kVs[VP_COUNT] = { VS_FULL, VS_MESH, VS_MESH, VS_PANEL, VS_SPHERE, VS_SPHERE_D, VS_ICON };
    static const VkShader kPs[VP_COUNT] = { PS_CONST, PS_CONST, PS_BOX, PS_PANEL, PS_COLOR, PS_HAND_D, PS_ICON };
    static const char* kVsName[VP_COUNT] = { "vsFull", "vsMesh", "vsMesh", "vsPanel", "vsSphere", "vsSphereD", "vsIcon" };
    static const char* kPsName[VP_COUNT] = { "psConst", "psConst", "psBox", "psPanel", "psColor", "psHandD", "psIcon" };
    VkPipelineShaderStageCreateInfo st[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = g_vk.shaders[kVs[prog]]; st[0].pName = kVsName[prog];
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = g_vk.shaders[kPs[prog]]; st[1].pName = kPsName[prog];

    std::vector<VkVertexInputBindingDescription> vb;
    std::vector<VkVertexInputAttributeDescription> va;
    const VkFormat f4 = VK_FORMAT_R32G32B32A32_SFLOAT;
    switch (prog) {
    case VP_FULL: break;
    case VP_MESH_CONST: case VP_MESH_BOX:
        vb = { { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX } };
        va = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 } };
        break;
    case VP_PANEL:
        vb = { { 0, 20, VK_VERTEX_INPUT_RATE_VERTEX } };
        va = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32_SFLOAT, 12 } };
        break;
    case VP_SPHERE: case VP_SPHERE_D:
        vb = { { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX }, { 1, sizeof(Inst), VK_VERTEX_INPUT_RATE_INSTANCE } };
        va = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 1, f4, 0 }, { 2, 1, f4, 16 } };
        break;
    case VP_ICON:
        vb = { { 0, 8, VK_VERTEX_INPUT_RATE_VERTEX }, { 1, sizeof(Inst), VK_VERTEX_INPUT_RATE_INSTANCE } };
        va = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 1, f4, 0 }, { 2, 1, f4, 16 }, { 3, 1, f4, 32 }, { 4, 1, f4, 48 }, { 5, 1, f4, 64 } };
        break;
    default: break;
    }
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = (uint32_t)vb.size(); vi.pVertexBindingDescriptions = vb.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)va.size(); vi.pVertexAttributeDescriptions = va.data();
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = prog == VP_PANEL ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = (VkSampleCountFlagBits)samples;
    VkPipelineColorBlendAttachmentState ba{};
    if (blend == VB_ALPHA_ONLY) ba.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
    else ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blend == VB_BLEND) {   // edit handles: soft edges; colour blends, alpha builds up to 1 (as g_bsBlend)
        ba.blendEnable = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
    pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pColorBlendState = &cb; pi.pDynamicState = &ds;
    pi.layout = g_vk.layout; pi.renderPass = rp; pi.subpass = 0;
    if (g_vk.f.vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe) != VK_SUCCESS) {
        Log("Vulkan: pipeline %d / %d for format %d failed\n", (int)prog, (int)blend, (int)fmt);
        pipe = VK_NULL_HANDLE;
    }
    g_vk.pipes[key] = pipe;
    return pipe;
}

static bool VkFormatHasStencil(VkFormat f) {
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}
// Formats whose writes are linear (sRGB-encoded or float): the guide picture is read as sRGB for them.
static bool VkLinearTarget(VkFormat f) {
    return f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_A8B8G8R8_SRGB_PACK32 ||
           f == VK_FORMAT_R16G16B16A16_SFLOAT || f == VK_FORMAT_B10G11R11_UFLOAT_PACK32 || f == VK_FORMAT_R32G32B32A32_SFLOAT;
}

static void VkBarrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, uint32_t mip0, uint32_t mips, uint32_t layer,
                      VkImageLayout from, VkImageLayout to, VkAccessFlags srcA, VkAccessFlags dstA,
                      VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = srcA; b.dstAccessMask = dstA; b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = { aspect, mip0, mips, layer, 1 };
    g_vk.f.vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// The guide panel's new picture: staging buffer -> mip 0, then each mip from the one above.
static void VkUploadGuide(VkSlot& s) {
    auto& f = g_vk.f;
    // The staging buffer may still be read by an earlier upload.
    if (g_vk.guideStagingSlot >= 0) {
        VkSlot& o = g_vk.slots[g_vk.guideStagingSlot];
        if (o.pending && o.serial == g_vk.guideStagingSerial) { f.vkWaitForFences(g_vk.device, 1, &o.fence, VK_TRUE, UINT64_MAX); o.pending = false; }
    }
    memcpy(g_vk.guideStaging.map, g_guidePixels.data(), size_t(kGuideW) * kGuideH * 4);
    VkCommandBuffer cmd = s.cmd;
    VkImage img = g_vk.guide.img;
    const VkImageAspectFlags C = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.oldLayout = g_vk.guideEverUploaded ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = { C, 0, g_vk.guideMips, 0, 1 };
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy bc{};
    bc.imageSubresource = { C, 0, 0, 1 };
    bc.imageExtent = { (uint32_t)kGuideW, (uint32_t)kGuideH, 1 };
    f.vkCmdCopyBufferToImage(cmd, g_vk.guideStaging.buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bc);
    int32_t w = kGuideW, h = kGuideH;
    for (uint32_t m = 1; m < g_vk.guideMips; ++m) {
        VkBarrier(cmd, img, C, m - 1, 1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        int32_t nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        VkImageBlit bl{};
        bl.srcSubresource = { C, m - 1, 0, 1 }; bl.srcOffsets[1] = { w, h, 1 };
        bl.dstSubresource = { C, m, 0, 1 }; bl.dstOffsets[1] = { nw, nh, 1 };
        f.vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
        w = nw; h = nh;
    }
    if (g_vk.guideMips > 1)
        VkBarrier(cmd, img, C, 0, g_vk.guideMips - 1, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkBarrier(cmd, img, C, g_vk.guideMips - 1, 1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    g_vk.guideEverUploaded = true;
    g_vk.guideStagingSlot = int(&s - g_vk.slots);
    g_vk.guideStagingSerial = g_vk.serial + 1;   // the serial this slot is about to be submitted with
}

// Image view + framebuffer for one slice of one of the game's colour images.
static VkFramebuffer VkFramebufferFor(SwapchainInfo& sc, uint32_t image, uint32_t slice, VkRenderPass rp) {
    uint64_t key = (uint64_t(image) << 32) | slice;
    auto it = sc.vk.fbs.find(key);
    if (it != sc.vk.fbs.end()) return it->second;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (image < sc.vk.images.size() && slice < sc.vk.layers) {
        VkImageView v = VkMakeView(sc.vk.images[image], (VkFormat)sc.format, VK_IMAGE_ASPECT_COLOR_BIT, slice, 1);
        sc.vk.views[key] = v;
        if (v) {
            VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fi.renderPass = rp; fi.attachmentCount = 1; fi.pAttachments = &v;
            fi.width = sc.vk.width; fi.height = sc.vk.height; fi.layers = 1;
            if (g_vk.f.vkCreateFramebuffer(g_vk.device, &fi, nullptr, &fb) != VK_SUCCESS) fb = VK_NULL_HANDLE;
        }
    }
    if (!fb) Log("Vulkan: no framebuffer for image %u slice %u (format %d)\n", image, slice, (int)sc.format);
    sc.vk.fbs[key] = fb;
    return fb;
}

// The game's depth for one view, if psBox / psHandD can read it: its image (to move it to a read-only
// layout and back) and a depth view of the slice.
struct VkDepthUse { VkImage img = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkImageAspectFlags aspects = 0; uint32_t slice = 0; };
static VkDepthUse VkGameDepth(const ViewDepth& vd) {
    VkDepthUse u;
    if (!vd.valid) return u;
    auto it = g_swapchains.find(vd.swapchain);
    if (it == g_swapchains.end() || !it->second.depth) return u;
    SwapchainInfo& sc = it->second;
    if (!sc.vk.sampled || sc.vk.failed || sc.lastAcquired >= sc.vk.images.size()) return u;
    VkFormat fmt = (VkFormat)sc.format;
    if (sc.vk.samples > 1 || vd.slice >= sc.vk.layers) {
        Log("Game depth can't be read (format %d, %u samples, slice %u of %u): boxes act as solid blocks.\n",
            (int)fmt, sc.vk.samples, vd.slice, sc.vk.layers);
        sc.vk.failed = true; return u;
    }
    uint64_t key = (uint64_t(sc.lastAcquired) << 32) | vd.slice;
    auto vit = sc.vk.views.find(key);
    VkImageView v;
    if (vit != sc.vk.views.end()) v = vit->second;
    else {
        v = VkMakeView(sc.vk.images[sc.lastAcquired], fmt, VK_IMAGE_ASPECT_DEPTH_BIT, vd.slice, 1);
        sc.vk.views[key] = v;
        if (!v) { Log("Vulkan: depth view failed (format %d): boxes act as solid blocks.\n", (int)fmt); sc.vk.failed = true; return u; }
        static bool logged = false;
        if (!logged) {
            Log("Reading game depth (Vulkan): %ux%u format %d, slice %u, depth %.2f..%.2f maps to %.3f..%.1f m\n",
                sc.vk.width, sc.vk.height, (int)fmt, vd.slice, vd.minDepth, vd.maxDepth, vd.nearZ, vd.farZ);
            logged = true;
        }
    }
    if (!v) return u;
    u.img = sc.vk.images[sc.lastAcquired]; u.view = v; u.slice = vd.slice;
    u.aspects = VK_IMAGE_ASPECT_DEPTH_BIT | (VkFormatHasStencil(fmt) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    return u;
}

static void DrawMaskVk(const std::vector<DrawView>& dviews) {
    auto& f = g_vk.f;
    VkSlot& s = g_vk.slots[g_vk.nextSlot];
    int slotIndex = g_vk.nextSlot;
    g_vk.nextSlot = (g_vk.nextSlot + 1) % kVkSlots;
    if (s.pending) { f.vkWaitForFences(g_vk.device, 1, &s.fence, VK_TRUE, UINT64_MAX); s.pending = false; }
    f.vkResetFences(g_vk.device, 1, &s.fence);
    f.vkResetCommandPool(g_vk.device, s.pool, 0);
    f.vkResetDescriptorPool(g_vk.device, s.dpool, 0);
    (void)slotIndex;

    // This draw's vertices: hand cutouts then handles, window triangles, guide corners.
    const VkDeviceSize offWin = kVkInstBytes, offPanel = kVkInstBytes + kVkWinBytes, offUbo = offPanel + kVkPanelBytes;
    UINT maskCount = (UINT)std::min<size_t>(g_maskInst.size(), kMaxInst);
    UINT uiCount = (UINT)std::min<size_t>(g_uiInst.size(), kMaxInst - maskCount);
    memcpy(s.buf.map, g_maskInst.data(), maskCount * sizeof(Inst));
    Inst* ui = (Inst*)s.buf.map + maskCount;
    memcpy(ui, g_uiInst.data(), uiCount * sizeof(Inst));
    DarkenKeyedHandles(ui, uiCount);
    UINT winVerts = (UINT)std::min<size_t>(g_winTris.size(), kMaxWinVerts);
    memcpy(s.buf.map + offWin, g_winTris.data(), winVerts * sizeof(XMFLOAT3));
    s.uboUsed = offUbo;
    auto pushUbo = [&](const void* data, size_t n) -> uint32_t {
        VkDeviceSize o = (s.uboUsed + g_vk.uboAlign - 1) & ~(g_vk.uboAlign - 1);
        if (o + n > s.buf.size) return UINT32_MAX;
        memcpy(s.buf.map + o, data, n);
        s.uboUsed = o + n;
        return (uint32_t)o;
    };

    bool drawGuide = g_guideShow && g_vk.guideUnorm && g_vk.guideSrgb;
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f.vkBeginCommandBuffer(s.cmd, &bi);
    if (!g_vk.dummyReady) {
        VkBarrier(s.cmd, g_vk.dummy.img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkClearColorValue cc{}; VkImageSubresourceRange rr{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        f.vkCmdClearColorImage(s.cmd, g_vk.dummy.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &rr);
        VkBarrier(s.cmd, g_vk.dummy.img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        g_vk.dummyReady = true;
    }
    if (drawGuide && g_guideDirty && g_guidePixels.size() == size_t(kGuideW) * kGuideH) {
        VkUploadGuide(s);
        g_guideDirty = false;
        g_guideUploaded = true;
    }
    drawGuide = drawGuide && g_guideUploaded;
    if (drawGuide) {
        static const float uv[4][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };
        float* v = (float*)(s.buf.map + offPanel);
        for (int k = 0; k < 4; ++k) {
            *v++ = g_guideCorner[k].x; *v++ = g_guideCorner[k].y; *v++ = g_guideCorner[k].z;
            *v++ = uv[k][0]; *v++ = uv[k][1];
        }
    }

    const bool keyed = UseKeyColour(), shown = CutoutsShown();
    bool any = false;
    for (const DrawView& pv : dviews) {
        auto it = g_swapchains.find(pv.swapchain);
        if (it == g_swapchains.end() || it->second.depth) continue;
        SwapchainInfo& sc = it->second;
        VkFormat fmt = (VkFormat)sc.format;
        uint32_t samples = std::max(1u, sc.vk.samples);
        VkRenderPass rp = VkPass(fmt, samples);
        if (!rp) continue;
        VkFramebuffer fb = VkFramebufferFor(sc, sc.lastAcquired, pv.slice, rp);
        if (!fb) continue;

        // The game's depth, if anything here reads it: boxes, or hands behind game objects.
        bool boxes = winVerts && shown && !g_boxDraws.empty();
        bool handsD = maskCount && shown && g_handOcclude;
        VkDepthUse depth;
        if (boxes || handsD) depth = VkGameDepth(pv.depth);
        const VkImageLayout kAtt = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kRead = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        if (depth.view)
            VkBarrier(s.cmd, depth.img, depth.aspects, 0, 1, depth.slice, kAtt, kRead, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = s.dpool; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_vk.dsl;
        if (f.vkAllocateDescriptorSets(g_vk.device, &ai, &set) != VK_SUCCESS) set = VK_NULL_HANDLE;
        if (set) {
            VkDescriptorBufferInfo dbi[3] = { { s.buf.buf, 0, sizeof(CB) }, { s.buf.buf, 0, sizeof(CBBox) }, { s.buf.buf, 0, sizeof(CBWin) } };
            VkDescriptorImageInfo dii[3] = {
                { VK_NULL_HANDLE, depth.view ? depth.view : g_vk.dummyView, depth.view ? kRead : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { VK_NULL_HANDLE, drawGuide ? (VkLinearTarget(fmt) ? g_vk.guideSrgb : g_vk.guideUnorm) : g_vk.dummyView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { g_vk.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED } };
            VkWriteDescriptorSet w[6] = {};
            const uint32_t bind[6] = { 0, 1, 2, 10, 11, 20 };
            for (int k = 0; k < 6; ++k) {
                w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[k].dstSet = set; w[k].dstBinding = bind[k]; w[k].descriptorCount = 1;
                if (k < 3) { w[k].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w[k].pBufferInfo = &dbi[k]; }
                else { w[k].descriptorType = k == 5 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[k].pImageInfo = &dii[k - 3]; }
            }
            f.vkUpdateDescriptorSets(g_vk.device, 6, w, 0, nullptr);
        }

        const XrRect2Di& r = pv.rect;
        VkRect2D area{ { std::max(0, r.offset.x), std::max(0, r.offset.y) }, { 0, 0 } };
        area.extent.width = (uint32_t)std::max(0, std::min<int32_t>(r.offset.x + r.extent.width, (int32_t)sc.vk.width) - area.offset.x);
        area.extent.height = (uint32_t)std::max(0, std::min<int32_t>(r.offset.y + r.extent.height, (int32_t)sc.vk.height) - area.offset.y);
        if (set && area.extent.width && area.extent.height) {
            any = true;
            VkRenderPassBeginInfo rb{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            rb.renderPass = rp; rb.framebuffer = fb; rb.renderArea = area;
            f.vkCmdBeginRenderPass(s.cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport vp{ (float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height, 0, 1 };
            f.vkCmdSetViewport(s.cmd, 0, 1, &vp);
            f.vkCmdSetScissor(s.cmd, 0, 1, &area);

            const XrPosef& p = pv.pose;
            XMMATRIX cam = XMMatrixAffineTransformation(XMVectorSet(1, 1, 1, 0), XMVectorZero(), Q(p.orientation),
                XMVectorSet(p.position.x, p.position.y, p.position.z, 1));
            // Vulkan can't turn depth clipping off (that needs a device feature the game chose): a very
            // near near plane and a far far plane instead (no depth test, so precision doesn't matter).
            const float n = 0.002f;
            XMMATRIX proj = XMMatrixPerspectiveOffCenterRH(tanf(pv.fov.angleLeft) * n, tanf(pv.fov.angleRight) * n,
                tanf(pv.fov.angleDown) * n, tanf(pv.fov.angleUp) * n, n, 100000.0f);
            XMMATRIX viewProj = XMMatrixInverse(nullptr, cam) * proj;
            CB cb{};
            XMStoreFloat4(&cb.camR, cam.r[0]);
            XMStoreFloat4(&cb.camU, cam.r[1]);
            uint32_t offB = 0, offW = 0;   // cbuffers B and W for the current draw (0: unused)
            VkPipeline bound = VK_NULL_HANDLE;
            auto use = [&](VkProg prog, VkBlend blend, uint32_t offC) -> bool {
                VkPipeline pipe = VkPipe(fmt, samples, prog, blend);
                if (!pipe || offC == UINT32_MAX || offB == UINT32_MAX || offW == UINT32_MAX) return false;
                if (pipe != bound) { f.vkCmdBindPipeline(s.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe); bound = pipe; }
                uint32_t dyn[3] = { offC, offB, offW };
                f.vkCmdBindDescriptorSets(s.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.layout, 0, 1, &set, 3, dyn);
                return true;
            };

            // 1. Alpha mode: force alpha = 1 everywhere (games often leave junk in alpha).
            if (!keyed) {
                XMStoreFloat4x4(&cb.mvp, XMMatrixIdentity());
                cb.col = { 0, 0, 0, 1 };
                if (use(VP_FULL, VB_ALPHA_ONLY, pushUbo(&cb, sizeof cb))) f.vkCmdDraw(s.cmd, 3, 1, 0, 0);
            }
            XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(viewProj));

            // 2. Windows: alpha 0 = passthrough (green tint when visualizing), or a solid key colour.
            if (winVerts && shown) {
                cb.col = keyed ? XMFLOAT4{ kKeyCol[g_keyMode][0], kKeyCol[g_keyMode][1], kKeyCol[g_keyMode][2], 1 }
                       : g_visualize ? XMFLOAT4{ 0, 1, 0, 0.5f } : XMFLOAT4{ 0, 0, 0, 0 };
                VkBlend bl = keyed || g_visualize ? VB_ALL : VB_ALPHA_ONLY;
                uint32_t offC = pushUbo(&cb, sizeof cb);
                VkDeviceSize vo = offWin;
                if (g_boxDraws.empty()) {   // flat windows only: cut their whole outline
                    if (use(VP_MESH_CONST, bl, offC)) {
                        f.vkCmdBindVertexBuffers(s.cmd, 0, 1, &s.buf.buf, &vo);
                        f.vkCmdDraw(s.cmd, g_flatVerts, 1, 0, 0);
                    }
                } else {
                    int ds = depth.view ? 1 : 0;
                    if (ds != g_depthRead)   // log changes only (reset each session)
                        Log(depth.view ? "Box windows: using the game's depth.\n"
                                       : "Box windows: no game depth this view, boxes act as solid blocks.\n");
                    g_depthRead = ds;
                    static CBBox bb;
                    static CBWin wb;
                    FillBoxCB(pv, cam, depth.view != VK_NULL_HANDLE, cb.col, bb, wb);
                    offB = pushUbo(&bb, sizeof bb);
                    offW = pushUbo(&wb, sizeof wb);
                    if (use(VP_MESH_BOX, bl, offC)) {
                        f.vkCmdBindVertexBuffers(s.cmd, 0, 1, &s.buf.buf, &vo);
                        f.vkCmdDraw(s.cmd, winVerts, 1, 0, 0);
                    }
                    offB = offW = 0;
                }
            }

            // 2b. Edit-mode guide panel: solid (alpha 1), over windows, under hands and handles.
            if (drawGuide && use(VP_PANEL, VB_ALL, pushUbo(&cb, sizeof cb))) {
                VkDeviceSize vo = offPanel;
                f.vkCmdBindVertexBuffers(s.cmd, 0, 1, &s.buf.buf, &vo);
                f.vkCmdDraw(s.cmd, 4, 1, 0, 0);
            }

            // 3 + 4. Hand cutouts, then visible edit handles on top.
            if (maskCount || uiCount) {
                uint32_t offC = pushUbo(&cb, sizeof cb);
                VkBuffer vbs[2] = { g_vk.geo.buf, s.buf.buf };
                VkDeviceSize vos[2] = { 0, 0 };
                if (maskCount && shown) {
                    VkBlend bl = keyed || g_visualize ? VB_ALL : VB_ALPHA_ONLY;
                    const ViewDepth& vd = pv.depth;
                    float range = vd.maxDepth - vd.minDepth;
                    bool occl = g_handOcclude && depth.view && fabsf(range) > 1e-6f;
                    VkProg prog = VP_SPHERE;
                    if (occl) {
                        auto inv = [](float z) { return (std::isfinite(z) && z > 0) ? 1.0f / z : 0.0f; };
                        CBBox hb{};
                        hb.rectC = { (float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height };
                        hb.rectD = { (float)vd.rect.offset.x, (float)vd.rect.offset.y, (float)vd.rect.extent.width, (float)vd.rect.extent.height };
                        hb.depthMap = { vd.minDepth, 1.0f / range, inv(vd.nearZ), inv(vd.farZ) };
                        hb.info = { 1, 0, 0, 0 };
                        offB = pushUbo(&hb, sizeof hb);
                        prog = VP_SPHERE_D;
                        if (g_handDepthUsed != 1) Log("Hands behind game objects: using the game's depth.\n");
                        g_handDepthUsed = 1;
                    } else {
                        if (g_handOcclude && g_handDepthUsed != 0) Log("Hands behind game objects: no game depth in this view, hands drawn in front.\n");
                        g_handDepthUsed = g_handOcclude ? 0 : -1;
                    }
                    if (use(prog, bl, offC)) {
                        f.vkCmdBindVertexBuffers(s.cmd, 0, 2, vbs, vos);
                        f.vkCmdBindIndexBuffer(s.cmd, g_vk.geo.buf, g_vk.geoSphereIdx, VK_INDEX_TYPE_UINT16);
                        f.vkCmdDrawIndexed(s.cmd, g_sphereIndexCount, maskCount, 0, 0, 0);
                    }
                    offB = 0;
                }
                if (uiCount && use(VP_ICON, VB_BLEND, offC)) {
                    VkBuffer ivbs[2] = { g_vk.geo.buf, s.buf.buf };
                    VkDeviceSize ivos[2] = { g_vk.geoQuad, 0 };
                    f.vkCmdBindVertexBuffers(s.cmd, 0, 2, ivbs, ivos);
                    f.vkCmdDraw(s.cmd, 6, uiCount, 0, maskCount);
                }
            }
            f.vkCmdEndRenderPass(s.cmd);
        }
        if (depth.view)
            VkBarrier(s.cmd, depth.img, depth.aspects, 0, 1, depth.slice, kRead, kAtt, VK_ACCESS_SHADER_READ_BIT,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    }
    f.vkEndCommandBuffer(s.cmd);
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &s.cmd;
    VkResult vr = f.vkQueueSubmit(g_vk.queue, 1, &si, s.fence);
    if (vr == VK_SUCCESS) {
        s.pending = true;
        s.serial = ++g_vk.serial;
        if (!g_vk.loggedFirstDraw && any) { Log("Vulkan: drawing cutouts on the game's queue.\n"); g_vk.loggedFirstDraw = true; }
    } else {
        static int fails = 0;
        if (fails++ < 3) Log("Vulkan: vkQueueSubmit failed %d\n", (int)vr);
    }
}
