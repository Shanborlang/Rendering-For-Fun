#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "shared/scene/vec4.h"

enum MaterialFlags {
    sMaterialFlags_CastShadow = 0x1,
    sMaterialFlags_ReceiveShadow = 0x2,
    sMaterialFlags_Transparent = 0x4
};

constexpr const uint64_t INVALID_TEXTURE = 0xFFFFFFFF;

struct PACKED_STRUCT MaterialDescription final {
    gpuvec4 mEmissiveColor = {0.f, 0.f, 0.f, 0.f};
    gpuvec4 mAlbedoColor = { 1.f, 1.f, 1.f, 1.f};

    // UV anisotropic roughness (isotropic lighting models use only the first value). ZW values are ignored
    gpuvec4 mRoughness = { 1.f, 1.f, 0.f, 0.f };
    float mTransparencyFactor = 1.f;
    float mAlphaTest = 0.f;
    float mMetallicFactor = 0.f;
    uint32_t mFlags = sMaterialFlags_CastShadow | sMaterialFlags_ReceiveShadow;

    // maps
    uint64_t mAmbientOcclusionMap = INVALID_TEXTURE;
    uint64_t mEmissiveMap = INVALID_TEXTURE;
    uint64_t mAlbedoMap = INVALID_TEXTURE;

    /// Occlusion (R), Roughness (G), Metallic (B) https://github.com/KhronosGroup/glTF/issues/857
    uint64_t mMetallicRoughnessMap = INVALID_TEXTURE;
    uint64_t mNormalMap = INVALID_TEXTURE;
    uint64_t mOpacityMap = INVALID_TEXTURE;
};

static_assert(sizeof(MaterialDescription) % 16 == 0, "MaterialDescription should be padded to 16 bytes");

void SaveMaterials(const char* filename, const std::vector<MaterialDescription>& materials, const std::vector<std::string>& files);
void LoadMaterials(const char* filename, std::vector<MaterialDescription>& materials, std::vector<std::string>& files);

// Merge material lists from multiple scenes
void MergeMaterialLists(
        // Input:
        const std::vector<std::vector<MaterialDescription>*>& oldMaterials, // all materials
        const std::vector<std::vector<std::string>*>& oldTextures,          // all textures from all material list

        // Output:
        std::vector<MaterialDescription>& allMaterials,
        std::vector<std::string>& newTextures                               // all textures(merged from oldTextures, only unique items)
        );
