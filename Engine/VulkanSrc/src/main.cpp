#include "shared/vkFramework/VulkanApp.h"
#include "shared/vkRenderers/VulkanClear.h"
#include "shared/vkRenderers/VulkanFinish.h"
#include "shared/vkRenderers/VulkanQuadRenderer.h"

const uint32_t kScreenWidth = 1280;
const uint32_t kScreenHeight = 720;

GLFWwindow* window;

VulkanInstance vk;
VulkanRenderDevice vkDev;

std::unique_ptr<VulkanQuadRenderer> quadRenderer;
std::unique_ptr<VulkanClear> clear;
std::unique_ptr<VulkanFinish> finish;

const double kAnimationFPS = 60.0;
const uint32_t kNumFlipbookFrames = 100;

struct AnimationState {
    vec2 position = vec2(0);
    double startTime = 0;
    uint32_t textureIndex = 0;
    uint32_t flipbookOffset = 0;
};

std::vector<AnimationState> animations;

void UpdateAnimations() {
    for(size_t i = 0; i < animations.size();) {
        animations[i].textureIndex = animations[i].flipbookOffset + (uint32_t)(kAnimationFPS * ((glfwGetTime() - animations[i].startTime)));
        if(animations[i].textureIndex - animations[i].flipbookOffset > kNumFlipbookFrames)
            animations.erase(animations.begin() - 1);
        else
            i++;
    }
}

void FillQuadBuffer(VulkanRenderDevice& vkDev, VulkanQuadRenderer& quadRenderer, size_t currentImage) {
    const float aspect = (float)vkDev.framebufferWidth / (float)vkDev.framebufferHeight;
    const float quadSize = 0.5f;

    quadRenderer.Clear();
    quadRenderer.Quad(-quadSize, -quadSize * aspect, quadSize, quadSize * aspect);
    quadRenderer.UpdateBuffer(vkDev, currentImage);
}

bool initVulkan() {
    CreateInstance(&vk.instance);

    if (!SetupDebugCallbacks(vk.instance, &vk.messenger, &vk.reportCallback))
        exit(EXIT_FAILURE);

    if (glfwCreateWindowSurface(vk.instance, window, nullptr, &vk.surface))
        exit(EXIT_FAILURE);

    VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
            .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            .descriptorBindingVariableDescriptorCount = VK_TRUE,
            .runtimeDescriptorArray = VK_TRUE
    };

    const VkPhysicalDeviceFeatures deviceFeatures = {
            .shaderSampledImageArrayDynamicIndexing = VK_TRUE
    };

    const VkPhysicalDeviceFeatures2 deviceFeatures2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &physicalDeviceDescriptorIndexingFeatures,
            .features = deviceFeatures
    };

    if(!InitVulkanRenderDevice2(vk, vkDev, kScreenWidth, kScreenHeight, IsDeviceSuitable, deviceFeatures2))
        exit(EXIT_FAILURE);

    std::vector<std::string> textureFiles;
    for(uint32_t j = 0; j < 3; j++) {
        for(uint32_t i = 0; i != kNumFlipbookFrames; i++) {
            char fname[1024];
            snprintf(fname, sizeof(fname), "../../../data/anim/explosion/explosion%02u-frame%03u.tga", j, i + 1);
            textureFiles.emplace_back(fname);
        }
    }

    quadRenderer = std::make_unique<VulkanQuadRenderer>(vkDev, textureFiles);

    for(size_t i = 0; i < vkDev.swapchainImages.size(); i++) {
        FillQuadBuffer(vkDev, *quadRenderer, i);
    }

    VulkanImage nullTexture = { .image = VK_NULL_HANDLE, .imageView = VK_NULL_HANDLE };
    clear = std::make_unique<VulkanClear>(vkDev, nullTexture);
    finish = std::make_unique<VulkanFinish>(vkDev, nullTexture);

    return VK_SUCCESS;
}

void terminateVulkan()
{
    finish = nullptr;
    clear = nullptr;

    quadRenderer = nullptr;

    DestroyVulkanRenderDevice(vkDev);
    DestroyVulkanInstance(vk);
}

float mouseX = 0;
float mouseY = 0;

void composeFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    UpdateAnimations();

    clear->FillCommandBuffer(commandBuffer, imageIndex);

    for(auto & animation : animations) {
        quadRenderer->PushConstants(commandBuffer, animation.textureIndex, animation.position);
        quadRenderer->FillCommandBuffer(commandBuffer, imageIndex);
    }

    finish->FillCommandBuffer(commandBuffer, imageIndex);
}

int main() {

    window = initVulkanApp(kScreenWidth, kScreenHeight);

    glfwSetCursorPosCallback(window,
                             [](GLFWwindow* window, double xpos, double ypos) {
        mouseX = (float)xpos;
        mouseY = (float)ypos;
    });

    glfwSetMouseButtonCallback(window,
                               [](GLFWwindow* window, int button, int action, int mods) {
       if(button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
           const float mx = (mouseX / (float)vkDev.framebufferWidth ) * 2.0f - 1.0f;
           const float my = (mouseY / (float)vkDev.framebufferHeight) * 2.0f - 1.0f;

           animations.push_back(
                   AnimationState {
                       .position = vec2(mx, my),
                       .startTime = glfwGetTime(),
                       .textureIndex = 0,
                       .flipbookOffset = kNumFlipbookFrames * (uint32_t)(rand() % 3)
                   });
       }
    });

    glfwSetKeyCallback(window,
                       [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
    });

    initVulkan();

    printf("Texture loaded. Click to make an explosion\n");


    while (!glfwWindowShouldClose(window))
    {

        drawFrame(vkDev, [](uint32_t){}, composeFrame);

        glfwPollEvents();
    }

    terminateVulkan();

    glfwTerminate();

    glslang_finalize_process();

    return 0;
}