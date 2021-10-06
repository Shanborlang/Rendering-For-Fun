#pragma once

#include <array>
#include <functional>
#include <vector>
#include <cassert>

#define VK_NO_PROTOTYPES
#include <volk/volk.h>

#include "glslang_c_interface.h"

#define VK_CHECK(value) CHECK(value == VK_SUCCESS, __FILE__, __LINE__);
#define VK_CHECK_RET(value) if ( value != VK_SUCCESS ) { CHECK(false, __FILE__, __LINE__); return value; }
#define BL_CHECK(value) CHECK(value, __FILE__, __LINE__);

struct VulkanInstance final {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT messenger;
    VkDebugReportCallbackEXT reportCallback;
};

struct VulkanRenderDevice final {
    uint32_t framebufferWidth;
    uint32_t framebufferHeight;

    VkDevice device;
    VkQueue graphicsQueue;
    VkPhysicalDevice physicalDevice;

    uint32_t graphicsFamily;

    VkSwapchainKHR swapchain;
    VkSemaphore semaphore;
    VkSemaphore renderSemaphore;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    // were we initialized with compute capabilities
    bool useCompute = false;

    // [may coincide with graphicsFamily]
    uint32_t computeFamily;
    VkQueue computeQueue;

    // a list of all queues (for shared buffer allocation)
    std::vector<uint32_t> deviceQueueIndices;
    std::vector<VkQueue> deviceQueues;

    VkCommandBuffer computeCommandBuffer;
    VkCommandPool computeCommandPool;
};

// Features we need for our Vulkan context
struct VulkanContextFeatures
{
    bool supportScreenshots_ = false;

    bool geometryShader_    = true;
    bool tessellationShader_ = false;

    bool vertexPipelineStoresAndAtomics_ = false;
    bool fragmentStoresAndAtomics_ = false;
};

/* To avoid breaking chapter 1-6 samples, we introduce a class which differs from VulkanInstance in that it has a ctor & dtor */
struct VulkanContextCreator
{
    VulkanContextCreator() = default;

    VulkanContextCreator(VulkanInstance& vk, VulkanRenderDevice& dev, void* window, int screenWidth, int screenHeight, const VulkanContextFeatures& ctxFeatures = VulkanContextFeatures());
    ~VulkanContextCreator();

    VulkanInstance& instance;
    VulkanRenderDevice& vkDev;
};

struct SwapchainSupportDetails final {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct ShaderModule final {
    std::vector<uint32_t> SPIRV;
    VkShaderModule shaderModule{};
};

struct VulkanBuffer
{
    VkBuffer       buffer;
    VkDeviceSize   size;
    VkDeviceMemory memory;

    /* Permanent mapping to CPU address space (see VulkanResources::addBuffer) */
    void*          ptr;
};

struct VulkanImage final {
    VkImage image{};
    VkDeviceMemory imageMemory{};
    VkImageView imageView{};
};

// Aggregate structure for passing around the texture data
struct VulkanTexture final {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    VkFormat format;

    VulkanImage image;
    VkSampler sampler;

    // Offscreen buffers require VK_IMAGE_LAYOUT_GENERAL && static textures have VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    VkImageLayout desiredLayout;
};

void CHECK(bool check, const char* fileName, int lineNumber);

bool SetupDebugCallbacks(VkInstance instance, VkDebugUtilsMessengerEXT* messenger, VkDebugReportCallbackEXT* reportCallback);

VkResult CreateShaderModule(VkDevice device, ShaderModule* shader, const char* fileName);

size_t CompileShaderFile(const char* file, ShaderModule& shaderModule);

inline VkPipelineShaderStageCreateInfo ShaderStageInfo(VkShaderStageFlagBits shaderStage, ShaderModule& module, const char* entryPoint)
{
    return VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = shaderStage,
            .module = module.shaderModule,
            .pName = entryPoint,
            .pSpecializationInfo = nullptr
    };
}

VkShaderStageFlagBits glslangShaderStageToVulkan(glslang_stage_t sh);

inline VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding(uint32_t binding, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags, uint32_t descriptorCount = 1) {
    return {
        .binding = binding,
        .descriptorType = descriptorType,
        .descriptorCount = descriptorCount,
        .stageFlags = stageFlags,
        .pImmutableSamplers = nullptr
    };
}

inline VkWriteDescriptorSet BufferWriteDescriptorSet(VkDescriptorSet ds, const VkDescriptorBufferInfo* bi, uint32_t bindIdx, VkDescriptorType dType)
{
    return VkWriteDescriptorSet { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                                  ds, bindIdx, 0, 1, dType, nullptr, bi, nullptr
    };
}

inline VkWriteDescriptorSet ImageWriteDescriptorSet(VkDescriptorSet ds, const VkDescriptorImageInfo* ii, uint32_t bindIdx)
{
    return VkWriteDescriptorSet { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                                  ds, bindIdx, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  ii, nullptr, nullptr
    };
}

void CreateInstance(VkInstance* instance);

VkResult CreateDevice(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures deviceFeatures, uint32_t graphicsFamily, VkDevice* device);
VkResult CreateDeviceWithCompute(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures deviceFeatures, uint32_t graphicsFamily, uint32_t computeFamily, VkDevice* device);
VkResult CreateDevice2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 deviceFeatures2, uint32_t graphicsFamily, VkDevice* device);
VkResult CreateDevice2WithCompute(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 deviceFeatures2, uint32_t graphicsFamily, uint32_t computeFamily, VkDevice* device);

VkResult CreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t graphicsFamily, uint32_t width, uint32_t height, VkSwapchainKHR* swapchain, bool supportScreenshots = false);

size_t CreateSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, std::vector<VkImage>& swapchainImages, std::vector<VkImageView>& swapchainImageViews);

VkResult CreateSemaphore(VkDevice device, VkSemaphore* outSemaphore);

bool CreateTextureSampler(VkDevice device, VkSampler* sampler, VkFilter minFilter = VK_FILTER_LINEAR, VkFilter maxFilter = VK_FILTER_LINEAR, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

bool CreateDescriptorPool(VulkanRenderDevice& vkDev, uint32_t uniformBufferCount, uint32_t storageBufferCount, uint32_t samplerCount, VkDescriptorPool* descriptorPool);

bool IsDeviceSuitable(VkPhysicalDevice device);

SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

uint32_t ChooseSwapImageCount(const VkSurfaceCapabilitiesKHR& capabilities);

VkResult FindSuitablePhysicalDevice(VkInstance instance, const std::function<bool(VkPhysicalDevice)> &selector, VkPhysicalDevice* physicalDevice);

uint32_t FindQueueFamilies(VkPhysicalDevice device, VkQueueFlags desiredFlags);

VkFormat FindSupportedFormat(VkPhysicalDevice device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

uint32_t FindMemoryType(VkPhysicalDevice device, uint32_t typeFilter, VkMemoryPropertyFlags properties);

VkFormat FindDepthFormat(VkPhysicalDevice device);

bool HasStencilComponent(VkFormat format);

bool CreateGraphicsPipeline(
        VulkanRenderDevice& vkDev,
        VkRenderPass renderPass, VkPipelineLayout pipelineLayout,
        const std::vector<const char*>& shaderFiles,
        VkPipeline* pipeline,
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST /* defaults to triangles*/,
        bool useDepth = true,
        bool useBlending = true,
        bool dynamicScissorState = false,
        int32_t customWidth  = -1,
        int32_t customHeight = -1,
        uint32_t numPatchControlPoints = 0);

VkResult CreateComputePipeline(VkDevice device, VkShaderModule computeShader, VkPipelineLayout pipelineLayout, VkPipeline* pipeline);

bool CreateSharedBuffer(VulkanRenderDevice& vkDev, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

bool CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
bool CreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, VkImageCreateFlags flags = 0, uint32_t mipLevels = 1);

bool CreateVolume(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, uint32_t depth,
                  VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, VkImageCreateFlags flags);

bool CreateOffscreenImage(VulkanRenderDevice& vkDev,
                          VkImage& textureImage, VkDeviceMemory& textureImageMemory,
                          uint32_t texWidth, uint32_t texHeight,
                          VkFormat texFormat,
                          uint32_t layerCount, VkImageCreateFlags flags);

bool CreateOffscreenImageFromData(VulkanRenderDevice& vkDev,
                                  VkImage& textureImage, VkDeviceMemory& textureImageMemory,
                                  void* imageData, uint32_t texWidth, uint32_t texHeight,
                                  VkFormat texFormat,
                                  uint32_t layerCount, VkImageCreateFlags flags);

bool CreateDepthSampler(VkDevice device, VkSampler* sampler);

bool CreateUniformBuffer(VulkanRenderDevice& vkDev, VkBuffer& buffer, VkDeviceMemory& bufferMemory, VkDeviceSize bufferSize);

/** Copy [data] to GPU device buffer */
void UploadBufferData(VulkanRenderDevice& vkDev, const VkDeviceMemory& bufferMemory, VkDeviceSize deviceOffset, const void* data, const size_t dataSize);

/** Copy GPU device buffer data to [outData] */
void DownloadBufferData(VulkanRenderDevice& vkDev, const VkDeviceMemory& bufferMemory, VkDeviceSize deviceOffset, void* outData, size_t dataSize);

bool CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView* imageView, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, uint32_t layerCount = 1, uint32_t mipLevels = 1);

enum eRenderPassBit : uint8_t {
    eRenderPassBit_First               = 0x01, // clear the attachment
    eRenderPassBit_Last                = 0x02, // transition to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    eRenderPassBit_Offscreen           = 0x04, // transition to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    eRenderPassBit_OffscreenInternal   = 0x08  // keep VK_IMAGE_LAYOUT_*_ATTACHMENT_OPTIMAL
};

struct RenderPassCreateInfo final
{
    bool clearColor_ = false;
    bool clearDepth_ = false;
    uint8_t flags_ = 0;
};

// Utility structure for Renderer classes to know the details about starting this pass
struct RenderPass
{
    RenderPass() = default;
    explicit RenderPass(VulkanRenderDevice& device, bool useDepth = true, const RenderPassCreateInfo& ci = RenderPassCreateInfo());

    RenderPassCreateInfo info;
    VkRenderPass handle = VK_NULL_HANDLE;
};

bool CreateColorOnlyRenderPass(VulkanRenderDevice& device, VkRenderPass* renderPass, const RenderPassCreateInfo& ci, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM);
bool CreateColorAndDepthRenderPass(VulkanRenderDevice& device, bool useDepth, VkRenderPass* renderPass, const RenderPassCreateInfo& ci, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM);
bool CreateDepthOnlyRenderPass(VulkanRenderDevice& vkDev, VkRenderPass* renderPass, const RenderPassCreateInfo& ci);

VkCommandBuffer BeginSingleTimeCommands(VulkanRenderDevice& vkDev);
void EndSingleTimeCommands(VulkanRenderDevice& vkDev, VkCommandBuffer commandBuffer);
void CopyBuffer(VulkanRenderDevice& vkDev, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void TransitionImageLayout(VulkanRenderDevice& vkDev, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount = 1, uint32_t mipLevels = 1);
void TransitionImageLayoutCmd(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount = 1, uint32_t mipLevels = 1);

bool InitVulkanRenderDevice(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, const std::function<bool(VkPhysicalDevice)>& selector, VkPhysicalDeviceFeatures deviceFeatures);
bool InitVulkanRenderDevice2(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, std::function<bool(VkPhysicalDevice)> selector, VkPhysicalDeviceFeatures2 deviceFeatures2);
bool InitVulkanRenderDevice3(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, const VulkanContextFeatures& ctxFeatures = VulkanContextFeatures());
void DestroyVulkanRenderDevice(VulkanRenderDevice& vkDev);
void DestroyVulkanInstance(VulkanInstance& vk);

bool InitVulkanRenderDeviceWithCompute(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, VkPhysicalDeviceFeatures deviceFeatures);

bool InitVulkanRenderDevice2WithCompute(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, std::function<bool(VkPhysicalDevice)> selector, VkPhysicalDeviceFeatures2 deviceFeatures2, bool supportScreenshots = false);

bool CreateColorAndDepthFramebuffers(VulkanRenderDevice& vkDev, VkRenderPass renderPass, VkImageView depthImageView, std::vector<VkFramebuffer>& swapchainFramebuffers);
bool CreateColorAndDepthFramebuffer(VulkanRenderDevice& vkDev,
                                    uint32_t width, uint32_t height,
                                    VkRenderPass renderPass, VkImageView colorImageView, VkImageView depthImageView,
                                    VkFramebuffer* framebuffer);
bool CreateDepthOnlyFramebuffer(VulkanRenderDevice& vkDev,
                                uint32_t width, uint32_t height,
                                VkRenderPass renderPass, VkImageView depthImageView,
                                VkFramebuffer* framebuffer);

void CopyBufferToImage(VulkanRenderDevice& vkDev, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount = 1);
void CopyImageToBuffer(VulkanRenderDevice& vkDev, VkImage image, VkBuffer buffer, uint32_t width, uint32_t height, uint32_t layerCount = 1);

void CopyMIPBufferToImage(VulkanRenderDevice& vkDev, VkBuffer buffer, VkImage image, uint32_t mipLevels, uint32_t width, uint32_t height, uint32_t bytesPP, uint32_t layerCount = 1);

void DestroyVulkanImage(VkDevice device, VulkanImage& image);
void DestroyVulkanTexture(VkDevice device, VulkanTexture& texture);

uint32_t BytesPerTexFormat(VkFormat fmt);

/* VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL for real update of an existing texture */
bool UpdateTextureImage(VulkanRenderDevice& vkDev, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t texWidth, uint32_t texHeight, VkFormat texFormat, uint32_t layerCount, const void* imageData, VkImageLayout sourceImageLayout = VK_IMAGE_LAYOUT_UNDEFINED);

bool UpdateTextureVolume(VulkanRenderDevice& vkDev, VkImage& textureVolume, VkDeviceMemory& textureVolumeMemory, uint32_t texWidth, uint32_t texHeight, uint32_t texDepth, VkFormat texFormat, const void* volumeData, VkImageLayout sourceImageLayout = VK_IMAGE_LAYOUT_UNDEFINED);

bool DownloadImageData(VulkanRenderDevice& vkDev, VkImage& textureImage, uint32_t texWidth, uint32_t texHeight, VkFormat texFormat, uint32_t layerCount, void* imageData, VkImageLayout sourceImageLayout);

bool CreateDepthResources(VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, VulkanImage& depth);

bool CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout dsLayout, VkPipelineLayout* pipelineLayout);

bool CreatePipelineLayoutWithConstants(VkDevice device, VkDescriptorSetLayout dsLayout, VkPipelineLayout* pipelineLayout, uint32_t vtxConstSize, uint32_t fragConstSize);

bool CreateTextureImageFromData(VulkanRenderDevice& vkDev,
                                VkImage& textureImage, VkDeviceMemory& textureImageMemory,
                                void* imageData, uint32_t texWidth, uint32_t texHeight,
                                VkFormat texFormat,
                                uint32_t layerCount = 1, VkImageCreateFlags flags = 0);

bool CreateMIPTextureImageFromData(VulkanRenderDevice& vkDev,
                                   VkImage& textureImage, VkDeviceMemory& textureImageMemory,
                                   void* mipData, uint32_t mipLevels, uint32_t texWidth, uint32_t texHeight,
                                   VkFormat texFormat,
                                   uint32_t layerCount = 1, VkImageCreateFlags flags = 0);

bool CreateTextureVolumeFromData(VulkanRenderDevice& vkDev,
                                 VkImage& textureVolume, VkDeviceMemory& textureVolumeMemory,
                                 void* volumeData, uint32_t texWidth, uint32_t texHeight, uint32_t texDepth,
                                 VkFormat texFormat,
                                 VkImageCreateFlags flags = 0);

bool CreateTextureImage(VulkanRenderDevice& vkDev, const char* filename, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t* outTexWidth = nullptr, uint32_t* outTexHeight = nullptr);

bool CreateMIPTextureImage(VulkanRenderDevice& vkDev, const char* filename, uint32_t mipLevels, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t* width = nullptr, uint32_t* height = nullptr);

bool CreateCubeTextureImage(VulkanRenderDevice& vkDev, const char* filename, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t* width = nullptr, uint32_t* height = nullptr);

bool CreateMIPCubeTextureImage(VulkanRenderDevice& vkDev, const char* filename, uint32_t mipLevels, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t* width = nullptr, uint32_t* height = nullptr);

size_t AllocateVertexBuffer(VulkanRenderDevice& vkDev, VkBuffer* storageBuffer, VkDeviceMemory* storageBufferMemory, size_t vertexDataSize, const void* vertexData, size_t indexDataSize, const void* indexData);

bool CreateTexturedVertexBuffer(VulkanRenderDevice& vkDev, const char* filename, VkBuffer* storageBuffer, VkDeviceMemory* storageBufferMemory, size_t* vertexBufferSize, size_t* indexBufferSize);

bool CreatePBRVertexBuffer(VulkanRenderDevice& vkDev, const char* filename, VkBuffer* storageBuffer, VkDeviceMemory* storageBufferMemory, size_t* vertexBufferSize, size_t* indexBufferSize);

bool ExecuteComputeShader(VulkanRenderDevice& vkDev,
                          VkPipeline computePipeline, VkPipelineLayout pl, VkDescriptorSet ds,
                          uint32_t xsize, uint32_t ysize, uint32_t zsize);

bool CreateComputeDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout* descriptorSetLayout);

void InsertComputedBufferBarrier(VulkanRenderDevice& vkDev, VkCommandBuffer commandBuffer, VkBuffer buffer);
void InsertComputedImageBarrier(VkCommandBuffer commandBuffer, VkImage image);

VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice physDevice);

inline uint32_t GetVulkanBufferAlignment(VulkanRenderDevice& vkDev)
{
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(vkDev.physicalDevice, &devProps);
    return static_cast<uint32_t>(devProps.limits.minStorageBufferOffsetAlignment);
}

/* Check if the texture is used as a depth buffer */
inline bool IsDepthFormat(VkFormat fmt) {
    return
            (fmt == VK_FORMAT_D16_UNORM) ||
            (fmt == VK_FORMAT_X8_D24_UNORM_PACK32) ||
            (fmt == VK_FORMAT_D32_SFLOAT) ||
            (fmt == VK_FORMAT_D16_UNORM_S8_UINT) ||
            (fmt == VK_FORMAT_D24_UNORM_S8_UINT) ||
            (fmt == VK_FORMAT_D32_SFLOAT_S8_UINT);
}

bool SetVkObjectName(VulkanRenderDevice& vkDev, void* object, VkObjectType objType, const char* name);

inline bool SetVkImageName(VulkanRenderDevice& vkDev, void* object, const char* name)
{
    return SetVkObjectName(vkDev, object, VK_OBJECT_TYPE_IMAGE, name);
}

/* This routine updates one texture discriptor in one descriptor set */
void UpdateTextureInDescriptorSetArray(VulkanRenderDevice& vkDev, VkDescriptorSet ds, VulkanTexture t, uint32_t textureIndex, uint32_t bindingIdx);