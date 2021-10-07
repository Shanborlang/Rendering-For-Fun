#pragma once

#include "shared/Utils.h"
#include "shared/UtilsVulkan.h"

class ComputedItem {
public:
    ComputedItem(VulkanRenderDevice& vkDev, uint32_t uniformBufferSize);
    virtual ~ComputedItem();

    void FillComputeCommandBuffer(void* pushConstant = nullptr, uint32_t pushConstantSize = 0, uint32_t xsize = 1, uint32_t ysize = 1, uint32_t zsize = 1);
    bool Submit();

    void WaitFence();

    inline void UploadUniformBuffer(uint32_t size, void* data) {
        UploadBufferData(vkDev, uniformBuffer.memory, 0, data, size);
    }

protected:
    VulkanRenderDevice& vkDev;

    VkFence fence;

    VulkanBuffer uniformBuffer;

    VkDescriptorSetLayout dsLayout;
    VkDescriptorPool      descriptorPool;
    VkDescriptorSet       descriptorSet;
    VkPipelineLayout      pipelineLayout;
    VkPipeline            pipeline;
};

