#include "VulkanModelRenderer.h"
#include "../EasyProfilerWrapper.h"

static constexpr VkClearColorValue clearValueColor = {1.f, 1.f, 1.f, 1.f};

bool ModelRenderer::CreateDescriptorSet(VulkanRenderDevice &vkDev, uint32_t uniformDataSize) {
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
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

    const VkDescriptorSetAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = mDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(vkDev.swapchainImages.size()),
            .pSetLayouts = layouts.data()
    };

    mDescriptorSets.resize(vkDev.swapchainImages.size());

    VK_CHECK(vkAllocateDescriptorSets(vkDev.device, &allocateInfo, mDescriptorSets.data()));

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++)
    {
        VkDescriptorSet ds = mDescriptorSets[i];

        const VkDescriptorBufferInfo bufferInfo  = { mUniformBuffers[i], 0, uniformDataSize };
        const VkDescriptorBufferInfo bufferInfo2 = { mStorageBuffer, 0, mVertexBufferSize };
        const VkDescriptorBufferInfo bufferInfo3 = { mStorageBuffer, mVertexBufferSize, mIndexBufferSize };
        const VkDescriptorImageInfo  imageInfo   = { mTextureSampler, mTexture.imageView, mUseGeneralTextureLayout ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        const std::array<VkWriteDescriptorSet, 4> descriptorWrites = {
                BufferWriteDescriptorSet(ds, &bufferInfo,  0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo3, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                ImageWriteDescriptorSet( ds, &imageInfo, 3)
        };

        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    return true;
}

void ModelRenderer::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    EASY_FUNCTION()

    BeginRenderPass(commandBuffer, currentImage);

    vkCmdDraw(commandBuffer, static_cast<uint32_t>(mIndexBufferSize/(sizeof(unsigned int))), 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void ModelRenderer::UpdateUniformBuffer(VulkanRenderDevice &vkDev, uint32_t currentImage, const void *data,
                                        const size_t dataSize) {
    UploadBufferData(vkDev, mUniformBuffersMemory[currentImage], 0, data, dataSize);
}

ModelRenderer::ModelRenderer(VulkanRenderDevice &vkDev, const char *modelFile, const char *textureFile,
                             uint32_t uniformDataSize)
                             : RendererBase(vkDev, VulkanImage()){
    // Resource loading part
    if(!CreateTexturedVertexBuffer(vkDev, modelFile, &mStorageBuffer, &mStorageBufferMemory, &mVertexBufferSize, &mIndexBufferSize)) {
        std::fprintf(stderr, "ModelRenderer: CreateTexturedVertexBuffer() failed\n");
        exit(EXIT_FAILURE);
    }

    CreateTextureImage(vkDev, textureFile, mTexture.image, mTexture.imageMemory);
    CreateImageView(vkDev.device, mTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &mTexture.imageView);
    CreateTextureSampler(vkDev.device, &mTextureSampler);

    if (!CreateDepthResources(vkDev, vkDev.framebufferWidth, vkDev.framebufferHeight, mDepthTexture) ||
        !CreateColorAndDepthRenderPass(vkDev, true, &mRenderPass, RenderPassCreateInfo()) ||
        !CreateUniformBuffers(vkDev, uniformDataSize) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, mDepthTexture.imageView, mSwapchainFramebuffers) ||
        !CreateDescriptorPool(vkDev, 1, 2, 1, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev, uniformDataSize) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, {"../../../data/shaders/VK02.vert", "../../../data/shaders/VK02.frag", "../../../data/shaders/VK02.geom" }, &mGraphicsPipeline))
    {
        std::fprintf(stderr, "ModelRenderer: failed to create pipeline\n");
        exit(EXIT_FAILURE);
    }
}

ModelRenderer::ModelRenderer(VulkanRenderDevice &vkDev, bool useDepth, VkBuffer storageBuffer,
                             VkDeviceMemory storageBufferMemory, uint32_t vertexBufferSize, uint32_t indexBufferSize,
                             VulkanImage texture, VkSampler textureSampler,
                             const std::vector<const char *> &shaderFiles, uint32_t uniformDataSize,
                             bool useGeneralTextureLayout, VulkanImage externalDepth, bool deleteMeshData)
                             : mUseGeneralTextureLayout(useGeneralTextureLayout)
                             , mVertexBufferSize(vertexBufferSize)
                             , mIndexBufferSize(indexBufferSize)
                             , mStorageBuffer(storageBuffer)
                             , mStorageBufferMemory(storageBufferMemory)
                             , mTexture(texture)
                             , mTextureSampler(textureSampler)
                             , mDeleteMeshData(deleteMeshData)
                             , RendererBase(vkDev, VulkanImage()){
    if(useDepth) {
        mIsExternalDepth = (externalDepth.image != VK_NULL_HANDLE);

        if(mIsExternalDepth)
            mDepthTexture = externalDepth;
        else
            CreateDepthResources(vkDev, vkDev.framebufferWidth, vkDev.framebufferHeight, mDepthTexture);
    }

    if (!CreateColorAndDepthRenderPass(vkDev, useDepth, &mRenderPass, RenderPassCreateInfo()) ||
        !CreateUniformBuffers(vkDev, uniformDataSize) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, mDepthTexture.imageView, mSwapchainFramebuffers) ||
        !CreateDescriptorPool(vkDev, 1, 2, 1, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev, uniformDataSize) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, shaderFiles, &mGraphicsPipeline))
    {
        std::fprintf(stderr, "ModelRenderer: failed to create pipeline\n");
        exit(EXIT_FAILURE);
    }
}

ModelRenderer::~ModelRenderer() {
    if(mDeleteMeshData) {
        vkDestroyBuffer(mDevice, mStorageBuffer, nullptr);
        vkFreeMemory(mDevice, mStorageBufferMemory, nullptr);
    }

    if(mTextureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, mTextureSampler, nullptr);
        DestroyVulkanImage(mDevice, mTexture);
    }

    if(!mIsExternalDepth)
        DestroyVulkanImage(mDevice, mDepthTexture);
}
