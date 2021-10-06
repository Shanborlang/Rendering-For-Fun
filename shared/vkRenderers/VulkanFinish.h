#pragma once

#include "VulkanRendererBase.h"

class VulkanFinish : public RendererBase{
public:
    VulkanFinish(VulkanRenderDevice& vkDev, VulkanImage depthTexture);

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;
};


