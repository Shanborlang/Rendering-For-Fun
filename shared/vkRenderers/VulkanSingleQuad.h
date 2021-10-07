#pragma once

#include "shared/vkRenderers/VulkanRendererBase.h"

class VulkanSingleQuadRenderer : public RendererBase{
public:
    VulkanSingleQuadRenderer(VulkanRenderDevice& vkDev, VulkanImage tex, VkSampler sampler, VkImageLayout desiredLayout = VK_IMAGE_LAYOUT_GENERAL);
    virtual ~VulkanSingleQuadRenderer();

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;

private:
    VulkanRenderDevice& vkDev;

    bool CreateDescriptorSet(VulkanRenderDevice& vkDev, VkImageLayout desiredLayout = VK_IMAGE_LAYOUT_GENERAL);

    VulkanImage texture;
    VkSampler textureSampler;
};

