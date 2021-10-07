//
// Created by shanb on 07/10/2021.
//

#include "VulkanSingleQuad.h"

VulkanSingleQuadRenderer::VulkanSingleQuadRenderer(VulkanRenderDevice &vkDev, VulkanImage tex, VkSampler sampler,
                                                   VkImageLayout desiredLayout)
                                                   : vkDev(vkDev), texture(tex),
                                                   textureSampler(sampler),
                                                   RendererBase(vkDev, VulkanImage()){
    /* we don't need them, but allocate them to allow destructor to complete */
    if (!CreateUniformBuffers(vkDev, sizeof(uint32_t)) ||
        !CreateDescriptorPool(vkDev, 0, 0, 1, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev, desiredLayout) ||
        !CreateColorAndDepthRenderPass(vkDev, false, &mRenderPass, RenderPassCreateInfo()) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, { "../../../data/shaders/quad.vert", "../../../data/shaders/quad.frag" }, &mGraphicsPipeline))
    {
        printf("Failed to create pipeline\n");
        fflush(stdout);
        exit(0);
    }

    CreateColorAndDepthFramebuffers(vkDev, mRenderPass, VK_NULL_HANDLE, mSwapchainFramebuffers);
}

VulkanSingleQuadRenderer::~VulkanSingleQuadRenderer() {

}



bool VulkanSingleQuadRenderer::CreateDescriptorSet(VulkanRenderDevice &vkDev, VkImageLayout desiredLayout) {
    VkDescriptorSetLayoutBinding binding = DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

    const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &binding
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

    VkDescriptorImageInfo textureDescriptor = {
            .sampler = textureSampler,
            .imageView = texture.imageView,
            .imageLayout = desiredLayout
    };

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++) {
        VkWriteDescriptorSet imageDescriptorWrite = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = mDescriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &textureDescriptor
        };

        vkUpdateDescriptorSets(vkDev.device, 1, &imageDescriptorWrite, 0, nullptr);
    }

    return true;
}

void VulkanSingleQuadRenderer::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    BeginRenderPass(commandBuffer, currentImage);

    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}
