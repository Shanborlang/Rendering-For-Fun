#include "GLSceneData.h"

static uint64_t GetTextureHandleBindless(uint64_t idx, const std::vector<GLTexture>& textures) {
    if(idx == INVALID_TEXTURE) return 0;

    return textures[idx].GetHandleBindless();
}

GLSceneData::GLSceneData(const char *meshFile, const char *sceneFile, const char *materialFile) {
    mHeader = loadMeshData(meshFile, mMeshData);
    LoadScene(sceneFile);

    std::vector<std::string> mTextureFiles;
    LoadMaterials(materialFile, mMaterials, mTextureFiles);

    for(const auto& f : mTextureFiles) {
        mAllMaterialTextures.emplace_back(GL_TEXTURE_2D, f.c_str());
    }

    for(auto& mtl : mMaterials) {
        mtl.mAmbientOcclusionMap = GetTextureHandleBindless(mtl.mAmbientOcclusionMap, mAllMaterialTextures);
        mtl.mEmissiveMap = GetTextureHandleBindless(mtl.mEmissiveMap, mAllMaterialTextures);
        mtl.mAlbedoMap = GetTextureHandleBindless(mtl.mAlbedoMap, mAllMaterialTextures);
        mtl.mMetallicRoughnessMap = GetTextureHandleBindless(mtl.mMetallicRoughnessMap, mAllMaterialTextures);
        mtl.mNormalMap = GetTextureHandleBindless(mtl.mNormalMap, mAllMaterialTextures);
    }
}

void GLSceneData::LoadScene(const char *sceneFile) {
    ::LoadScene(sceneFile, mScene);

    // prepare draw data buffer
    for(const auto& c : mScene.mMeshes) {
        auto material = mScene.mMaterialForNode.find(c.first);
        if(material != mScene.mMaterialForNode.end()) {
            mShapes.push_back(
                DrawData {
                    .meshIndex = c.second,
                    .materialIndex = material->second,
                    .LOD = 0,
                    .indexOffset = mMeshData.mMeshes[c.second].indexOffset,
                    .vertexOffset = mMeshData.mMeshes[c.second].vertexOffset,
                    .transformIndex = c.first
                }
            );
        }
    }

    // recalculate all global transformation
    MarkAsChanged(mScene, 0);
    RecalculateGlobalTransforms(mScene);
}
