#pragma once

#include "VulkanRendererBase.h"

class VulkanClear : public RendererBase{
public:
    VulkanClear(VulkanRenderDevice& vkDev, VulkanImage depthTexture);

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;

private:
    bool mShouldClearDepth;
};
