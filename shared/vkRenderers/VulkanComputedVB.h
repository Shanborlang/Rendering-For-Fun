#pragma once

#include "shared/Utils.h"
#include "shared/vkRenderers/VulkanComputedItem.h"

class ComputedVertexBuffer : public ComputedItem {
 public:
  ComputedVertexBuffer(VulkanRenderDevice &vkDev, const char *shaderName,
					   uint32_t indexBufferSize,
					   uint32_t uniformBufferSize,
					   uint32_t vertexSize,
					   uint32_t vertexCount,
					   bool supportDownload = false);

  virtual ~ComputedVertexBuffer() {}

  void UploadIndexData(uint32_t *indices);

  void DownloadVertices(void *vertexData);

  VkBuffer computedBuffer;
  VkDeviceMemory computedMemory;

  uint32_t computedVertexCount;

 protected:
  uint32_t indexBufferSize;
  uint32_t vertexSize;

  bool CanDownloadVertices;

  bool CreateComputedBuffer();

  bool CreateDescriptorSet();
  bool CreateComputedSetLayout();
};

