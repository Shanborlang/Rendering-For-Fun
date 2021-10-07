#pragma once

#include "shared/vkRenderers/VulkanRendererBase.h"

#include <string>
#include <cstdio>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

class VulkanQuadRenderer : public RendererBase {
    VulkanQuadRenderer(VulkanRenderDevice& vkDev, const std::vector<std::string>& textureFiles);
    virtual ~VulkanQuadRenderer();

    void UpdateBuffer(VulkanRenderDevice& vkDev, size_t t);
    void PushConstants(VkCommandBuffer commandBuffer, uint32_t textureIndex, const glm::vec2& offset);

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;

    void Quad(float x1, float y1, float x2, float y2);
    void Clear();

private:
    bool CreateDescriptorSet(VulkanRenderDevice& vkDev);

    struct ConstBuffer {
        vec2 offset;
        uint32_t textureIndex;
    };

    struct VertexData {
        vec3 pos;
        vec2 tc;
    };

    VulkanRenderDevice& vkDev;

    std::vector<VertexData> mQuads;

    size_t mVertexBufferSize;
    size_t mIndexBufferSize;

    // Storage Buffer with Index and Vertex Data
    std::vector<VkBuffer> mStorageBuffers;
    std::vector<VkDeviceMemory> mStorageBuffersMemory;

    std::vector<VulkanImage> mTextures;
    std::vector<VkSampler> mTextureSamplers;
};