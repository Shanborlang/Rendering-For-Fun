#include "VulkanClear.h"
#include "../EasyProfilerWrapper.h"

VulkanClear::VulkanClear(VulkanRenderDevice &vkDev, VulkanImage depthTexture)
: RendererBase(vkDev, depthTexture)
, mShouldClearDepth(depthTexture.image != VK_NULL_HANDLE)
{
    if(!CreateColorAndDepthRenderPass(
            vkDev, mShouldClearDepth, &mRenderPass, RenderPassCreateInfo{.clearColor_ = true, .clearDepth_ = true, .flags_ = eRenderPassBit_First}
            )) {
        std::fprintf(stderr, "VulkanClear: failed to create render pass\n");
    }

    CreateColorAndDepthFramebuffers(vkDev, mRenderPass, depthTexture.imageView, mSwapchainFramebuffers);
}

void VulkanClear::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    EASY_FUNCTION()

    const VkClearValue clearValue[2] {
        VkClearValue {.color = {1.f, 1.f, 1.f, 1.f}},
        VkClearValue {.depthStencil = {1.f, 0}}
    };

    const VkRect2D screenRect = {
            .offset = {0, 0},
            .extent = {.width = mFramebufferWidth, .height = mFramebufferHeight}
    };

    const VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = mRenderPass,
            .framebuffer = mSwapchainFramebuffers[currentImage],
            .renderArea = screenRect,
            .clearValueCount = static_cast<uint32_t>(mShouldClearDepth ? 2 : 1),
            .pClearValues = &clearValue[0]
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(commandBuffer);
}
