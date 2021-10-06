#pragma once

#include "VulkanRendererBase.h"

#include <glm/glm.hpp>

class CubeRenderer : public RendererBase {
public:
    CubeRenderer(VulkanRenderDevice& vkDev, VulkanImage inDepthTexture, const char* textureFile);
    virtual ~CubeRenderer();

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;

    void UpdateUniformBuffer(VulkanRenderDevice& vkDev, uint32_t currentImage, const glm::mat4& m);

private:
    VkSampler mTextureSampler;
    VulkanImage mTexture;

    bool CreateDescriptorSet(VulkanRenderDevice& vkDev);
};