#pragma once

#include "shared/vkRenderers/VulkanRendererBase.h"

#include <glm/glm.hpp>
#include <glm/ext.hpp>
using glm::mat4;

#include "shared/scene/VtxData.h"

class MultiMeshRenderer : public RendererBase {
public:
    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;

    MultiMeshRenderer(
            VulkanRenderDevice& vkDev,
            const char* meshFile,
            const char* drawDataFile,
            const char* materialFile,
            const char* vtxShaderFile,
            const char* fragShaderFile
            );

    void UpdateIndirectBuffers(VulkanRenderDevice& vkDev, size_t currentImage, bool* visibility = nullptr);

    void UpdateGeometryBuffers(VulkanRenderDevice& vkDev, uint32_t vertexCount, uint32_t indexCount, const void* vertices, const void* indices);
    void UpdateMaterialBuffer(VulkanRenderDevice& vkDev, uint32_t materialSize, const void* materialData);

    void UpdateUniformBuffer(VulkanRenderDevice& vkDev, size_t currentImage, const mat4& m);
    void UpdateDrawDataBuffer(VulkanRenderDevice& vkDev, size_t currentImage, uint32_t drawDataSize, const void* drawData);
    void UpdateCountBuffer(VulkanRenderDevice& vkDev, size_t currentImage, uint32_t itemCount);

    virtual ~MultiMeshRenderer();

    uint32_t mVertexBufferSize;
    uint32_t mIndexBufferSize;

private:
    VulkanRenderDevice& vkDev;

    uint32_t mMaxVertexBufferSize;
    uint32_t mMaxIndexBufferSize;

    uint32_t mMaxShapes;

    uint32_t mMaxDrawDataSize;
    uint32_t mMaxMaterialSize;

    // Storage Buffer with index and vertex data
    VkBuffer mStorageBuffer;
    VkDeviceMemory mStorageBufferMemory;

    VkBuffer mMaterialBuffer;
    VkDeviceMemory mMaterialBufferMemory;

    std::vector<VkBuffer> mIndirectBuffers;
    std::vector<VkDeviceMemory> mIndirectBuffersMemory;

    std::vector<VkBuffer> mDrawDataBuffers;
    std::vector<VkDeviceMemory> mDrawDataBuffersMemory;

    // Buffer for draw count
    std::vector<VkBuffer> mCountBuffers;
    std::vector<VkDeviceMemory> mCountBuffersMemory;

    /* DrawData loaded from file. Converted to indirectBuffers[] and uploaded to drawDataBuffers[] */
    std::vector<DrawData> mShapes;
    MeshData mMeshData;

    bool CreateDescriptorSet(VulkanRenderDevice& vkDev);

    void LoadDrawData(const char* drawDataFile);
};