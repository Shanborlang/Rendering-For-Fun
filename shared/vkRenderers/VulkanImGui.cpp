#include "VulkanImGui.h"
#include "../EasyProfilerWrapper.h"

#include <imgui/imgui.h>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

constexpr uint32_t ImGuiVtxBufferSize = 512 * 1024 * sizeof(ImDrawVert);
constexpr uint32_t ImGuiIdxBufferSize = 512 * 1024 * sizeof(uint32_t);

void AddImGuiItem(uint32_t width, uint32_t height, VkCommandBuffer commandBuffer, const ImDrawCmd* pcmd, ImVec2 clipOff, ImVec2 clipScale, int idxOffset, int vtxOffset,
                  const std::vector<VulkanTexture>& textures, VkPipelineLayout pipelineLayout) {
    if(pcmd->UserCallback)
        return;

    // Project scissor/clipping rectangles into framebuffer space
    ImVec4 clipRect;
    clipRect.x = (pcmd->ClipRect.x - clipOff.x) * clipScale.x;
    clipRect.y = (pcmd->ClipRect.y - clipOff.y) * clipScale.y;
    clipRect.z = (pcmd->ClipRect.z - clipOff.x) * clipScale.x;
    clipRect.w = (pcmd->ClipRect.w - clipOff.y) * clipScale.y;

    if (clipRect.x < width && clipRect.y < height && clipRect.z >= 0.0f && clipRect.w >= 0.0f)
    {
        if (clipRect.x < 0.0f) clipRect.x = 0.0f;
        if (clipRect.y < 0.0f) clipRect.y = 0.0f;
        // Apply scissor/clipping rectangle
        const VkRect2D scissor = {
                .offset = { .x = (int32_t)(clipRect.x), .y = (int32_t)(clipRect.y) },
                .extent = { .width = (uint32_t)(clipRect.z - clipRect.x), .height = (uint32_t)(clipRect.w - clipRect.y) }
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // this is added in the Chapter 6: Using descriptor indexing in Vulkan to render an ImGui UI
        if (textures.size() > 0)
        {
            uint32_t texture = (uint32_t)(intptr_t)pcmd->TextureId;
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), (const void*)&texture);
        }

        vkCmdDraw(commandBuffer,
                  pcmd->ElemCount,
                  1,
                  pcmd->IdxOffset + idxOffset,
                  pcmd->VtxOffset + vtxOffset);
    }
}

bool CreateFontTexture(ImGuiIO& io, const char* fontFile, VulkanRenderDevice& vkDev, VkImage& textureImage, VkDeviceMemory& textureImageMemory) {
    // Build texture atlas
    ImFontConfig cfg = ImFontConfig();
    cfg.FontDataOwnedByAtlas = false;
    cfg.RasterizerMultiply = 1.5f;
    cfg.SizePixels = 768.0f / 32.0f;
    cfg.PixelSnapH = true;
    cfg.OversampleH = 4;
    cfg.OversampleV = 4;
    ImFont* Font = io.Fonts->AddFontFromFileTTF(fontFile, cfg.SizePixels, &cfg);

    unsigned  char* pixels = nullptr;
    int texWidth = 1, texHeight = 1;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texWidth, &texHeight);

    if(!pixels || !CreateTextureImageFromData(vkDev, textureImage, textureImageMemory, pixels, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM)) {
        std::fprintf(stderr, "Failed to load textures\n");
        fflush(stdout);
        return false;
    }

    io.Fonts->TexID = (ImTextureID)0;
    io.FontDefault = Font;
    io.DisplayFramebufferScale = ImVec2(1, 1);

    return true;
}

ImGuiRenderer::ImGuiRenderer(VulkanRenderDevice &vkDev)
: RendererBase(vkDev, VulkanImage()){
    // Resource loading
    ImGuiIO& io = ImGui::GetIO();
    CreateFontTexture(io, "../../../data/fonts/OpenSans-Light.ttf", vkDev, mFont.image, mFont.imageMemory);

    CreateImageView(vkDev.device, mFont.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &mFont.imageView);
    CreateTextureSampler(vkDev.device, &mFontSampler);

    // Buffer allocation
    const size_t imgCount = vkDev.swapchainImages.size();

    mStorageBuffer.resize(imgCount);
    mStorageBufferMemory.resize(imgCount);

    mBufferSize = ImGuiVtxBufferSize + ImGuiIdxBufferSize;

    for(size_t i = 0; i < imgCount; i++) {
        if(!CreateBuffer(vkDev.device, vkDev.physicalDevice, mBufferSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         mStorageBuffer[i], mStorageBufferMemory[i])) {
            std::fprintf(stderr, "ImGuiRenderer: CreateBuffer() failed\n");
            exit(EXIT_FAILURE);
        }
    }

    // Pipeline creation
    if (!CreateColorAndDepthRenderPass(vkDev, false, &mRenderPass, RenderPassCreateInfo()) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, VK_NULL_HANDLE, mSwapchainFramebuffers) ||
        !CreateUniformBuffers(vkDev, sizeof(glm::mat4)) ||
        !CreateDescriptorPool(vkDev, 1, 2, 1, &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout,
                                { "../../../data/shaders/imgui.vert", "../../../data/shaders/imgui.frag" }, &mGraphicsPipeline, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                true, true, true))
    {
        std::fprintf(stderr, "ImGuiRenderer: pipeline creation failed\n");
        exit(EXIT_FAILURE);
    }
}

ImGuiRenderer::ImGuiRenderer(VulkanRenderDevice &vkDev, const std::vector<VulkanTexture> &textures)
: RendererBase(vkDev, VulkanImage()), mExtTextures(textures){
    // Resource loading
    ImGuiIO& io = ImGui::GetIO();
    CreateFontTexture(io, "../../../data/fonts/OpenSans-Light.ttf", vkDev, mFont.image, mFont.imageMemory);

    CreateImageView(vkDev.device, mFont.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &mFont.imageView);
    CreateTextureSampler(vkDev.device, &mFontSampler);

    // Buffer allocation
    const size_t imgCount = vkDev.swapchainImages.size();

    mStorageBuffer.resize(imgCount);
    mStorageBufferMemory.resize(imgCount);

    mBufferSize = ImGuiVtxBufferSize + ImGuiIdxBufferSize;

    for(size_t i = 0; i < imgCount; i++) {
        if(!CreateBuffer(vkDev.device, vkDev.physicalDevice, mBufferSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         mStorageBuffer[i], mStorageBufferMemory[i])) {
            std::fprintf(stderr, "ImGuiRenderer: CreateBuffer() failed\n");
            exit(EXIT_FAILURE);
        }
    }

    // Pipeline creation
    if (!CreateColorAndDepthRenderPass(vkDev, false, &mRenderPass, RenderPassCreateInfo()) ||
        !CreateColorAndDepthFramebuffers(vkDev, mRenderPass, VK_NULL_HANDLE, mSwapchainFramebuffers) ||
        !CreateUniformBuffers(vkDev, sizeof(glm::mat4)) ||
        !CreateDescriptorPool(vkDev, 1, 2, 1 + static_cast<uint32_t>(textures.size()), &mDescriptorPool) ||
        !CreateDescriptorSet(vkDev) ||
        !CreatePipelineLayout(vkDev.device, mDescriptorSetLayout, &mPipelineLayout) ||
        !CreateGraphicsPipeline(vkDev, mRenderPass, mPipelineLayout,
                                { "../../data/shaders/imgui.vert", "../../data/shaders/imgui.frag" }, &mGraphicsPipeline, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                true, true, true))
    {
        std::fprintf(stderr, "ImGuiRenderer: pipeline creation failed\n");
        exit(EXIT_FAILURE);
    }
}

ImGuiRenderer::~ImGuiRenderer() {

}

void ImGuiRenderer::FillCommandBuffer(VkCommandBuffer commandBuffer, size_t currentImage) {
    EASY_FUNCTION()

    BeginRenderPass(commandBuffer, currentImage);

    ImVec2 clipOff = mDrawData->DisplayPos;         // (0, 0) unless using multi viewports
    ImVec2 clipScale = mDrawData->FramebufferScale; // (1, 1) unless using retina display which are often (2, 2)

    int vtxOffset = 0;
    int idxOffset = 0;

    for(int n = 0; n < mDrawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = mDrawData->CmdLists[n];
        for(int cmd = 0; cmd < cmdList->CmdBuffer.Size; cmd++) {
            const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmd];
            AddImGuiItem(mFramebufferWidth, mFramebufferHeight, commandBuffer, pcmd, clipOff, clipScale, idxOffset, vtxOffset, mExtTextures, mPipelineLayout);
        }
        idxOffset += cmdList->IdxBuffer.Size;
        vtxOffset += cmdList->VtxBuffer.Size;
    }
    vkCmdEndRenderPass(commandBuffer);
}

void ImGuiRenderer::UpdateBuffers(VulkanRenderDevice &vkDev, uint32_t currentImage, const ImDrawData *imguiDrawData) {
    mDrawData = imguiDrawData;

    const float L = mDrawData->DisplayPos.x;
    const float R = mDrawData->DisplayPos.x + mDrawData->DisplaySize.x;
    const float T = mDrawData->DisplayPos.y;
    const float B = mDrawData->DisplayPos.y + mDrawData->DisplaySize.y;

    const glm::mat4 inMtx = glm::ortho(L, R, T, B);

    UploadBufferData(vkDev, mUniformBuffersMemory[currentImage], 0, glm::value_ptr(inMtx), sizeof(glm::mat4));

    void* data = nullptr;
    vkMapMemory(vkDev.device, mStorageBufferMemory[currentImage], 0, mBufferSize, 0, &data);

    auto* vtx = reinterpret_cast<ImDrawVert*>(data);
    for(int n = 0; n < mDrawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = mDrawData->CmdLists[n];
        memcpy(vtx, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
        vtx += cmdList->VtxBuffer.Size;
    }

    auto* idx = (uint32_t*)((uint8_t*)data + ImGuiVtxBufferSize);
    for(int n = 0; n < mDrawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = mDrawData->CmdLists[n];
        const auto* src = (const uint16_t*)cmdList->IdxBuffer.Data;

        for(int j = 0; j < cmdList->IdxBuffer.Size; j++)
            *idx++ = (uint32_t)*src++;
    }

    vkUnmapMemory(vkDev.device, mStorageBufferMemory[currentImage]);
}

bool ImGuiRenderer::CreateDescriptorSet(VulkanRenderDevice &vkDev) {
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
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

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++) {
        VkDescriptorSet ds = mDescriptorSets[i];
        const VkDescriptorBufferInfo bufferInfo = {mUniformBuffers[i], 0, sizeof(glm::mat4)};
        const VkDescriptorBufferInfo bufferInfo2 = {mStorageBuffer[i], 0, ImGuiVtxBufferSize};
        const VkDescriptorBufferInfo bufferInfo3 = {mStorageBuffer[i], ImGuiVtxBufferSize, ImGuiIdxBufferSize};
        const VkDescriptorImageInfo imageInfo = {mFontSampler, mFont.imageView,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        const std::array<VkWriteDescriptorSet, 4> descriptorWrites = {
                BufferWriteDescriptorSet(ds, &bufferInfo, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo3, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                ImageWriteDescriptorSet(ds, &imageInfo, 3)
        };

        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0,
                               nullptr);
    }

    return true;
}

bool ImGuiRenderer::CreateMultiDescriptorSet(VulkanRenderDevice &vkDev) {
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
            DescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                                       static_cast<uint32_t>(1 + mExtTextures.size()))
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

    // use the font texture initialized in the constructor
    std::vector<VkDescriptorImageInfo> textureDescriptors = {
            {mFontSampler, mFont.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    };

    for (auto &mExtTexture: mExtTextures) {
        textureDescriptors.push_back({
                                         .sampler = mExtTexture.sampler,
                                         .imageView = mExtTexture.image.imageView,
                                         .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL   /// TODO: select type from VulkanTexture object (GENERAL or SHADER_READ_ONLY_OPTIMAL)
                                     });
    }

    for (size_t i = 0; i < vkDev.swapchainImages.size(); i++) {
        VkDescriptorSet ds = mDescriptorSets[i];
        const VkDescriptorBufferInfo bufferInfo = {mUniformBuffers[i], 0, sizeof(glm::mat4)};
        const VkDescriptorBufferInfo bufferInfo2 = {mStorageBuffer[i], 0, ImGuiVtxBufferSize};
        const VkDescriptorBufferInfo bufferInfo3 = {mStorageBuffer[i], ImGuiVtxBufferSize, ImGuiIdxBufferSize};

        const std::array<VkWriteDescriptorSet, 4> descriptorWrites = {
                BufferWriteDescriptorSet(ds, &bufferInfo, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                BufferWriteDescriptorSet(ds, &bufferInfo3, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                VkWriteDescriptorSet{
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = mDescriptorSets[i],
                        .dstBinding = 3,
                        .dstArrayElement = 0,
                        .descriptorCount = static_cast<uint32_t>(1 + mExtTextures.size()),
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = textureDescriptors.data()
                },
        };

        vkUpdateDescriptorSets(vkDev.device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0,
                               nullptr);
    }

    return true;
}
