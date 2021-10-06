#pragma once

#include <imgui/imgui.h>
#include "VulkanRendererBase.h"

class ImGuiRenderer : public RendererBase {
public:
    explicit ImGuiRenderer(VulkanRenderDevice& vkDev);
    explicit ImGuiRenderer(VulkanRenderDevice& vkDev, const std::vector<VulkanTexture>& textures);
    virtual ~ImGuiRenderer();

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;
    void UpdateBuffers(VulkanRenderDevice& vkDev, uint32_t currentImage, const ImDrawData* imguiDrawData);

private:
    const ImDrawData* mDrawData = nullptr;

    bool CreateDescriptorSet(VulkanRenderDevice& vkDev);

    /* Descriptor set with multiple textures(for offscreen buffer display etc.) */
    bool CreateMultiDescriptorSet(VulkanRenderDevice& vkDev);

    std::vector<VulkanTexture> mExtTextures;

    // storage buffer with index and vertex data
    VkDeviceSize mBufferSize;
    std::vector<VkBuffer> mStorageBuffer;
    std::vector<VkDeviceMemory> mStorageBufferMemory;

    VkSampler mFontSampler;
    VulkanImage mFont;

};

