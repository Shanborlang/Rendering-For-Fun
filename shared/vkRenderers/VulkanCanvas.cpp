#include "VulkanCanvas.h"
#include "../EasyProfilerWrapper.h"

VulkanCanvas::VulkanCanvas(VulkanRenderDevice &vkDev, VulkanImage depth)
: RendererBase(vkDev, depth){
    const size_t imgCount = vkDev.swapchainImages.size();

    mStorageBuffer.resize(imgCount);
    mStorageBufferMemory.resize(imgCount);

    for(size_t i = 0; i < imgCount; i++) {
        if(!CreateBuffer(vkDev.device, vkDev.physicalDevice, kMaxLinesDataSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         mStorageBuffer[i], mStorageBufferMemory[i])) {
            std::fprintf(stderr, "VulkanCanvas: CreateBuffer() failed\n");
            exit(EXIT_FAILURE);
        }
    }

    if (!CreateColorAndDepthRenderPass(vkDev, (depth.image != VK_NULL_HANDLE), &mRenderPass, RenderPassCreateInfo()) ||
        !CreateUniformBuffers(vkDev, sizeof(UniformBuffer)) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, depth.imageView, mSwapchainFramebuffers) ||
        !CreateDescriptorPool(vkDev, 1, 1, 0, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout, { "../../data/shaders/Lines.vert", "../../data/shaders/Lines.frag" }, &mGraphicsPipeline, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, (depth.image != VK_NULL_HANDLE), true ))
    {
        std::fprintf(stderr, "VulkanCanvas: failed to create pipeline\n");
        exit(EXIT_FAILURE);
    }
}

VulkanCanvas::~VulkanCanvas() {
    for (size_t i = 0; i < mSwapchainFramebuffers.size(); i++)
    {
        vkDestroyBuffer(mDevice, mStorageBuffer[i], nullptr);
        vkFreeMemory(mDevice, mStorageBufferMemory[i], nullptr);
    }
}

void VulkanCanvas::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    EASY_FUNCTION()

    if(mLines.empty())
        return;

    BeginRenderPass(commandBuffer, currentImage);

    vkCmdDraw(commandBuffer, static_cast<uint32_t>(mLines.size()), 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanCanvas::Clear() {
    mLines.clear();
}

void VulkanCanvas::Line(const vec3 &p1, const vec3 &p2, const vec4 &c) {
    mLines.push_back({.position = p1, .color = c});
    mLines.push_back({.position = p2, .color = c});
}

void VulkanCanvas::Plane3d(const vec3 &orig, const vec3 &v1, const vec3 &v2, int n1, int n2, float s1, float s2,
                           const vec4 &color, const vec4 &outlineColor) {
    Line(orig - s1 / 2.0f * v1 - s2 / 2.0f * v2, orig - s1 / 2.0f * v1 + s2 / 2.0f * v2, outlineColor);
    Line(orig + s1 / 2.0f * v1 - s2 / 2.0f * v2, orig + s1 / 2.0f * v1 + s2 / 2.0f * v2, outlineColor);

    Line(orig - s1 / 2.0f * v1 + s2 / 2.0f * v2, orig + s1 / 2.0f * v1 + s2 / 2.0f * v2, outlineColor);
    Line(orig - s1 / 2.0f * v1 - s2 / 2.0f * v2, orig + s1 / 2.0f * v1 - s2 / 2.0f * v2, outlineColor);

    for (int i = 1; i < n1; i++)
    {
        float t = ((float)i - (float)n1 / 2.0f) * s1 / (float)n1;
        const vec3 o1 = orig + t * v1;
        Line(o1 - s2 / 2.0f * v2, o1 + s2 / 2.0f * v2, color);
    }

    for (int i = 1; i < n2; i++)
    {
        const float t = ((float)i - (float)n2 / 2.0f) * s2 / (float)n2;
        const vec3 o2 = orig + t * v2;
        Line(o2 - s1 / 2.0f * v1, o2 + s1 / 2.0f * v1, color);
    }
}

void VulkanCanvas::UpdateBuffer(VulkanRenderDevice &vkDev, size_t currentImage) {
    if(mLines.empty())
        return;

    const VkDeviceSize bufferSize = mLines.size() * sizeof(VertexData);

    UploadBufferData(vkDev, mStorageBufferMemory[currentImage], 0, mLines.data(), bufferSize);
}

void VulkanCanvas::UpdateUniformBuffer(VulkanRenderDevice &vkDev, const mat4 &modelViewProj, float time,
                                       uint32_t currentImage) {
    const UniformBuffer ubo = {
            .mvp = modelViewProj,
            .time = time
    };
    UploadBufferData(vkDev, mUniformBuffersMemory[currentImage], 0, &ubo, sizeof(ubo));
}

bool VulkanCanvas::CreateDescriptorSet(VulkanRenderDevice &vkDev) {
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
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

        const VkDescriptorBufferInfo bufferInfo  = { mUniformBuffers[i], 0, sizeof(UniformBuffer) };
        const VkDescriptorBufferInfo bufferInfo2 = { mStorageBuffer[i], 0, kMaxLinesDataSize };

        const std::array<VkWriteDescriptorSet, 2> descriptorWrites = {
                BufferWriteDescriptorSet(ds, &bufferInfo,	0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        };

        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    return true;
}
