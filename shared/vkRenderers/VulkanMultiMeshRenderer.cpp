#include "VulkanMultiMeshRenderer.h"

void MultiMeshRenderer::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    BeginRenderPass(commandBuffer, currentImage);
    /* For CountKHR (Vulkan 1.1) we may use indirect rendering with GPU-based object counter */
    /// vkCmdDrawIndirectCountKHR(commandBuffer, indirectBuffers_[currentImage], 0, countBuffers_[currentImage], 0, maxShapes_, sizeof(VkDrawIndirectCommand));
    /* For Vulkan 1.0 vkCmdDrawIndirect is enough */
    vkCmdDrawIndirect(commandBuffer, mIndirectBuffers[currentImage], 0, mMaxShapes, sizeof(VkDrawIndirectCommand));

    vkCmdEndRenderPass(commandBuffer);
}

MultiMeshRenderer::MultiMeshRenderer(VulkanRenderDevice &vkDev, const char *meshFile, const char *drawDataFile,
                                     const char *materialFile, const char *vtxShaderFile, const char *fragShaderFile)
                                     : vkDev(vkDev), RendererBase(vkDev, VulkanImage()){

    if (!CreateColorAndDepthRenderPass(vkDev, false, &mRenderPass, RenderPassCreateInfo())) {
        printf("Failed to create render pass\n");
        exit(EXIT_FAILURE);
    }

    mFramebufferWidth = vkDev.framebufferWidth;
    mFramebufferHeight = vkDev.framebufferHeight;

    CreateDepthResources(vkDev, mFramebufferWidth, mFramebufferHeight, mDepthTexture);

    LoadDrawData(drawDataFile);

    MeshFileHeader header = loadMeshData(meshFile, mMeshData);

    const uint32_t indirectDataSize = mMaxShapes * sizeof(VkDrawIndirectCommand);
    mMaxDrawDataSize = mMaxShapes * sizeof(DrawData);
    mMaxMaterialSize = 1024;

    mCountBuffers.resize(vkDev.swapchainImages.size());
    mCountBuffersMemory.resize(vkDev.swapchainImages.size());

    mDrawDataBuffers.resize(vkDev.swapchainImages.size());
    mDrawDataBuffersMemory.resize(vkDev.swapchainImages.size());

    mIndirectBuffers.resize(vkDev.swapchainImages.size());
    mIndirectBuffersMemory.resize(vkDev.swapchainImages.size());

    if(!CreateBuffer(vkDev.device, vkDev.physicalDevice, mMaxMaterialSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     mMaterialBuffer, mMaterialBufferMemory)) {
        printf("Cannot create material buffer\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    mMaxVertexBufferSize = header.vertexDataSize;
    mIndexBufferSize = header.indexDataSize;

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(vkDev.physicalDevice, &devProps);
    const auto offsetAlignment = static_cast<uint32_t>(devProps.limits.minStorageBufferOffsetAlignment);
    if((mMaxVertexBufferSize & (offsetAlignment - 1)) != 0) {
        int floats = (offsetAlignment - (mMaxVertexBufferSize & (offsetAlignment - 1))) / sizeof(float);
        for(int ii = 0; ii < floats; ii++)
            mMeshData.mVertexData.push_back(0);
        mMaxVertexBufferSize = (mMaxVertexBufferSize + offsetAlignment) & ~(offsetAlignment - 1);
    }

    if (!CreateBuffer(vkDev.device, vkDev.physicalDevice, mMaxVertexBufferSize + mMaxIndexBufferSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      mStorageBuffer, mStorageBufferMemory))
    {
        printf("Cannot create vertex/index buffer\n"); fflush(stdout);
        exit(EXIT_FAILURE);
    }

    UpdateGeometryBuffers(vkDev, header.vertexDataSize, header.indexDataSize, mMeshData.mVertexData.data(), mMeshData.mIndexData.data());

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++)
    {
        if (!CreateBuffer(vkDev.device, vkDev.physicalDevice, indirectDataSize,
                          VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, // | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, /* for debugging we make it host-visible */
                          mIndirectBuffers[i], mIndirectBuffersMemory[i]))
        {
            printf("Cannot create indirect buffer\n"); fflush(stdout);
            exit(EXIT_FAILURE);
        }

        UpdateIndirectBuffers(vkDev, i);

        if (!CreateBuffer(vkDev.device, vkDev.physicalDevice, mMaxDrawDataSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, /* for debugging we make it host-visible */
                          mDrawDataBuffers[i], mDrawDataBuffersMemory[i]))
        {
            printf("Cannot create draw data buffer\n"); fflush(stdout);
            exit(EXIT_FAILURE);
        }

        UpdateDrawDataBuffer(vkDev, i, mMaxDrawDataSize, mShapes.data());

        if (!CreateBuffer(vkDev.device, vkDev.physicalDevice, sizeof(uint32_t),
                          VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, /* for debugging we make it host-visible */
                          mCountBuffers[i], mCountBuffersMemory[i]))
        {
            printf("Cannot create count buffer\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }

        UpdateCountBuffer(vkDev, i, mMaxShapes);
    }

    if (!CreateUniformBuffers(vkDev, sizeof(mat4)) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, VK_NULL_HANDLE, mSwapchainFramebuffers) ||
        !CreateDescriptorPool(vkDev, 1, 4, 0, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, { vtxShaderFile, fragShaderFile }, &mGraphicsPipeline))
    {
        printf("Failed to create pipeline\n"); fflush(stdout);
        exit(EXIT_FAILURE);
    }
}

void MultiMeshRenderer::UpdateIndirectBuffers(VulkanRenderDevice &vkDev, size_t currentImage, bool *visibility) {
    VkDrawIndirectCommand* data = nullptr;
    vkMapMemory(vkDev.device, mIndirectBuffersMemory[currentImage], 0, 2 * sizeof(VkDrawIndirectCommand), 0, (void **)&data);

    for(uint32_t i = 0; i < mMaxShapes; i++) {
        const uint32_t j = mShapes[i].meshIndex;
        const uint32_t lod = mShapes[i].LOD;
        data[i] = {
                .vertexCount = mMeshData.mMeshes[j].GetLODIndicesCount(lod),
                .instanceCount = visibility ? (visibility[i] ? 1u : 0u) : 1u,
                .firstVertex = 0,
                .firstInstance = i
        };
    }
    vkUnmapMemory(vkDev.device, mIndirectBuffersMemory[currentImage]);
}

void MultiMeshRenderer::UpdateGeometryBuffers(VulkanRenderDevice &vkDev, uint32_t vertexCount, uint32_t indexCount,
                                              const void *vertices, const void *indices) {
    UploadBufferData(vkDev, mStorageBufferMemory, 0, vertices, vertexCount);
    UploadBufferData(vkDev, mStorageBufferMemory, mMaxVertexBufferSize, indices, indexCount);
}

void
MultiMeshRenderer::UpdateMaterialBuffer(VulkanRenderDevice &vkDev, uint32_t materialSize, const void *materialData) {

}

void MultiMeshRenderer::UpdateUniformBuffer(VulkanRenderDevice &vkDev, size_t currentImage, const mat4 &m) {
    UploadBufferData(vkDev, mUniformBuffersMemory[currentImage], 0, glm::value_ptr(m), sizeof(mat4));
}

void MultiMeshRenderer::UpdateDrawDataBuffer(VulkanRenderDevice &vkDev, size_t currentImage, uint32_t drawDataSize,
                                             const void *drawData) {
    UploadBufferData(vkDev, mDrawDataBuffersMemory[currentImage], 0, drawData, drawDataSize);
}

void MultiMeshRenderer::UpdateCountBuffer(VulkanRenderDevice &vkDev, size_t currentImage, uint32_t itemCount) {
    UploadBufferData(vkDev, mCountBuffersMemory[currentImage], 0, &itemCount, sizeof(uint32_t));
}

MultiMeshRenderer::~MultiMeshRenderer() {
    VkDevice device = vkDev.device;

    vkDestroyBuffer(device, mStorageBuffer, nullptr);
    vkFreeMemory(device, mStorageBufferMemory, nullptr);

    for (size_t i = 0; i < mSwapchainFramebuffers.size(); i++)
    {
        vkDestroyBuffer(device, mDrawDataBuffers[i], nullptr);
        vkFreeMemory(device, mDrawDataBuffersMemory[i], nullptr);

        vkDestroyBuffer(device, mCountBuffers[i], nullptr);
        vkFreeMemory(device, mCountBuffersMemory[i], nullptr);

        vkDestroyBuffer(device, mIndirectBuffers[i], nullptr);
        vkFreeMemory(device, mIndirectBuffersMemory[i], nullptr);
    }

    vkDestroyBuffer(device, mMaterialBuffer, nullptr);
    vkFreeMemory(device, mMaterialBufferMemory, nullptr);

    DestroyVulkanImage(device, mDepthTexture);
}

bool MultiMeshRenderer::CreateDescriptorSet(VulkanRenderDevice &vkDev) {
    const std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            /* vertices [part of this.storageBuffer] */
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            /* indices [part of this.storageBuffer] */
            DescriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            /* draw data [this.drawDataBuffer] */
            DescriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            /* material data [this.materialBuffer] */
            DescriptorSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
    };

    VK_CHECK(vkCreateDescriptorSetLayout(vkDev.device, &layoutInfo, nullptr, &mDescriptorSetLayout));

    std::vector<VkDescriptorSetLayout> layouts(vkDev.swapchainImages.size(), mDescriptorSetLayout);

    const VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = mDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(vkDev.swapchainImages.size()),
            .pSetLayouts = layouts.data()
    };

    mDescriptorSets.resize(vkDev.swapchainImages.size());

    VK_CHECK(vkAllocateDescriptorSets(vkDev.device, &allocInfo, mDescriptorSets.data()));

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++)
    {
        VkDescriptorSet ds = mDescriptorSets[i];

        const VkDescriptorBufferInfo bufferInfo  = { mUniformBuffers[i], 0, sizeof(mat4) };
        const VkDescriptorBufferInfo bufferInfo2 = { mStorageBuffer, 0, mMaxVertexBufferSize };
        const VkDescriptorBufferInfo bufferInfo3 = { mStorageBuffer, mMaxVertexBufferSize, mMaxIndexBufferSize };
        const VkDescriptorBufferInfo bufferInfo4 = { mDrawDataBuffers[i], 0, mMaxDrawDataSize };
        const VkDescriptorBufferInfo bufferInfo5 = { mMaterialBuffer, 0, mMaxMaterialSize };

        const std::array<VkWriteDescriptorSet, 5> descriptorWrites = {
                BufferWriteDescriptorSet(ds, &bufferInfo,  0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo3, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo4, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo5, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
//			imageWriteDescriptorSet( ds, &imageInfo,   3)
        };

        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    return true;
}

void MultiMeshRenderer::LoadDrawData(const char *drawDataFile) {
    FILE* f = fopen(drawDataFile, "rb");

    if(!f) {
        printf("Unable to open draw data file. Run MeshConvert first\n");
        exit(255);
    }

    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    mMaxShapes = static_cast<uint32_t>(fsize / sizeof(DrawData));

    printf("Reading draw data items: %d\n", (int)mMaxShapes); fflush(stdout);

    mShapes.resize(mMaxShapes);

    if(fread(mShapes.data(), sizeof(DrawData), mMaxShapes, f) != mMaxShapes) {
        printf("Unable to read draw data\n");
        exit(255);
    }

    fclose(f);

}
