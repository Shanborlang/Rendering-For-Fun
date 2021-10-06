#include "VulkanRendererBase.h"

void RendererBase::BeginRenderPass(VkCommandBuffer commandBuffer, size_t currentImage) {
    const VkRect2D screenRect = {
            .offset = {0 , 0},
            .extent = {.width = mFramebufferWidth, .height = mFramebufferHeight }
    };

    const VkRenderPassBeginInfo renderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = mRenderPass,
            .framebuffer = mSwapchainFramebuffers[currentImage],
            .renderArea = screenRect
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &mDescriptorSets[currentImage], 0,
                            nullptr);
}

bool RendererBase::CreateUniformBuffers(VulkanRenderDevice &vkDev, size_t uniformDataSize) {
    mUniformBuffers.resize(vkDev.swapchainImages.size());
    mUniformBuffersMemory.resize(vkDev.swapchainImages.size());

    for(size_t i = 0; i < vkDev.swapchainImages.size(); i++) {
        if(!CreateUniformBuffer(vkDev, mUniformBuffers[i], mUniformBuffersMemory[i], uniformDataSize)) {
            std::fprintf(stderr, "Cannot create uniform buffer\n");
            fflush(stdout);
            return false;
        }
    }

    return true;
}

RendererBase::~RendererBase() {
    for(auto buf : mUniformBuffers)
        vkDestroyBuffer(mDevice, buf, nullptr);

    for(auto mem : mUniformBuffersMemory)
        vkFreeMemory(mDevice, mem, nullptr);

    vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);

    for(auto framebuffer : mSwapchainFramebuffers)
        vkDestroyFramebuffer(mDevice, framebuffer, nullptr);

    vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
    vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
    vkDestroyPipeline(mDevice, mGraphicsPipeline, nullptr);
}
