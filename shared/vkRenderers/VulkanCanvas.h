#pragma once

#include "VulkanRendererBase.h"

#include <glm/glm.hpp>
#include <glm/ext.hpp>

using glm::mat4;
using glm::vec3;
using glm::vec4;

class VulkanCanvas : public RendererBase{
public:
    explicit VulkanCanvas(VulkanRenderDevice& vkDev, VulkanImage depth);
    virtual ~VulkanCanvas();

    virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) override;

    void Clear();
    void Line(const vec3& p1, const vec3& p2, const vec4& c);
    void Plane3d(const vec3& orig, const vec3& v1, const vec3& v2, int n1, int n2, float s1, float s2, const vec4& color, const vec4& outlineColor);
    void UpdateBuffer(VulkanRenderDevice& vkDev, size_t currentImage);
    void UpdateUniformBuffer(VulkanRenderDevice& vkDev, const mat4& modelViewProj, float time, uint32_t currentImage);

private:
    struct VertexData {
        vec3 position;
        vec4 color;
    };

    struct UniformBuffer {
        mat4 mvp;
        float time;
    };

    bool CreateDescriptorSet(VulkanRenderDevice& vkDev);

    std::vector<VertexData> mLines;

    // 7. Storage Buffer with index and vertex data
    std::vector<VkBuffer> mStorageBuffer;
    std::vector<VkDeviceMemory> mStorageBufferMemory;

    static constexpr unsigned kMaxLinesCount = 65536;
    static constexpr unsigned kMaxLinesDataSize = kMaxLinesCount * sizeof(VulkanCanvas::VertexData) * 2;
};


