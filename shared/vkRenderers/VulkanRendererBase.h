#pragma once

#include "shared/UtilsVulkan.h"

class RendererBase {
public:
    explicit RendererBase(const VulkanRenderDevice& vkDev, VulkanImage depthTexture)
    : mDevice(vkDev.device)
    , mFramebufferWidth(vkDev.framebufferWidth)
    , mFramebufferHeight(vkDev.framebufferHeight)
    , mDepthTexture(depthTexture)
    {}
    virtual ~RendererBase();
    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) = 0;

    inline VulkanImage GetDepthTexture() const { return mDepthTexture; }

protected:
    void BeginRenderPass(VkCommandBuffer commandBuffer, size_t currentImage);
    bool CreateUniformBuffers(VulkanRenderDevice& vkDev, size_t uniformDataSize);

    VkDevice mDevice = VK_NULL_HANDLE;

    uint32_t mFramebufferWidth = 0;
    uint32_t mFramebufferHeight = 0;

    // Depth buffer
    VulkanImage mDepthTexture;

    // Descriptor set(layout + pool + sets) -> uses uniform buffers, textures, framebuffers
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> mDescriptorSets;

    // Framebuffers (one for each command buffer)
    std::vector<VkFramebuffer> mSwapchainFramebuffers;

    // 4. Pipeline & render pass (using DescriptorSets & pipeline state options)
    VkRenderPass mRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mGraphicsPipeline = VK_NULL_HANDLE;

    // 5. Uniform buffer
    std::vector<VkBuffer> mUniformBuffers;
    std::vector<VkDeviceMemory> mUniformBuffersMemory;
};