#include "UtilsVulkan.h"
#include "Utils.h"
#include "StandAlone/ResourceLimits.h"
#include "Bitmap.h"
#include "UtilsCubemap.h"

#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/version.h>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <cstdio>

using glm::mat4;
using glm::vec3;
using glm::vec2;

void CHECK(bool check, const char *fileName, int lineNumber) {
    if (!check) {
        printf("CHECK() failed at %s:%i\n", fileName, lineNumber);
        assert(false);
        exit(EXIT_FAILURE);
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL sVulkanDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData
) {
    printf("Validation layer: %s\n", callbackData->pMessage);
    return VK_FALSE;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL sVulkanDebugReportCallback(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType,
        uint64_t object,
        size_t location,
        int32_t messageCode,
        const char *pLayerPrefix,
        const char *pMessage,
        void *userData
) {
    // https://github.com/zeux/niagara/blob/master/src/device.cpp   [ignoring performance warnings]
    // This silences warnings like "For optimal performance image layout should be VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL instead of GENERAL."
    if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
        return VK_FALSE;

    std::fprintf(stderr, "Debug callback (%s): %s\n", pLayerPrefix, pMessage);
    return VK_FALSE;
}

bool SetupDebugCallbacks(VkInstance instance, VkDebugUtilsMessengerEXT *messenger,
                         VkDebugReportCallbackEXT *reportCallback) {
    {
        const VkDebugUtilsMessengerCreateInfoEXT createInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = &sVulkanDebugCallback,
                .pUserData = nullptr
        };

        VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, messenger));
    }
    {
        const VkDebugReportCallbackCreateInfoEXT createInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
                .pNext = nullptr,
                .flags =
                VK_DEBUG_REPORT_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT |
                VK_DEBUG_REPORT_DEBUG_BIT_EXT,
                .pfnCallback = &sVulkanDebugReportCallback,
                .pUserData = nullptr
        };

        VK_CHECK(vkCreateDebugReportCallbackEXT(instance, &createInfo, nullptr, reportCallback));
    }

    return true;
}

VkShaderStageFlagBits glslangShaderStageToVulkan(glslang_stage_t sh) {
    switch(sh)
    {
        case GLSLANG_STAGE_VERTEX:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case GLSLANG_STAGE_FRAGMENT:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case GLSLANG_STAGE_GEOMETRY:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case GLSLANG_STAGE_TESSCONTROL:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case GLSLANG_STAGE_TESSEVALUATION:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case GLSLANG_STAGE_COMPUTE:
            return VK_SHADER_STAGE_COMPUTE_BIT;
    }

    return VK_SHADER_STAGE_VERTEX_BIT;
}

glslang_stage_t glslangShaderStageFromFileName(const char *fileName) {
    if (EndsWith(fileName, ".vert"))
        return GLSLANG_STAGE_VERTEX;

    if (EndsWith(fileName, ".frag"))
        return GLSLANG_STAGE_FRAGMENT;

    if (EndsWith(fileName, ".geom"))
        return GLSLANG_STAGE_GEOMETRY;

    if (EndsWith(fileName, ".comp"))
        return GLSLANG_STAGE_COMPUTE;

    if (EndsWith(fileName, ".tesc"))
        return GLSLANG_STAGE_TESSCONTROL;

    if (EndsWith(fileName, ".tese"))
        return GLSLANG_STAGE_TESSEVALUATION;

    return GLSLANG_STAGE_VERTEX;
}

static_assert(sizeof(TBuiltInResource) == sizeof(glslang_resource_t));

static size_t sCompileShader(glslang_stage_t stage, const char *shaderSource, ShaderModule &shaderModule) {
    const glslang_input_t input = {
            .language = GLSLANG_SOURCE_GLSL,
            .stage = stage,
            .client = GLSLANG_CLIENT_VULKAN,
            .client_version = GLSLANG_TARGET_VULKAN_1_1,
            .target_language = GLSLANG_TARGET_SPV,
            .target_language_version = GLSLANG_TARGET_SPV_1_3,
            .code = shaderSource,
            .default_version = 100,
            .default_profile = GLSLANG_NO_PROFILE,
            .force_default_version_and_profile = false,
            .forward_compatible = false,
            .messages = GLSLANG_MSG_DEFAULT_BIT,
            .resource = (const glslang_resource_t *) &glslang::DefaultTBuiltInResource,
    };

    glslang_shader_t *shader = glslang_shader_create(&input);

    if (!glslang_shader_preprocess(shader, &input)) {
        std::fprintf(stderr, "GLSL preprocessing failed\n");
        std::fprintf(stderr, "\n%s", glslang_shader_get_info_log(shader));
        std::fprintf(stderr, "\n%s", glslang_shader_get_info_debug_log(shader));
        PrintShaderSource(input.code);
        return 0;
    }

    if (!glslang_shader_parse(shader, &input)) {
        std::fprintf(stderr, "GLSL parsing failed\n");
        std::fprintf(stderr, "\n%s", glslang_shader_get_info_log(shader));
        std::fprintf(stderr, "\n%s", glslang_shader_get_info_debug_log(shader));
        PrintShaderSource(glslang_shader_get_preprocessed_code(shader));
        return 0;
    }

    glslang_program_t *program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        std::fprintf(stderr, "GLSL linking failed\n");
        std::fprintf(stderr, "\n%s", glslang_program_get_info_log(program));
        std::fprintf(stderr, "\n%s", glslang_program_get_info_debug_log(program));
        return 0;
    }

    glslang_program_SPIRV_generate(program, stage);

    shaderModule.SPIRV.resize(glslang_program_SPIRV_get_size(program));
    glslang_program_SPIRV_get(program, shaderModule.SPIRV.data());

    {
        const char *spirv_message = glslang_program_SPIRV_get_messages(program);
        if (spirv_message) std::fprintf(stderr, "%s", spirv_message);
    }

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    return shaderModule.SPIRV.size();
}

size_t CompileShaderFile(const char *file, ShaderModule &shaderModule) {
    if (auto shaderSource = ReadShaderFile(file); !shaderSource.empty())
        return sCompileShader(glslangShaderStageFromFileName(file), shaderSource.c_str(), shaderModule);

    return 0;
}

VkResult CreateShaderModule(VkDevice device, ShaderModule* shader, const char* fileName) {
    if(CompileShaderFile(fileName, *shader) < 1)
        return VK_NOT_READY;

    const VkShaderModuleCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shader->SPIRV.size() * sizeof(unsigned int),
            .pCode = shader->SPIRV.data()
    };

    return vkCreateShaderModule(device, &createInfo, nullptr, &shader->shaderModule);
}

VulkanContextCreator::VulkanContextCreator(VulkanInstance& vk, VulkanRenderDevice& dev, void* window, int screenWidth, int screenHeight,
                                           const VulkanContextFeatures& ctxFeatures)
                                           : instance(vk)
                                           , vkDev(dev) {
    CreateInstance(&vk.instance);

    if (!SetupDebugCallbacks(vk.instance, &vk.messenger, &vk.reportCallback))
        exit(0);

    if (glfwCreateWindowSurface(vk.instance, (GLFWwindow *)window, nullptr, &vk.surface))
        exit(0);

    if (!InitVulkanRenderDevice3(vk, dev, screenWidth, screenHeight, ctxFeatures))
        exit(0);
}

VulkanContextCreator::~VulkanContextCreator()
{
    DestroyVulkanRenderDevice(vkDev);
    DestroyVulkanInstance(instance);
}

RenderPass::RenderPass(VulkanRenderDevice& vkDev, bool useDepth, const RenderPassCreateInfo& ci)
: info(ci) {
    if (!CreateColorAndDepthRenderPass(vkDev, useDepth, &handle, ci)) {
        printf("Failed to create render pass\n");
        exit(EXIT_FAILURE);
    }
}

void CreateInstance(VkInstance *instance) {
    const std::vector<const char *> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char *> exts = {
            "VK_KHR_surface",
#if defined(_WIN32)
            "VK_KHR_win32_surface",
#elif defined(__linux__)
            "VK_KHR_xcb_surface",
#endif
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
            /* for index textures */
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };

    const VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "Vulkan",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1
    };

    const VkInstanceCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
            .ppEnabledLayerNames = validationLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(exts.size()),
            .ppEnabledExtensionNames = exts.data()
    };

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, instance));
    volkLoadInstance(*instance);
}

VkResult CreateDevice(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures deviceFeatures, uint32_t graphicsFamily,
                      VkDevice *device) {
    const std::vector<const char *> extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    const float queuePriority = 1.f;

    const VkDeviceQueueCreateInfo deviceQueueCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = graphicsFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
    };

    const VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = &deviceFeatures
    };

    return vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, device);
}

VkResult CreateDeviceWithCompute(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures deviceFeatures,
	uint32_t graphicsFamily, uint32_t computeFamily, VkDevice* device)
{
    const std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    if (graphicsFamily == computeFamily)
        return CreateDevice(physicalDevice, deviceFeatures, graphicsFamily, device);

    const float queuePriorities[2] = { 0.f, 0.f };
    const VkDeviceQueueCreateInfo qciGfx = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities[0]
    };

    const VkDeviceQueueCreateInfo qciComp = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = computeFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities[1]
    };

    const VkDeviceQueueCreateInfo qci[2] = { qciGfx,qciComp };

    const VkDeviceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 2,
        .pQueueCreateInfos = qci,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
        .pEnabledFeatures = &deviceFeatures
    };

    return vkCreateDevice(physicalDevice, &ci, nullptr, device);
}

VkResult CreateDevice2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 deviceFeatures2,
	uint32_t graphicsFamily, VkDevice* device)
{
    const std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        // for legacy drivers Vulkan 1.1
        VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME
    };

    const float queuePriority = 1.0f;

    const VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    const VkDeviceCreateInfo ci =
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &deviceFeatures2,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
        .pEnabledFeatures = nullptr
    };

    return vkCreateDevice(physicalDevice, &ci, nullptr, device);
}

VkResult CreateDevice2WithCompute(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 deviceFeatures2, uint32_t graphicsFamily, uint32_t computeFamily, VkDevice* device)
{
    const std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        // for legacy drivers Vulkan 1.1
        VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME
    };

    if (graphicsFamily == computeFamily)
        return CreateDevice2(physicalDevice, deviceFeatures2, graphicsFamily, device);

    const float queuePriorities[2] = { 0.f, 0.f };
    const VkDeviceQueueCreateInfo qciGfx = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities[0]
    };

    const VkDeviceQueueCreateInfo qciComp =
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = computeFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities[1]
    };

    const VkDeviceQueueCreateInfo qci[2] = { qciGfx, qciComp };

    const VkDeviceCreateInfo ci =
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &deviceFeatures2,
        .flags = 0,
        .queueCreateInfoCount = 2,
        .pQueueCreateInfos = qci,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
        .pEnabledFeatures = nullptr
    };

    return vkCreateDevice(physicalDevice, &ci, nullptr, device);
}

VkResult
CreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t graphicsFamily,
                uint32_t width, uint32_t height, VkSwapchainKHR *swapchain, bool supportScreenshots) {
    auto swapChainSupport = QuerySwapchainSupport(physicalDevice, surface);
    auto surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    auto presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);

    const VkSwapchainCreateInfoKHR createInfoKhr = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .flags = 0,
            .surface = surface,
            .minImageCount = ChooseSwapImageCount(swapChainSupport.capabilities),
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = {.width = width, .height = height},
            .imageArrayLayers = 1,
            .imageUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | (supportScreenshots ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u),
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &graphicsFamily,
            .preTransform = swapChainSupport.capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
    };

    return vkCreateSwapchainKHR(device, &createInfoKhr, nullptr, swapchain);
}

size_t CreateSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, std::vector<VkImage> &swapchainImages,
                             std::vector<VkImageView> &swapchainImageViews) {
    uint32_t imageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));

    swapchainImages.resize(imageCount);
    swapchainImageViews.resize(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()));

    for (uint32_t i = 0; i < imageCount; i++) {
        if (!CreateImageView(device, swapchainImages[i], VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT,
                             &swapchainImageViews[i]))
            exit(0);
    }

    return imageCount;
}

bool CreateSharedBuffer(VulkanRenderDevice& vkDev, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    auto familyCount = static_cast<uint32_t>(vkDev.deviceQueueIndices.size());

    if(familyCount < 2)
        return CreateBuffer(vkDev.device, vkDev.physicalDevice, size, usage, properties, buffer, bufferMemory);

    const VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = usage,
            .sharingMode = (familyCount > 1) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = static_cast<uint32_t>(vkDev.deviceQueueIndices.size()),
            .pQueueFamilyIndices = (familyCount > 1) ? vkDev.deviceQueueIndices.data() : nullptr
    };

    VK_CHECK(vkCreateBuffer(vkDev.device, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vkDev.device, buffer, &memRequirements);

    const VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(vkDev.physicalDevice, memRequirements.memoryTypeBits, properties)
    };

    VK_CHECK(vkAllocateMemory(vkDev.device, &allocInfo, nullptr, &bufferMemory));

    vkBindBufferMemory(vkDev.device, buffer, bufferMemory, 0);

    return true;
}

bool CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
    const VkBufferCreateInfo bufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
    };
    VK_CHECK(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer));

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    const VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = FindMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, properties)
    };
    VK_CHECK(vkAllocateMemory(device, &allocateInfo, nullptr, &bufferMemory));

    vkBindBufferMemory(device, buffer, bufferMemory, 0);

    return true;
}

bool IsDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    const bool isDiscreteGPU = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    const bool isIntegrateGPU = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    const bool isGPU = isDiscreteGPU || isIntegrateGPU;

    return isGPU && deviceFeatures.geometryShader;
}

SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
}

VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto mode: availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

uint32_t ChooseSwapImageCount(const VkSurfaceCapabilitiesKHR &capabilities) {
    const uint32_t imageCount = capabilities.minImageCount + 1;

    const bool imageCountExceeded = capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount;

    return imageCountExceeded ? capabilities.maxImageCount : imageCount;
}

VkResult FindSuitablePhysicalDevice(VkInstance instance, const std::function<bool(VkPhysicalDevice)> &selector,
                                    VkPhysicalDevice *physicalDevice) {
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    if (!deviceCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
    for (const auto &device: devices) {
        if (selector(device)) {
            *physicalDevice = device;
            return VK_SUCCESS;
        }
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

uint32_t FindQueueFamilies(VkPhysicalDevice device, VkQueueFlags desiredFlags) {
    uint32_t familyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    for (uint32_t i = 0; i != families.size(); i++) {
        if (families[i].queueCount && (families[i].queueFlags & desiredFlags))
            return i;
    }
    return 0;
}

VkFormat FindSupportedFormat(
        VkPhysicalDevice device,
        const std::vector<VkFormat>& candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    printf("failed to find supported format!\n");
    exit(0);
}

uint32_t FindMemoryType(VkPhysicalDevice device, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return 0xffffffff;
}

VkFormat FindDepthFormat(VkPhysicalDevice device) {
    return FindSupportedFormat(
            device,
            {
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D32_SFLOAT_S8_UINT,
            },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
    format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool CreateGraphicsPipeline(
        VulkanRenderDevice& vkDev,
        VkRenderPass renderPass, VkPipelineLayout pipelineLayout,
        const std::vector<const char*>& shaderFiles,
        VkPipeline* pipeline,
        VkPrimitiveTopology topology /* defaults to triangles*/,
        bool useDepth,
        bool useBlending,
        bool dynamicScissorState,
        int32_t customWidth,
        int32_t customHeight,
        uint32_t numPatchControlPoints) {

    std::vector<ShaderModule> shaderModules;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    shaderStages.resize(shaderFiles.size());
    shaderModules.resize(shaderFiles.size());

    for(size_t i = 0; i < shaderFiles.size(); i++) {
        const char* file = shaderFiles[i];
        VK_CHECK(CreateShaderModule(vkDev.device, &shaderModules[i], file));

        VkShaderStageFlagBits stage = glslangShaderStageToVulkan(glslangShaderStageFromFileName(file));

        shaderStages[i] = ShaderStageInfo(stage, shaderModules[i], "main");
    }

    const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            /* The only difference from createGraphicsPipeline() */
            .topology = topology,
            .primitiveRestartEnable = VK_FALSE
    };

    const VkViewport viewport = {
            .x = 0.f,
            .y = 0.f,
            .width = static_cast<float>(customWidth > 0 ? customWidth : vkDev.framebufferWidth),
            .height = static_cast<float>(customHeight > 0 ? customHeight : vkDev.framebufferHeight),
            .minDepth = 0.f,
            .maxDepth = 1.f
    };

    const VkRect2D scissor = {
            .offset = {0, 0},
            .extent = {
                    customWidth > 0 ? customWidth : vkDev.framebufferWidth,
                    customHeight > 0 ? customHeight : vkDev.framebufferHeight
            }
    };

    const VkPipelineViewportStateCreateInfo viewportState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor
    };

    const VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .lineWidth = 1.f
    };

    const VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.f
    };

    const VkPipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = useBlending ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    const VkPipelineColorBlendStateCreateInfo colorBlending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
            .blendConstants = { 0.f, 0.f, 0.f, 0.f}
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = static_cast<VkBool32>(useDepth ? VK_TRUE : VK_FALSE),
            .depthWriteEnable = static_cast<VkBool32>(useDepth ? VK_TRUE : VK_FALSE),
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .minDepthBounds = 0.f,
            .maxDepthBounds = 1.f
    };

    VkDynamicState dynamicStateElt = VK_DYNAMIC_STATE_SCISSOR;

    const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = 1,
            .pDynamicStates = &dynamicStateElt
    };

    const VkPipelineTessellationStateCreateInfo tessellationState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .patchControlPoints = numPatchControlPoints
    };

    const VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pTessellationState = (topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST) ? &tessellationState : nullptr,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = useDepth ? &depthStencil : nullptr,
            .pColorBlendState = &colorBlending,
            .pDynamicState = dynamicScissorState ? &dynamicState : nullptr,
            .layout = pipelineLayout,
            .renderPass = renderPass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
    };

    VK_CHECK(vkCreateGraphicsPipelines(vkDev.device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, pipeline));

    for (const auto& m: shaderModules)
        vkDestroyShaderModule(vkDev.device, m.shaderModule, nullptr);

    return true;
}

VkResult CreateSemaphore(VkDevice device, VkSemaphore *outSemaphore) {
    const VkSemaphoreCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    return vkCreateSemaphore(device, &createInfo, nullptr, outSemaphore);
}


bool CreateColorAndDepthRenderPass(
        VulkanRenderDevice& vkDev, bool useDepth,
        VkRenderPass* renderPass,
        const RenderPassCreateInfo& ci,
        VkFormat colorFormat
        ) {
    const bool offscreenInt = ci.flags_ & eRenderPassBit_OffscreenInternal;
    const bool first = ci.flags_ & eRenderPassBit_First;
    const bool last = ci.flags_ & eRenderPassBit_Last;

    VkAttachmentDescription colorAttachment = {
            .flags = 0,
            .format = colorFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = offscreenInt ? VK_ATTACHMENT_LOAD_OP_LOAD : (ci.clearColor_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD),
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = first ? VK_IMAGE_LAYOUT_UNDEFINED : (offscreenInt ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
            .finalLayout = last ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    const VkAttachmentReference colorAttachmentRef = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentDescription depthAttachment = {
            .flags = 0,
            .format = useDepth ? FindDepthFormat(vkDev.physicalDevice) : VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = offscreenInt ? VK_ATTACHMENT_LOAD_OP_LOAD : (ci.clearDepth_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD),
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = ci.clearDepth_ ? VK_IMAGE_LAYOUT_UNDEFINED : (offscreenInt ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    const VkAttachmentReference depthAttachmentRef = {
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    if(ci.flags_ & eRenderPassBit_Offscreen)
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<VkSubpassDependency> dependencies = {
        /* VkSubpassDependency */
        {
          .srcSubpass = VK_SUBPASS_EXTERNAL,
          .dstSubpass = 0,
          .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .dependencyFlags = 0
        }
    };

    if(ci.flags_ & eRenderPassBit_Offscreen) {
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Use subpass dependencies for layout transitions
        dependencies.resize(2);

        dependencies[0] = {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT

        };

        dependencies[1] = {
                .srcSubpass = 0,
                .dstSubpass = VK_SUBPASS_EXTERNAL,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT

        };
    }

    const VkSubpassDescription subpass = {
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = useDepth ? &depthAttachmentRef : nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr
    };

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    const VkRenderPassCreateInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = static_cast<uint32_t>(useDepth ? 2 : 1),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies = dependencies.data()
    };

    return (vkCreateRenderPass(vkDev.device, &renderPassInfo, nullptr, renderPass) == VK_SUCCESS);
}


VkCommandBuffer BeginSingleTimeCommands(VulkanRenderDevice &vkDev) {
    VkCommandBuffer commandBuffer;

    const VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = vkDev.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
    };
    vkAllocateCommandBuffers(vkDev.device, &allocateInfo, &commandBuffer);

    const VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
    };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void EndSingleTimeCommands(VulkanRenderDevice &vkDev, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    const VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
    };

    vkQueueSubmit(vkDev.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkDev.graphicsQueue);

    vkFreeCommandBuffers(vkDev.device, vkDev.commandPool, 1, &commandBuffer);
}

void CopyBuffer(VulkanRenderDevice &vkDev, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(vkDev);

    const VkBufferCopy copyRegion = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size
    };

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    EndSingleTimeCommands(vkDev, commandBuffer);
}

void TransitionImageLayout(VulkanRenderDevice &vkDev, VkImage image, VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout, uint32_t layerCount, uint32_t mipLevels) {
    auto commandBuffer = BeginSingleTimeCommands(vkDev);

    TransitionImageLayoutCmd(commandBuffer, image, format, oldLayout, newLayout, layerCount, mipLevels);

    EndSingleTimeCommands(vkDev, commandBuffer);

}

void TransitionImageLayoutCmd(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                              VkImageLayout newLayout, uint32_t layerCount, uint32_t mipLevels) {
    VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = 0,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = mipLevels,
                    .baseArrayLayer = 0,
                    .layerCount = layerCount
            }
    };

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
        (format == VK_FORMAT_D16_UNORM) ||
        (format == VK_FORMAT_X8_D24_UNORM_PACK32) ||
        (format == VK_FORMAT_D32_SFLOAT) ||
        (format == VK_FORMAT_S8_UINT) ||
        (format == VK_FORMAT_D16_UNORM_S8_UINT) ||
        (format == VK_FORMAT_D24_UNORM_S8_UINT)) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (HasStencilComponent(format)) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
        /* Convert back from read-only to updateable */
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
        /* Convert from updateable texture to shader read-only */
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
        /* Convert depth texture from undefined state to depth-stencil buffer */
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }

        /* Wait for render pass to complete */
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0; // VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = 0;
/*
		sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
///		destinationStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
*/
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

        /* Convert back from read-only to color attachment */
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
        /* Convert from updateable texture to shader read-only */
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

        /* Convert back from read-only to depth attachment */
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
        /* Convert from updateable depth texture to shader read-only */
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
    );

}


bool CreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format,
                 VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage &image,
                 VkDeviceMemory &imageMemory, VkImageCreateFlags flags, uint32_t mipLevels) {
    const VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = flags,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = VkExtent3D{.width = width, .height = height, .depth = 1},
            .mipLevels = mipLevels,
            .arrayLayers = (uint32_t) ((flags == VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ? 6 : 1),
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &image));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(device, image, &memoryRequirements);

    const VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = FindMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, properties)
    };
    VK_CHECK(vkAllocateMemory(device, &allocateInfo, nullptr, &imageMemory));

    vkBindImageMemory(device, image, imageMemory, 0);
    return true;
}

bool CreateVolume(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, uint32_t depth,
                  VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, VkImageCreateFlags flags) {
    const VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = flags,
            .imageType = VK_IMAGE_TYPE_3D,
            .format = format,
            .extent = VkExtent3D {.width = width, .height = height, .depth = depth },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &image));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    const VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties)
    };

    VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));

    vkBindImageMemory(device, image, imageMemory, 0);
    return true;
}

bool InitVulkanRenderDevice(VulkanInstance &vk, VulkanRenderDevice &vkDev, uint32_t width, uint32_t height,
                            const std::function<bool(VkPhysicalDevice)>& selector, VkPhysicalDeviceFeatures deviceFeatures) {
    vkDev.framebufferWidth = width;
    vkDev.framebufferHeight = height;

    VK_CHECK(FindSuitablePhysicalDevice(vk.instance, selector, &vkDev.physicalDevice));
    vkDev.graphicsFamily = FindQueueFamilies(vkDev.physicalDevice, VK_QUEUE_GRAPHICS_BIT);
    VK_CHECK(CreateDevice(vkDev.physicalDevice, deviceFeatures, vkDev.graphicsFamily, &vkDev.device));

    vkGetDeviceQueue(vkDev.device, vkDev.graphicsFamily, 0, &vkDev.graphicsQueue);
    if (vkDev.graphicsQueue == nullptr) exit(EXIT_FAILURE);

    VkBool32 presentSupported = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(vkDev.physicalDevice, vkDev.graphicsFamily, vk.surface, &presentSupported);
    if (!presentSupported) exit(EXIT_FAILURE);

    VK_CHECK(CreateSwapchain(vkDev.device, vkDev.physicalDevice, vk.surface, vkDev.graphicsFamily, width, height,
                             &vkDev.swapchain, false));
    const size_t imageCount = CreateSwapchainImages(vkDev.device, vkDev.swapchain, vkDev.swapchainImages,
                                                    vkDev.swapchainImageViews);
    vkDev.commandBuffers.resize(imageCount);

    VK_CHECK(CreateSemaphore(vkDev.device, &vkDev.semaphore));
    VK_CHECK(CreateSemaphore(vkDev.device, &vkDev.renderSemaphore));

    const VkCommandPoolCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = 0,
            .queueFamilyIndex = vkDev.graphicsFamily
    };
    VK_CHECK(vkCreateCommandPool(vkDev.device, &createInfo, nullptr, &vkDev.commandPool));

    const VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = vkDev.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = static_cast<uint32_t>(vkDev.swapchainImages.size())
    };
    VK_CHECK(vkAllocateCommandBuffers(vkDev.device, &allocateInfo, &vkDev.commandBuffers[0]));
    return true;
}

void CopyBufferToImage(VulkanRenderDevice &vkDev, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                       uint32_t layerCount) {
    auto commandBuffer = BeginSingleTimeCommands(vkDev);

    const VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = layerCount
            },
            .imageOffset = VkOffset3D{.x = 0, .y = 0, .z = 0},
            .imageExtent = VkExtent3D{.width = width, .height = height, .depth = 1}
    };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommands(vkDev, commandBuffer);
}

bool CreateDepthSampler(VkDevice device, VkSampler* sampler) {
    VkSamplerCreateInfo si = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .maxAnisotropy = 1.0f,
            .minLod = 0.0f,
            .maxLod = 1.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
    };

    return (vkCreateSampler(device, &si, nullptr, sampler) == VK_SUCCESS);
}

bool CreateUniformBuffer(VulkanRenderDevice& vkDev, VkBuffer& buffer, VkDeviceMemory& bufferMemory, VkDeviceSize bufferSize) {
    return CreateBuffer(vkDev.device, vkDev.physicalDevice, bufferSize,
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        buffer, bufferMemory);
}

void UploadBufferData(VulkanRenderDevice& vkDev, const VkDeviceMemory& bufferMemory, VkDeviceSize deviceOffset, const void* data, const size_t dataSize) {
    void* mappedData = nullptr;
    vkMapMemory(vkDev.device, bufferMemory, deviceOffset, dataSize, 0, &mappedData);
    memcpy(mappedData, data, dataSize);
    vkUnmapMemory(vkDev.device, bufferMemory);
}

bool
CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView *imageView,
                VkImageViewType viewType, uint32_t layerCount, uint32_t mipLevels) {
    const VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = viewType,
            .format = format,
            .subresourceRange = {
                    .aspectMask = aspectFlags,
                    .baseMipLevel = 0,
                    .levelCount = mipLevels,
                    .baseArrayLayer = 0,
                    .layerCount = layerCount,

            }
    };

    return (vkCreateImageView(device, &viewInfo, nullptr, imageView) == VK_SUCCESS);
}

void DestroyVulkanTexture(VkDevice device, VulkanTexture &texture) {
    DestroyVulkanImage(device, texture.image);
    vkDestroySampler(device, texture.sampler, nullptr);
}

uint32_t BytesPerTexFormat(VkFormat fmt) {
    switch (fmt)
    {
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8_UNORM:
            return 1;
        case VK_FORMAT_R16_SFLOAT:
            return 2;
        case VK_FORMAT_R16G16_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16_SNORM:
            return 4;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return 4;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 4 * sizeof(uint16_t);
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 4 * sizeof(float);
        default:
            break;
    }
    return 0;
}

bool UpdateTextureImage(VulkanRenderDevice& vkDev, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t texWidth, uint32_t texHeight, VkFormat texFormat, uint32_t layerCount, const void* imageData, VkImageLayout sourceImageLayout) {
    uint32_t bytesPerPixel = BytesPerTexFormat(texFormat);

    VkDeviceSize layerSize = texWidth * texHeight * bytesPerPixel;
    VkDeviceSize imageSize = layerSize * layerCount;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(vkDev.device, vkDev.physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    UploadBufferData(vkDev, stagingBufferMemory, 0, imageData, imageSize);

    TransitionImageLayout(vkDev, textureImage, texFormat, sourceImageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
    CopyBufferToImage(vkDev, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), layerCount);
    TransitionImageLayout(vkDev, textureImage, texFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerCount);

    vkDestroyBuffer(vkDev.device, stagingBuffer, nullptr);
    vkFreeMemory(vkDev.device, stagingBufferMemory, nullptr);

    return true;
}

void DestroyVulkanImage(VkDevice device, VulkanImage &image) {
    vkDestroyImageView(device, image.imageView, nullptr);
    vkDestroyImage(device, image.image, nullptr);
    vkFreeMemory(device, image.imageMemory, nullptr);
}

bool InitVulkanRenderDeviceWithCompute(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, VkPhysicalDeviceFeatures deviceFeatures) {
    vkDev.framebufferWidth = width;
    vkDev.framebufferHeight = height;

    VK_CHECK(FindSuitablePhysicalDevice(vk.instance, &IsDeviceSuitable, &vkDev.physicalDevice));
    vkDev.graphicsFamily = FindQueueFamilies(vkDev.physicalDevice, VK_QUEUE_GRAPHICS_BIT);
    vkDev.computeFamily = FindQueueFamilies(vkDev.physicalDevice, VK_QUEUE_COMPUTE_BIT);
    //	VK_CHECK(vkGetBestComputeQueue(vkDev.physicalDevice, &vkDev.computeFamily));
    VK_CHECK(CreateDeviceWithCompute(vkDev.physicalDevice, deviceFeatures, vkDev.graphicsFamily, vkDev.computeFamily, &vkDev.device));

    vkDev.deviceQueueIndices.push_back(vkDev.graphicsFamily);
    if (vkDev.graphicsFamily != vkDev.computeFamily)
        vkDev.deviceQueueIndices.push_back(vkDev.computeFamily);

    vkGetDeviceQueue(vkDev.device, vkDev.graphicsFamily, 0, &vkDev.graphicsQueue);
    if (vkDev.graphicsQueue == nullptr)
        exit(EXIT_FAILURE);

    vkGetDeviceQueue(vkDev.device, vkDev.computeFamily, 0, &vkDev.computeQueue);
    if (vkDev.computeQueue == nullptr)
        exit(EXIT_FAILURE);

    VkBool32 presentSupported = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(vkDev.physicalDevice, vkDev.graphicsFamily, vk.surface, &presentSupported);
    if (!presentSupported)
        exit(EXIT_FAILURE);

    VK_CHECK(CreateSwapchain(vkDev.device, vkDev.physicalDevice, vk.surface, vkDev.graphicsFamily, width, height, &vkDev.swapchain));
    const size_t imageCount = CreateSwapchainImages(vkDev.device, vkDev.swapchain, vkDev.swapchainImages, vkDev.swapchainImageViews);
    vkDev.commandBuffers.resize(imageCount);

    VK_CHECK(CreateSemaphore(vkDev.device, &vkDev.semaphore));
    VK_CHECK(CreateSemaphore(vkDev.device, &vkDev.renderSemaphore));

    const VkCommandPoolCreateInfo cpi =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = vkDev.graphicsFamily
    };

    VK_CHECK(vkCreateCommandPool(vkDev.device, &cpi, nullptr, &vkDev.commandPool));


    const VkCommandBufferAllocateInfo ai =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = vkDev.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(vkDev.swapchainImages.size()),
    };

    VK_CHECK(vkAllocateCommandBuffers(vkDev.device, &ai, &vkDev.commandBuffers[0]));

    {
        // Create compute command pool
        const VkCommandPoolCreateInfo cpi1 =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, /* Allow command from this pool buffers to be reset*/
            .queueFamilyIndex = vkDev.computeFamily
        };
        VK_CHECK(vkCreateCommandPool(vkDev.device, &cpi1, nullptr, &vkDev.computeCommandPool));

        const VkCommandBufferAllocateInfo ai1 =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = vkDev.computeCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VK_CHECK(vkAllocateCommandBuffers(vkDev.device, &ai1, &vkDev.computeCommandBuffer));
    }

    vkDev.useCompute = true;

    return true;
}

bool InitVulkanRenderDevice2WithCompute(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, std::function<bool(VkPhysicalDevice)> selector, VkPhysicalDeviceFeatures2 deviceFeatures2, bool supportScreenshots) {
    vkDev.framebufferWidth = width;
    vkDev.framebufferHeight = height;

    VK_CHECK(FindSuitablePhysicalDevice(vk.instance, selector, &vkDev.physicalDevice));
    vkDev.graphicsFamily = FindQueueFamilies(vkDev.physicalDevice, VK_QUEUE_GRAPHICS_BIT);
//	VK_CHECK(createDevice2(vkDev.physicalDevice, deviceFeatures2, vkDev.graphicsFamily, &vkDev.device));
//	VK_CHECK(vkGetBestComputeQueue(vkDev.physicalDevice, &vkDev.computeFamily));
    vkDev.computeFamily = FindQueueFamilies(vkDev.physicalDevice, VK_QUEUE_COMPUTE_BIT);
    VK_CHECK(CreateDevice2WithCompute(vkDev.physicalDevice, deviceFeatures2, vkDev.graphicsFamily, vkDev.computeFamily, &vkDev.device));

    vkGetDeviceQueue(vkDev.device, vkDev.graphicsFamily, 0, &vkDev.graphicsQueue);
    if (vkDev.graphicsQueue == nullptr)
        exit(EXIT_FAILURE);

    vkGetDeviceQueue(vkDev.device, vkDev.computeFamily, 0, &vkDev.computeQueue);
    if (vkDev.computeQueue == nullptr)
        exit(EXIT_FAILURE);

    VkBool32 presentSupported = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(vkDev.physicalDevice, vkDev.graphicsFamily, vk.surface, &presentSupported);
    if (!presentSupported)
        exit(EXIT_FAILURE);

    VK_CHECK(CreateSwapchain(vkDev.device, vkDev.physicalDevice, vk.surface, vkDev.graphicsFamily, width, height, &vkDev.swapchain, supportScreenshots));
    const size_t imageCount = CreateSwapchainImages(vkDev.device, vkDev.swapchain, vkDev.swapchainImages, vkDev.swapchainImageViews);
    vkDev.commandBuffers.resize(imageCount);

    VK_CHECK(CreateSemaphore(vkDev.device, &vkDev.semaphore));
    VK_CHECK(CreateSemaphore(vkDev.device, &vkDev.renderSemaphore));

    const VkCommandPoolCreateInfo cpi =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = vkDev.graphicsFamily
    };

    VK_CHECK(vkCreateCommandPool(vkDev.device, &cpi, nullptr, &vkDev.commandPool));

    const VkCommandBufferAllocateInfo ai =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = vkDev.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(vkDev.swapchainImages.size()),
    };
    VK_CHECK(vkAllocateCommandBuffers(vkDev.device, &ai, &vkDev.commandBuffers[0]));

    {
        // Create compute command pool
        const VkCommandPoolCreateInfo cpi1 =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, /* Allow command from this pool buffers to be reset*/
            .queueFamilyIndex = vkDev.computeFamily
        };
        VK_CHECK(vkCreateCommandPool(vkDev.device, &cpi1, nullptr, &vkDev.computeCommandPool));

        const VkCommandBufferAllocateInfo ai1 =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = vkDev.computeCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VK_CHECK(vkAllocateCommandBuffers(vkDev.device, &ai1, &vkDev.computeCommandBuffer));
    }

    vkDev.useCompute = true;

    return true;
}

bool InitVulkanRenderDevice3(VulkanInstance& vk, VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, const VulkanContextFeatures& ctxFeatures) {
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
            .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            .descriptorBindingVariableDescriptorCount = VK_TRUE,
            .runtimeDescriptorArray = VK_TRUE,
    };

    VkPhysicalDeviceFeatures deviceFeatures = {
            /* for wireframe outlines */
            .geometryShader = (VkBool32)(ctxFeatures.geometryShader_ ? VK_TRUE : VK_FALSE),
            /* for tesselation experiments */
            .tessellationShader = (VkBool32)(ctxFeatures.tessellationShader_ ? VK_TRUE : VK_FALSE),
            /* for indirect instanced rendering */
            .multiDrawIndirect = VK_TRUE,
            .drawIndirectFirstInstance = VK_TRUE,
            /* for OIT and general atomic operations */
            .vertexPipelineStoresAndAtomics = (VkBool32)(ctxFeatures.vertexPipelineStoresAndAtomics_ ? VK_TRUE : VK_FALSE),
            .fragmentStoresAndAtomics = (VkBool32)(ctxFeatures.fragmentStoresAndAtomics_ ? VK_TRUE : VK_FALSE),
            /* for arrays of textures */
            .shaderSampledImageArrayDynamicIndexing = VK_TRUE,
            /* for GL <-> VK material shader compatibility */
            .shaderInt64 =  VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 deviceFeatures2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &physicalDeviceDescriptorIndexingFeatures,
            .features = deviceFeatures  /*  */
    };

    return InitVulkanRenderDevice2WithCompute(vk, vkDev, width, height, IsDeviceSuitable, deviceFeatures2, ctxFeatures.supportScreenshots_);
}

void DestroyVulkanRenderDevice(VulkanRenderDevice &vkDev) {
    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++)
        vkDestroyImageView(vkDev.device, vkDev.swapchainImageViews[i], nullptr);

    vkDestroySwapchainKHR(vkDev.device, vkDev.swapchain, nullptr);

    vkDestroyCommandPool(vkDev.device, vkDev.commandPool, nullptr);

    vkDestroySemaphore(vkDev.device, vkDev.semaphore, nullptr);
    vkDestroySemaphore(vkDev.device, vkDev.renderSemaphore, nullptr);

    vkDestroyDevice(vkDev.device, nullptr);
}

void DestroyVulkanInstance(VulkanInstance &vk) {
    vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);

    vkDestroyDebugReportCallbackEXT(vk.instance, vk.reportCallback, nullptr);
    vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.messenger, nullptr);

    vkDestroyInstance(vk.instance, nullptr);
}

bool CreateColorAndDepthFramebuffers(VulkanRenderDevice& vkDev, VkRenderPass renderPass, VkImageView depthImageView, std::vector<VkFramebuffer>& swapchainFramebuffers) {
    swapchainFramebuffers.resize(vkDev.swapchainImageViews.size());

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++) {
        std::array<VkImageView, 2> attachments = {
                vkDev.swapchainImageViews[i],
                depthImageView
        };

        const VkFramebufferCreateInfo framebufferInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderPass = renderPass,
                .attachmentCount = static_cast<uint32_t>((depthImageView == VK_NULL_HANDLE) ? 1 : 2),
                .pAttachments = attachments.data(),
                .width = vkDev.framebufferWidth,
                .height = vkDev.framebufferHeight,
                .layers = 1
        };

        VK_CHECK(vkCreateFramebuffer(vkDev.device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]));
    }

    return true;
}

bool CreateTextureSampler(VkDevice device, VkSampler *sampler, VkFilter minFilter, VkFilter maxFilter,
                          VkSamplerAddressMode addressMode) {
    const VkSamplerCreateInfo samplerCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = addressMode,    //VK_SAMPLER_ADDRESS_MODE_REPEAT
            .addressModeV = addressMode,    //VK_SAMPLER_ADDRESS_MODE_REPEAT
            .addressModeW = addressMode,    //VK_SAMPLER_ADDRESS_MODE_REPEAT
            .mipLodBias = 0.f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE
    };

    return (vkCreateSampler(device, &samplerCreateInfo, nullptr, sampler) == VK_SUCCESS);
}

bool CreateDescriptorPool(VulkanRenderDevice& vkDev, uint32_t uniformBufferCount, uint32_t storageBufferCount, uint32_t samplerCount, VkDescriptorPool* descriptorPool) {
    const auto imageCount = static_cast<uint32_t>(vkDev.swapchainImages.size());

    std::vector<VkDescriptorPoolSize> poolSizes;

    if(uniformBufferCount)
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = imageCount * uniformBufferCount
        });

    if(storageBufferCount)
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = imageCount * storageBufferCount
        });

    if(samplerCount)
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = imageCount * samplerCount
        });

    const VkDescriptorPoolCreateInfo poolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = static_cast<uint32_t>(imageCount),
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.empty() ? nullptr : poolSizes.data()
    };

    return (vkCreateDescriptorPool(vkDev.device, &poolCreateInfo, nullptr, descriptorPool) == VK_SUCCESS);
}

bool CreateDepthResources(VulkanRenderDevice& vkDev, uint32_t width, uint32_t height, VulkanImage& depth) {
    VkFormat depthFormat = FindDepthFormat(vkDev.physicalDevice);

    if(!CreateImage(vkDev.device, vkDev.physicalDevice, width, height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depth.image, depth.imageMemory))
        return false;

    if(!CreateImageView(vkDev.device, depth.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, &depth.imageView))
        return false;

    TransitionImageLayout(vkDev, depth.image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    return true;
}

bool CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout dsLayout, VkPipelineLayout* pipelineLayout) {
    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &dsLayout,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
    };

    return (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, pipelineLayout) == VK_SUCCESS);
}

bool CreateTextureImageFromData(
        VulkanRenderDevice& vkDev,
        VkImage& textureImage, VkDeviceMemory& textureImageMemory,
        void* imageData, uint32_t texWidth, uint32_t texHeight,
        VkFormat texFormat,
        uint32_t layerCount, VkImageCreateFlags flags
        ) {

    CreateImage(vkDev.device,
                vkDev.physicalDevice,
                texWidth, texHeight, texFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                textureImage, textureImageMemory,
                flags);

    return UpdateTextureImage(vkDev, textureImage, textureImageMemory, texWidth, texHeight, texFormat, layerCount, imageData);
}

bool CreateTextureImage(VulkanRenderDevice& vkDev, const char* filename, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t* outTexWidth, uint32_t* outTexHeight) {
    int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    if(!pixels) {
        std::fprintf(stderr, "Failed to load [%s] texture\n", filename);
        return false;
    }

    bool result = CreateTextureImageFromData(vkDev, textureImage, textureImageMemory, pixels, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM);

    stbi_image_free(pixels);

    if(outTexWidth && outTexHeight) {
        *outTexWidth = (uint32_t)texWidth;
        *outTexHeight = (uint32_t)texHeight;
    }

    return result;
}

static void float24to32(int w, int h, const float* img24, float *img32) {
    const int numPixels = w * h;
    for(int i = 0; i != numPixels; i++) {
        *img32++ = *img24++;
        *img32++ = *img24++;
        *img32++ = *img24++;
        *img32++ = 1.0f;
    }
}

bool CreateCubeTextureImage(VulkanRenderDevice& vkDev, const char* filename, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t* width, uint32_t* height) {
    int w, h, comp;
    const float* img = stbi_loadf(filename, &w, &h, &comp, 3);
    std::vector<float> img32(w * h * 4);

    float24to32(w, h, img, img32.data());

    if(!img) {
        printf("Failed to load [%s] texture\n", filename); fflush(stdout);
        return false;
    }

    stbi_image_free((void*)img);

    Bitmap in(w, h, 4, eBitmapFormat::Float, img32.data());
    Bitmap out = ConvertEquirectangularMapToVerticalCross(in);

    Bitmap cube = ConvertVerticalCrossToCubeMapFaces(out);

    if (width && height)
    {
        *width = w;
        *height = h;
    }

    return CreateTextureImageFromData(vkDev, textureImage, textureImageMemory,
                                      cube.mData.data(), cube.mW, cube.mH,
                                      VK_FORMAT_R32G32B32A32_SFLOAT,
                                      6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
}

size_t AllocateVertexBuffer(VulkanRenderDevice& vkDev,
                            VkBuffer* storageBuffer,
                            VkDeviceMemory* storageBufferMemory,
                            size_t vertexDataSize,
                            const void* vertexData,
                            size_t indexDataSize,
                            const void* indexData) {
    VkDeviceSize bufferSize = vertexDataSize + indexDataSize;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(vkDev.device, vkDev.physicalDevice, bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(vkDev.device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertexData, vertexDataSize);
    memcpy((unsigned char*)data + vertexDataSize, indexData, indexDataSize);
    vkUnmapMemory(vkDev.device, stagingBufferMemory);

    CreateBuffer(vkDev.device, vkDev.physicalDevice, bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 *storageBuffer, *storageBufferMemory);
    CopyBuffer(vkDev, stagingBuffer, *storageBuffer, bufferSize);

    vkDestroyBuffer(vkDev.device, stagingBuffer, nullptr);
    vkFreeMemory(vkDev.device, stagingBufferMemory, nullptr);

    return bufferSize;
}

bool CreateTexturedVertexBuffer(
        VulkanRenderDevice& vkDev, const char* filename,
        VkBuffer* storageBuffer, VkDeviceMemory* storageBufferMemory,
        size_t* vertexBufferSize, size_t* indexBufferSize
        ) {
    const aiScene* scene = aiImportFile(filename, aiProcess_Triangulate);
    if(!scene || !scene->HasMeshes()) {
        std::fprintf(stderr, "Unable to load %s\n", filename);
        exit(255);
    }

    const aiMesh* mesh = scene->mMeshes[0];
    struct VertexData {
        vec3 pos;
        vec2 tc;
    };

    std::vector<VertexData> vertices;
    for(unsigned i = 0; i != mesh->mNumVertices; i++) {
        const aiVector3D v = mesh->mVertices[i];
        const aiVector3D t = mesh->mTextureCoords[0][i];
        vertices.push_back({.pos = vec3(v.x, v.z, v.y), .tc = vec2(t.x, t.y)});
    }

    std::vector<unsigned int> indices;
    for(unsigned i = 0; i != mesh->mNumFaces; i++) {
        for(unsigned  j = 0; j != 3; j++) {
            indices.push_back(mesh->mFaces[i].mIndices[j]);
        }
    }
    aiReleaseImport(scene);

    *vertexBufferSize = sizeof(VertexData) * vertices.size();
    *indexBufferSize = sizeof(uint32_t) * indices.size();

    AllocateVertexBuffer(vkDev, storageBuffer, storageBufferMemory, *vertexBufferSize, vertices.data(), *indexBufferSize, indices.data());

    return true;
}

void UpdateTextureInDescriptorSetArray(VulkanRenderDevice& vkDev, VkDescriptorSet ds, VulkanTexture t, uint32_t textureIndex, uint32_t bindingIdx)
{
    const VkDescriptorImageInfo imageInfo = {
            .sampler = t.sampler,
            .imageView = t.image.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet writeSet = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ds,
            .dstBinding = bindingIdx,
            .dstArrayElement = textureIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo
    };

    vkUpdateDescriptorSets(vkDev.device, 1, &writeSet, 0, nullptr);
}