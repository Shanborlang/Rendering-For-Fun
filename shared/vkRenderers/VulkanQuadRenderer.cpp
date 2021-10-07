#include "VulkanQuadRenderer.h"

static constexpr int MAX_QUADS = 256;

VulkanQuadRenderer::VulkanQuadRenderer(VulkanRenderDevice &vkDev, const std::vector<std::string> &textureFiles)
: vkDev(vkDev), RendererBase(vkDev, VulkanImage()){
    const size_t imgCount = vkDev.swapchainImages.size();

    mFramebufferWidth = vkDev.framebufferWidth;
    mFramebufferHeight = vkDev.framebufferHeight;

    mStorageBuffers.resize(imgCount);
    mStorageBuffersMemory.resize(imgCount);

    mVertexBufferSize = MAX_QUADS * 6 * sizeof(VertexData);

    for(size_t i = 0; i < imgCount; i++) {
        if (!CreateBuffer(vkDev.device, vkDev.physicalDevice, mVertexBufferSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          mStorageBuffers[i], mStorageBuffersMemory[i]))
        {
            printf("Cannot create vertex buffer\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
    }

    if (!CreateUniformBuffers(vkDev, sizeof(ConstBuffer)))
    {
        printf("Cannot create data buffers\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    const size_t numTextureFiles = textureFiles.size();

    mTextures.resize(numTextureFiles);
    mTextureSamplers.resize(numTextureFiles);
    for (size_t i = 0; i < numTextureFiles; i++)
    {
        printf("\rLoading texture %u...", unsigned(i));
        CreateTextureImage(vkDev, textureFiles[i].c_str(), mTextures[i].image, mTextures[i].imageMemory);
        CreateImageView(vkDev.device, mTextures[i].image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &mTextures[i].imageView);
        CreateTextureSampler(vkDev.device, &mTextureSamplers[i]);
    }
    printf("\n");

    if (!CreateDepthResources(vkDev, vkDev.framebufferWidth, vkDev.framebufferHeight, mDepthTexture) ||
        !CreateDescriptorPool(vkDev, 1, 1, 1 * static_cast<uint32_t>(mTextures.size()), &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev) ||
        !CreateColorAndDepthRenderPass(vkDev, false, &mRenderPass, RenderPassCreateInfo()) ||
        !CreatePipelineLayoutWithConstants(vkDev.device, mDescriptorSetLayout, &mPipelineLayout, sizeof(ConstBuffer), 0) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, { "../../../data/shaders/texture_array.vert", "../../../data/shaders/texture_array.frag" }, &mGraphicsPipeline))
    {
        printf("Failed to create pipeline\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    CreateColorAndDepthFramebuffers(vkDev, mRenderPass, VK_NULL_HANDLE/*depthTexture.imageView*/, mSwapchainFramebuffers);
}

VulkanQuadRenderer::~VulkanQuadRenderer() {
    VkDevice device = vkDev.device;

    for (size_t i = 0; i < mStorageBuffers.size(); i++)
    {
        vkDestroyBuffer(device, mStorageBuffers[i], nullptr);
        vkFreeMemory(device, mStorageBuffersMemory[i], nullptr);
    }

    for (size_t i = 0; i < mTextures.size(); i++)
    {
        vkDestroySampler(device, mTextureSamplers[i], nullptr);
        DestroyVulkanImage(device, mTextures[i]);
    }

    DestroyVulkanImage(device, mDepthTexture);
}

void VulkanQuadRenderer::UpdateBuffer(VulkanRenderDevice &vkDev, size_t i) {
    UploadBufferData(vkDev, mStorageBuffersMemory[i], 0, mQuads.data(), mQuads.size() * sizeof(VertexData));
}

void VulkanQuadRenderer::PushConstants(VkCommandBuffer commandBuffer, uint32_t textureIndex, const vec2 &offset) {
    const ConstBuffer constBuffer = {offset, textureIndex};
    vkCmdPushConstants(commandBuffer, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ConstBuffer), &constBuffer);
}

void VulkanQuadRenderer::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    if(mQuads.empty())
        return;

    BeginRenderPass(commandBuffer, currentImage);

    vkCmdDraw(commandBuffer, static_cast<uint32_t>(mQuads.size()), 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanQuadRenderer::Quad(float x1, float y1, float x2, float y2) {
    VertexData v1 { { x1, y1, 0 }, { 0, 0 } };
    VertexData v2 { { x2, y1, 0 }, { 1, 0 } };
    VertexData v3 { { x2, y2, 0 }, { 1, 1 } };
    VertexData v4 { { x1, y2, 0 }, { 0, 1 } };

    mQuads.push_back( v1 );
    mQuads.push_back( v2 );
    mQuads.push_back( v3 );

    mQuads.push_back( v1 );
    mQuads.push_back( v3 );
    mQuads.push_back( v4 );
}

void VulkanQuadRenderer::Clear() {
    mQuads.clear();
}

bool VulkanQuadRenderer::CreateDescriptorSet(VulkanRenderDevice &vkDev) {
    const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(mTextures.size()))
    };

    const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
    };

    VK_CHECK(vkCreateDescriptorSetLayout(vkDev.device, &layoutInfo, nullptr, &mDescriptorSetLayout));

    const std::vector<VkDescriptorSetLayout> layouts(vkDev.swapchainImages.size(), mDescriptorSetLayout);

    const VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = mDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(vkDev.swapchainImages.size()),
            .pSetLayouts = layouts.data()
    };

    mDescriptorSets.resize(vkDev.swapchainImages.size());
    VK_CHECK(vkAllocateDescriptorSets(vkDev.device, &allocInfo, mDescriptorSets.data()));

    std::vector<VkDescriptorImageInfo> textureDescriptors(mTextures.size());
    for (size_t i = 0; i < mTextures.size(); i++) {
        textureDescriptors[i] = VkDescriptorImageInfo {
                .sampler = mTextureSamplers[i],
                .imageView = mTextures[i].imageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++)
    {
        const VkDescriptorBufferInfo bufferInfo = {
                .buffer = mUniformBuffers[i],
                .offset = 0,
                .range = sizeof(ConstBuffer)
        };
        const VkDescriptorBufferInfo bufferInfo2 = {
                .buffer = mStorageBuffers[i],
                .offset = 0,
                .range = mVertexBufferSize
        };

        const std::array<VkWriteDescriptorSet, 3> descriptorWrites = {
                VkWriteDescriptorSet {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = mDescriptorSets[i],
                        .dstBinding = 0,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .pBufferInfo = &bufferInfo
                },
                VkWriteDescriptorSet {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = mDescriptorSets[i],
                        .dstBinding = 1,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .pBufferInfo = &bufferInfo2
                },
                VkWriteDescriptorSet {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = mDescriptorSets[i],
                        .dstBinding = 2,
                        .dstArrayElement = 0,
                        .descriptorCount = static_cast<uint32_t>(mTextures.size()),
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = textureDescriptors.data()
                },
        };
        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    return true;
}
