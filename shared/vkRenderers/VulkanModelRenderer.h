#pragma once

#include "VulkanRendererBase.h"

class ModelRenderer : public RendererBase {
public:
    ModelRenderer(VulkanRenderDevice& vkDev, const char* modelFile, const char* textureFile, uint32_t uniformDataSize);
    ModelRenderer(VulkanRenderDevice& vkDev,
                  bool useDepth,
                  VkBuffer storageBuffer,
                  VkDeviceMemory storageBufferMemory,
                  uint32_t vertexBufferSize,
                  uint32_t indexBufferSize,
                  VulkanImage texture,
                  VkSampler textureSampler,
                  const std::vector<const char*>& shaderFiles,
                  uint32_t uniformDataSize,
                  bool useGeneralTextureLayout = true,
                  VulkanImage externalDepth = {.image = VK_NULL_HANDLE},
                  bool deleteMeshData = true);
    virtual ~ModelRenderer();

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;
    void UpdateUniformBuffer(VulkanRenderDevice& vkDev, uint32_t currentImage, const void* data, const size_t dataSize);

    // HACK to allow sharing textures between multiple ModelRenderers
    void FreeTextureSampler() { mTextureSampler = VK_NULL_HANDLE; }

private:
    bool mUseGeneralTextureLayout = false;
    bool mIsExternalDepth = false;
    bool mDeleteMeshData = true;

    size_t mVertexBufferSize;
    size_t mIndexBufferSize;

    // 6. Storage Buffer with index and vertex data
    VkBuffer mStorageBuffer;
    VkDeviceMemory mStorageBufferMemory;

    VkSampler mTextureSampler;
    VulkanImage mTexture;

    bool CreateDescriptorSet(VulkanRenderDevice& vkDev, uint32_t uniformDataSize);
};