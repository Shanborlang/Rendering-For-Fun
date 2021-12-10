#ifndef RENDERING_FOR_FUN_GLSCENEDATA_H
#define RENDERING_FOR_FUN_GLSCENEDATA_H

#include "shared/scene/Scene.h"
#include "shared/scene/Material.h"
#include "shared/scene/VtxData.h"
#include "shared/glFramework/GLShader.h"
#include "shared/glFramework/GLTexture.h"

class GLSceneData {
public:
    GLSceneData(const char* meshFile,
                const char* sceneFile,
                const char* materialFile);

    std::vector<GLTexture> mAllMaterialTextures;

    MeshFileHeader mHeader;
    MeshData mMeshData;

    Scene mScene;
    std::vector<MaterialDescription> mMaterials;
    std::vector<DrawData> mShapes;

    void LoadScene(const char* sceneFile);
};

#endif //RENDERING_FOR_FUN_GLSCENEDATA_H
