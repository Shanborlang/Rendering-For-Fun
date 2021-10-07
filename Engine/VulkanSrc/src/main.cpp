#include "shared/vkFramework/VulkanApp.h"
#include "shared/vkRenderers/VulkanClear.h"
#include "shared/vkRenderers/VulkanFinish.h"
#include "shared/vkRenderers/VulkanMultiMeshRenderer.h"

const uint32_t kScreenWidth = 1280;
const uint32_t kScreenHeight = 720;

GLFWwindow* window;

VulkanInstance vk;
VulkanRenderDevice vkDev;

std::unique_ptr<MultiMeshRenderer> multiRenderer;
std::unique_ptr<VulkanClear> clear;
std::unique_ptr<VulkanFinish> finish;

struct MouseState {
    glm::vec2 pos = glm::vec2(0.f);
    bool pressedLeft = false;
}mouseState;

CameraPositioner_FirstPerson positioner_firstPerson(glm::vec3(0.0f, -5.0f, 15.0f), vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f));
Camera camera = Camera(positioner_firstPerson);

void initVulkan() {
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

    const int kNumFilpbookFrames = 100;

    std::vector<std::string> textureFiles;
    for(uint32_t j = 0; j < 3; j++) {
        for(uint32_t i = 0; i != kNumFilpbookFrames; i++) {
            char fname[1024];
            snprintf(fname, sizeof(fname), "../../../data/anim/explosion/explosion%02u-frame%03u.tga", j, j+1);
            textureFiles.emplace_back(fname);
        }
    }

    clear = std::make_unique<VulkanClear>(vkDev, VulkanImage());
    finish = std::make_unique<VulkanFinish>(vkDev, VulkanImage());

    multiRenderer = std::make_unique<MultiMeshRenderer>(
            vkDev,
            "../../../data/meshes/test.meshes",
            "../../../data/meshes/test.meshes.drawdata",
            "",
            "../../../data/shaders/Vk01.vert","../../../data/shaders/Vk01.frag");
}

void terminateVulkan()
{
    finish = nullptr;
    clear = nullptr;

    multiRenderer = nullptr;

    DestroyVulkanRenderDevice(vkDev);
    DestroyVulkanInstance(vk);
}

void update3D(uint32_t imageIndex)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    const float ratio = width / (float)height;

    const mat4 m1 = glm::rotate(mat4(1.f), glm::pi<float>(), vec3(1, 0, 0));
    const mat4 p = glm::perspective(45.0f, ratio, 0.1f, 1000.0f);

    const mat4 view = camera.GetViewMatrix();
    const mat4 mtx = p * view * m1;

    multiRenderer->UpdateUniformBuffer(vkDev, imageIndex, mtx);
}

void composeFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    clear->FillCommandBuffer(commandBuffer, imageIndex);
    multiRenderer->FillCommandBuffer(commandBuffer, imageIndex);
    finish->FillCommandBuffer(commandBuffer, imageIndex);
}

int main() {

    window = initVulkanApp(kScreenWidth, kScreenHeight);

    glfwSetKeyCallback(
            window,
            [](GLFWwindow* window, int key, int scancode, int action, int mods)
            {
                const bool pressed = action != GLFW_RELEASE;
                if (key == GLFW_KEY_ESCAPE && pressed)
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                if (key == GLFW_KEY_W)
                    positioner_firstPerson.mMovement.mForward = pressed;
                if (key == GLFW_KEY_S)
                    positioner_firstPerson.mMovement.mBackward = pressed;
                if (key == GLFW_KEY_A)
                    positioner_firstPerson.mMovement.mLeft = pressed;
                if (key == GLFW_KEY_D)
                    positioner_firstPerson.mMovement.mRight = pressed;
                if (key == GLFW_KEY_Q)
                    positioner_firstPerson.mMovement.mUp = pressed;
                if (key == GLFW_KEY_E)
                    positioner_firstPerson.mMovement.mDown = pressed;
                if (key == GLFW_KEY_SPACE)
                    positioner_firstPerson.SetUpVector(vec3(0.0f, 1.0f, 0.0f));
            }
    );

    initVulkan();

    double timeStamp = glfwGetTime();
    float deltaSeconds = 0.0f;

    while (!glfwWindowShouldClose(window))
    {
        positioner_firstPerson.Update(deltaSeconds, mouseState.pos, mouseState.pressedLeft);

        const double newTimeStamp = glfwGetTime();
        deltaSeconds = static_cast<float>(newTimeStamp - timeStamp);
        timeStamp = newTimeStamp;

        drawFrame(vkDev, &update3D, &composeFrame);

        glfwPollEvents();
    }

    terminateVulkan();

    glfwTerminate();

    glslang_finalize_process();

    return 0;
}