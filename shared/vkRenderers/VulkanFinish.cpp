#include "VulkanFinish.h"
#include "../EasyProfilerWrapper.h"

VulkanFinish::VulkanFinish(VulkanRenderDevice &vkDev, VulkanImage depthTexture)
: RendererBase(vkDev, depthTexture) {
    if(!CreateColorAndDepthRenderPass(
            vkDev, (depthTexture.image != VK_NULL_HANDLE), &mRenderPass, RenderPassCreateInfo{.clearColor_=false, .clearDepth_= false, .flags_=eRenderPassBit_Last}
            )) {
        std::fprintf(stderr, "VulkanFinish : failed to create render pass\n");
        exit(EXIT_FAILURE);
    }

    CreateColorAndDepthFramebuffers(vkDev, mRenderPass, depthTexture.imageView, mSwapchainFramebuffers);
}

void VulkanFinish::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    EASY_FUNCTION()

    const VkRect2D screenRect = {
            .offset = {0, 0},
            .extent = {.width = mFramebufferWidth, .height = mFramebufferHeight}
    };

    const VkRenderPassBeginInfo renderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = mRenderPass,
            .framebuffer = mSwapchainFramebuffers[currentImage],
            .renderArea = screenRect
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(commandBuffer);
}
