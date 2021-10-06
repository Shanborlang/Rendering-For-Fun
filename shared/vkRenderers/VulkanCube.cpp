#include "VulkanCube.h"
#include "../EasyProfilerWrapper.h"

#include <glm/ext.hpp>

CubeRenderer::CubeRenderer(VulkanRenderDevice &vkDev, VulkanImage inDepthTexture, const char *textureFile)
: RendererBase(vkDev, inDepthTexture) {
    // Resource Loading
    CreateCubeTextureImage(vkDev, textureFile, mTexture.image, mTexture.imageMemory);

    CreateImageView(vkDev.device, mTexture.image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &mTexture.imageView, VK_IMAGE_VIEW_TYPE_CUBE, 6);
    CreateTextureSampler(vkDev.device, &mTextureSampler);

    // Pipeline initialization
    if (!CreateColorAndDepthRenderPass(vkDev, true, &mRenderPass, RenderPassCreateInfo()) ||
        !CreateUniformBuffers(vkDev, sizeof(glm::mat4)) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, mDepthTexture.imageView, mSwapchainFramebuffers) ||
        !CreateDescriptorPool(vkDev, 1, 0, 1, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, { "../../../data/shaders/VKCube.vert", "../../../data/shaders/VKCube.frag" }, &mGraphicsPipeline))
    {
        std::fprintf(stderr, "CubeRenderer: failed to create pipeline\n");
        exit(EXIT_FAILURE);
    }
}

CubeRenderer::~CubeRenderer() {
    vkDestroySampler(mDevice, mTextureSampler, nullptr);
    DestroyVulkanImage(mDevice, mTexture);
}

void CubeRenderer::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    EASY_FUNCTION()

    BeginRenderPass(commandBuffer, currentImage);

    vkCmdDraw(commandBuffer, 36, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
}

void CubeRenderer::UpdateUniformBuffer(VulkanRenderDevice &vkDev, uint32_t currentImage, const glm::mat4 &m) {
    UploadBufferData(vkDev, mUniformBuffersMemory[currentImage], 0, glm::value_ptr(m), sizeof(glm::mat4));
}

bool CubeRenderer::CreateDescriptorSet(VulkanRenderDevice &vkDev) {
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
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

        const VkDescriptorBufferInfo bufferInfo  = { mUniformBuffers[i], 0, sizeof(glm::mat4) };
        const VkDescriptorImageInfo  imageInfo   = { mTextureSampler, mTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        const std::array<VkWriteDescriptorSet, 2> descriptorWrites = {
                BufferWriteDescriptorSet(ds, &bufferInfo,  0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                ImageWriteDescriptorSet( ds, &imageInfo,   1)
        };

        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    return true;
}
