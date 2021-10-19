#include <algorithm>
#include <execution>
#include <fstream>
#include <filesystem>

#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/pbrmaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include "shared/scene/VtxData.h"

#include "shared/scene/Material.h"
#include "shared/scene/Scene.h"
#include "shared/scene/MergeUtil.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"
#include "stb_image_resize.h"

#include <meshoptimizer.h>

namespace fs = std::filesystem;

MeshData g_MeshData;

uint32_t g_indexOffset = 0;
uint32_t g_vertexOffset = 0;

const uint32_t g_numElementsToStore = 3 + 3 + 2; // pos(vec3) + normal(vec3) + uv(vec2)

struct SceneConfig
{
    std::string fileName;
    std::string outputMesh;
    std::string outputScene;
    std::string outputMaterials;
    float scale;
    bool calculateLODs;
    bool mergeInstances;
};

MaterialDescription convertAIMaterialToDescription(const aiMaterial* M, std::vector<std::string>& files, std::vector<std::string>& opacityMaps)
{
    MaterialDescription D;

    aiColor4D Color;

    if (aiGetMaterialColor(M, AI_MATKEY_COLOR_AMBIENT, &Color) == AI_SUCCESS)
    {
        D.mEmissiveColor = { Color.r, Color.g, Color.b, Color.a };
        if (D.mEmissiveColor.w > 1.0f) D.mEmissiveColor.w = 1.0f;
    }
    if (aiGetMaterialColor(M, AI_MATKEY_COLOR_DIFFUSE, &Color) == AI_SUCCESS)
    {
        D.mAlbedoColor = { Color.r, Color.g, Color.b, Color.a };
        if (D.mAlbedoColor.w > 1.0f) D.mAlbedoColor.w = 1.0f;
    }
    if (aiGetMaterialColor(M, AI_MATKEY_COLOR_EMISSIVE, &Color) == AI_SUCCESS)
    {
        D.mEmissiveColor.x += Color.r;
        D.mEmissiveColor.y += Color.g;
        D.mEmissiveColor.z += Color.b;
        D.mEmissiveColor.w += Color.a;
        if (D.mEmissiveColor.w > 1.0f) D.mAlbedoColor.w = 1.0f;
    }

    const float opaquenessThreshold = 0.05f;
    float Opacity = 1.0f;

    if (aiGetMaterialFloat(M, AI_MATKEY_OPACITY, &Opacity) == AI_SUCCESS)
    {
        D.mTransparencyFactor = glm::clamp(1.0f - Opacity, 0.0f, 1.0f);
        if (D.mTransparencyFactor >= 1.0f - opaquenessThreshold) D.mTransparencyFactor = 0.0f;
    }

    if (aiGetMaterialColor(M, AI_MATKEY_COLOR_TRANSPARENT, &Color) == AI_SUCCESS)
    {
        const float Opacity = std::max(std::max(Color.r, Color.g), Color.b);
        D.mTransparencyFactor = glm::clamp(Opacity, 0.0f, 1.0f);
        if (D.mTransparencyFactor >= 1.0f - opaquenessThreshold) D.mTransparencyFactor = 0.0f;
        D.mAlphaTest = 0.5f;
    }

    float tmp = 1.0f;
    if (aiGetMaterialFloat(M, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, &tmp) == AI_SUCCESS)
        D.mMetallicFactor = tmp;

    if (aiGetMaterialFloat(M, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, &tmp) == AI_SUCCESS)
        D.mRoughness = { tmp, tmp, tmp, tmp };

    aiString Path;
    aiTextureMapping Mapping;
    unsigned int UVIndex = 0;
    float Blend = 1.0f;
    aiTextureOp TextureOp = aiTextureOp_Add;
    aiTextureMapMode TextureMapMode[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
    unsigned int TextureFlags = 0;

    if ( aiGetMaterialTexture( M, aiTextureType_EMISSIVE, 0, &Path, &Mapping, &UVIndex, &Blend, &TextureOp, TextureMapMode, &TextureFlags ) == AI_SUCCESS )
    {
        D.mEmissiveMap = AddUnique(files, Path.C_Str());
    }

    if ( aiGetMaterialTexture( M, aiTextureType_DIFFUSE, 0, &Path, &Mapping, &UVIndex, &Blend, &TextureOp, TextureMapMode, &TextureFlags ) == AI_SUCCESS )
    {
        D.mAlbedoMap = AddUnique(files, Path.C_Str());
        const std::string albedoMap = std::string(Path.C_Str());
        if (albedoMap.find("grey_30") != std::string::npos)
            D.mFlags |= sMaterialFlags_Transparent;
    }

    // first try tangent space normal map
    if ( aiGetMaterialTexture( M, aiTextureType_NORMALS, 0, &Path, &Mapping, &UVIndex, &Blend, &TextureOp, TextureMapMode, &TextureFlags) == AI_SUCCESS )
    {
        D.mNormalMap = AddUnique(files, Path.C_Str());
    }
    // then height map
    if (D.mNormalMap == 0xFFFFFFFF)
        if ( aiGetMaterialTexture( M, aiTextureType_HEIGHT, 0, &Path, &Mapping, &UVIndex, &Blend, &TextureOp, TextureMapMode, &TextureFlags ) == AI_SUCCESS )
            D.mNormalMap = AddUnique(files, Path.C_Str());

    if ( aiGetMaterialTexture( M, aiTextureType_OPACITY, 0, &Path, &Mapping, &UVIndex, &Blend, &TextureOp, TextureMapMode, &TextureFlags ) == AI_SUCCESS )
    {
        D.mOpacityMap = AddUnique(opacityMaps, Path.C_Str());
        D.mAlphaTest = 0.5f;
    }

    // patch materials
    aiString Name;
    std::string materialName;
    if (aiGetMaterialString(M, AI_MATKEY_NAME, &Name) == AI_SUCCESS)
    {
        materialName = Name.C_Str();
    }
    // apply heuristics
    if ((materialName.find("Glass") != std::string::npos) ||
        (materialName.find("Vespa_Headlight") != std::string::npos))
    {
        D.mAlphaTest = 0.75f;
        D.mTransparencyFactor = 0.1f;
        D.mFlags |= sMaterialFlags_Transparent;
    }
    else if (materialName.find("Bottle") != std::string::npos)
    {
        D.mAlphaTest = 0.54f;
        D.mTransparencyFactor = 0.4f;
        D.mFlags |= sMaterialFlags_Transparent;
    }
    else if (materialName.find("Metal") != std::string::npos)
    {
        D.mMetallicFactor = 1.0f;
        D.mRoughness = gpuvec4(0.1f, 0.1f, 0.0f, 0.0f);
    }

    return D;
}

void processLods(std::vector<uint32_t>& indices, std::vector<float>& vertices, std::vector<std::vector<uint32_t>>& outLods)
{
    size_t verticesCountIn = vertices.size() / 2;
    size_t targetIndicesCount = indices.size();

    uint8_t LOD = 1;

    printf("\n   LOD0: %i indices", int(indices.size()));

    outLods.push_back(indices);

    while ( targetIndicesCount > 1024 && LOD < 8 )
    {
        targetIndicesCount = indices.size() / 2;

        bool sloppy = false;

        size_t numOptIndices = meshopt_simplify(
                indices.data(),
                indices.data(), (uint32_t)indices.size(),
                vertices.data(), verticesCountIn,
                sizeof( float ) * 3,
                targetIndicesCount, 0.02f );

        // cannot simplify further
        if (static_cast<size_t>(numOptIndices * 1.1f) > indices.size())
        {
            if (LOD > 1)
            {
                // try harder
                numOptIndices = meshopt_simplifySloppy(
                        indices.data(),
                        indices.data(), indices.size(),
                        vertices.data(), verticesCountIn,
                        sizeof(float) * 3,
                        targetIndicesCount, 0.02f, nullptr);
                sloppy = true;
                if (numOptIndices == indices.size()) break;
            }
            else
                break;
        }

        indices.resize(numOptIndices);

        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), verticesCountIn);

        printf("\n   LOD%i: %i indices %s", int(LOD), int(numOptIndices), sloppy ? "[sloppy]" : "");

        LOD++;

        outLods.push_back(indices);
    }
}

Mesh convertAIMesh(const aiMesh* m, const SceneConfig& cfg)
{
    const bool hasTexCoords = m->HasTextureCoords(0);
    const auto streamElementSize = static_cast<uint32_t>(g_numElementsToStore * sizeof(float));

    Mesh result = {
            .streamCount = 1,
            .indexOffset = g_indexOffset,
            .vertexOffset = g_vertexOffset,
            .vertexCount = m->mNumVertices,
            .streamOffset = { g_vertexOffset * streamElementSize },
            .streamElementSize = { streamElementSize }
    };

    // Original data for LOD calculation
    std::vector<float> srcVertices;
    std::vector<uint32_t> srcIndices;

    std::vector<std::vector<uint32_t>> outLods;

    auto& vertices = g_MeshData.vertexData_;

    for (size_t i = 0; i != m->mNumVertices; i++)
    {
        const aiVector3D v = m->mVertices[i];
        const aiVector3D n = m->mNormals[i];
        const aiVector3D t = hasTexCoords ? m->mTextureCoords[0][i] : aiVector3D();

        if (cfg.calculateLODs)
        {
            srcVertices.push_back(v.x);
            srcVertices.push_back(v.y);
            srcVertices.push_back(v.z);
        }

        vertices.push_back(v.x * cfg.scale);
        vertices.push_back(v.y * cfg.scale);
        vertices.push_back(v.z * cfg.scale);

        vertices.push_back(t.x);
        vertices.push_back(1.0f - t.y);

        vertices.push_back(n.x);
        vertices.push_back(n.y);
        vertices.push_back(n.z);
    }

    for (size_t i = 0; i != m->mNumFaces; i++)
    {
        if (m->mFaces[i].mNumIndices != 3)
            continue;
        for (unsigned j = 0; j != m->mFaces[i].mNumIndices; j++)
            srcIndices.push_back(m->mFaces[i].mIndices[j]);
    }

    if (!cfg.calculateLODs)
        outLods.push_back(srcIndices);
    else
        processLods(srcIndices, srcVertices, outLods);

    printf("\nCalculated LOD count: %u\n", (unsigned)outLods.size());

    uint32_t numIndices = 0;

    for (size_t l = 0 ; l < outLods.size() ; l++)
    {
        for (unsigned int i : outLods[l])
            g_MeshData.indexData_.push_back(i);

        result.lodOffset[l] = numIndices;
        numIndices += (int)outLods[l].size();
    }

    result.lodOffset[outLods.size()] = numIndices;
    result.lodCount = (uint32_t)outLods.size();

    g_indexOffset  += numIndices;
    g_vertexOffset += m->mNumVertices;

    return result;
}

void makePrefix(int ofs) { for(int i = 0 ; i < ofs ; i++) printf("\t"); }

void printMat4(const aiMatrix4x4& m)
{
    if (!m.IsIdentity())
    {
        for (int i = 0 ; i < 4 ; i++)
            for (int j = 0 ; j < 4 ; j++)
                printf("%f ;", m[i][j]);
    } else
    {
        printf(" Identity");
    }
}

glm::mat4 toMat4(const aiMatrix4x4& from)
{
    glm::mat4 to;
    to[0][0] = (float)from.a1; to[0][1] = (float)from.b1;  to[0][2] = (float)from.c1; to[0][3] = (float)from.d1;
    to[1][0] = (float)from.a2; to[1][1] = (float)from.b2;  to[1][2] = (float)from.c2; to[1][3] = (float)from.d2;
    to[2][0] = (float)from.a3; to[2][1] = (float)from.b3;  to[2][2] = (float)from.c3; to[2][3] = (float)from.d3;
    to[3][0] = (float)from.a4; to[3][1] = (float)from.b4;  to[3][2] = (float)from.c4; to[3][3] = (float)from.d4;
    return to;
}

void traverse(const aiScene* sourceScene, Scene& scene, aiNode* N, int parent, int ofs)
{
    int newNode = AddNode(scene, parent, ofs);

    if (N->mName.C_Str())
    {
        makePrefix(ofs); printf("Node[%d].name = %s\n", newNode, N->mName.C_Str());

        auto stringID = (uint32_t)scene.mNames.size();
        scene.mNames.emplace_back(N->mName.C_Str());
        scene.mNameForNode[newNode] = stringID;
    }

    for (size_t i = 0; i < N->mNumMeshes ; i++)
    {
        int newSubNode = AddNode(scene, newNode, ofs + 1);;

        auto stringID = (uint32_t)scene.mNames.size();
        scene.mNames.push_back(std::string(N->mName.C_Str()) + "_Mesh_" + std::to_string(i));
        scene.mNameForNode[newSubNode] = stringID;

        int mesh = (int)N->mMeshes[i];
        scene.mMeshes[newSubNode] = mesh;
        scene.mMaterialForNode[newSubNode] = sourceScene->mMeshes[mesh]->mMaterialIndex;

        makePrefix(ofs); printf("Node[%d].SubNode[%d].mesh     = %d\n", newNode, newSubNode, (int)mesh);
        makePrefix(ofs); printf("Node[%d].SubNode[%d].material = %d\n", newNode, newSubNode, sourceScene->mMeshes[mesh]->mMaterialIndex);

        scene.mGlobalTransform[newSubNode] = glm::mat4(1.0f);
        scene.mLocalTransform[newSubNode] = glm::mat4(1.0f);
    }

    scene.mGlobalTransform[newNode] = glm::mat4(1.0f);
    scene.mLocalTransform[newNode] = toMat4(N->mTransformation);

    if (N->mParent != nullptr) {
        makePrefix(ofs); printf("\tNode[%d].parent         = %s\n", newNode, N->mParent->mName.C_Str());
        makePrefix(ofs); printf("\tNode[%d].localTransform = ", newNode); printMat4(N->mTransformation); printf("\n");
    }

    for (unsigned int n = 0 ; n  < N->mNumChildren ; n++)
        traverse(sourceScene, scene, N->mChildren[n], newNode, ofs + 1);
}

void dumpMaterial(const std::vector<std::string>& files, const MaterialDescription& d)
{
    printf("files: %d\n", (int)files.size());
    printf("maps: %u/%u/%u/%u/%u\n", (uint32_t)d.mAlbedoMap, (uint32_t)d.mAmbientOcclusionMap, (uint32_t)d.mEmissiveMap, (uint32_t)d.mOpacityMap, (uint32_t)d.mMetallicRoughnessMap);
    printf(" albedo:    %s\n", (d.mAlbedoMap < 0xFFFF) ? files[d.mAlbedoMap].c_str() : "");
    printf(" occlusion: %s\n", (d.mAmbientOcclusionMap < 0xFFFF) ? files[d.mAmbientOcclusionMap].c_str() : "");
    printf(" emission:  %s\n", (d.mEmissiveMap < 0xFFFF) ? files[d.mEmissiveMap].c_str() : "");
    printf(" opacity:   %s\n", (d.mOpacityMap < 0xFFFF) ? files[d.mOpacityMap].c_str() : "");
    printf(" MeR:       %s\n", (d.mMetallicRoughnessMap < 0xFFFF) ? files[d.mMetallicRoughnessMap].c_str() : "");
    printf(" Normal:    %s\n", (d.mNormalMap < 0xFFFF) ? files[d.mNormalMap].c_str() : "");
}

std::string replaceAll( const std::string& str, const std::string& oldSubStr, const std::string& newSubStr )
{
    std::string result = str;

    for ( size_t p = result.find( oldSubStr ); p != std::string::npos; p = result.find( oldSubStr ) )
        result.replace( p, oldSubStr.length(), newSubStr );

    return result;
}

/* Convert 8-bit ASCII string to upper case */
std::string lowercaseString(const std::string& s)
{
    std::string out(s.length(), ' ');
    std::transform(s.begin(), s.end(), out.begin(), tolower);
    return out;
}

/* find a file in directory which "almost" coincides with the origFile (their lowercase versions coincide) */
std::string findSubstitute(const std::string& origFile)
{
    // Make absolute path
    auto apath = fs::absolute(fs::path(origFile));
    // Absolute lowercase filename [we compare with it]
    auto afile = lowercaseString(apath.filename().string());
    // Directory where in which the file should be
    auto dir = fs::path(origFile).remove_filename();

    // Iterate each file non-recursively and compare lowercase absolute path with 'afile'
    for(auto& p: fs::directory_iterator(dir))
        if (afile == lowercaseString(p.path().filename().string()))
            return p.path().string();

    return std::string {};
}

std::string fixTextureFile(const std::string& file)
{
    // TODO: check the findSubstitute() function
    return fs::exists(file) ? file : findSubstitute(file);
}

std::string convertTexture(const std::string& file, const std::string& basePath, std::unordered_map<std::string, uint32_t>& opacityMapIndices, const std::vector<std::string>& opacityMaps)
{
    const int maxNewWidth = 512;
    const int maxNewHeight = 512;

    const auto srcFile = replaceAll(basePath + file, "\\",  "/");
    auto newFile = std::string("../../../data/out_textures/") + lowercaseString(replaceAll(replaceAll(srcFile, "..", "__"), "/", "__") + std::string("__rescaled")) + std::string(".png");

    // load this image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(fixTextureFile(srcFile).c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    uint8_t* src = pixels;
    texChannels = STBI_rgb_alpha;

    std::vector<uint8_t> tmpImage(maxNewWidth * maxNewHeight * 4);

    if (!src)
    {
        printf("Failed to load [%s] texture\n", srcFile.c_str());
        texWidth = maxNewWidth;
        texHeight = maxNewHeight;
        texChannels = STBI_rgb_alpha;
        src = tmpImage.data();
    }
    else
    {
        printf("Loaded [%s] %dx%d texture with %d channels\n", srcFile.c_str(), texWidth, texHeight, texChannels);
    }

    if (opacityMapIndices.count(file) > 0)
    {
        const auto opacityMapFile = replaceAll(basePath + opacityMaps[opacityMapIndices[file]], "\\", "/");
        int opacityWidth, opacityHeight;
        stbi_uc* opacityPixels = stbi_load(fixTextureFile(opacityMapFile).c_str(), &opacityWidth, &opacityHeight, nullptr, 1);

        if (!opacityPixels)
        {
            printf("Failed to load opacity mask [%s]\n", opacityMapFile.c_str());
        }

        assert(opacityPixels);
        assert(texWidth == opacityWidth);
        assert(texHeight == opacityHeight);

        // store the opacity mask in the alpha component of this image
        if (opacityPixels)
            for (int y = 0; y != opacityHeight; y++)
                for (int x = 0; x != opacityWidth; x++)
                    src[(y * opacityWidth + x) * texChannels + 3] = opacityPixels[y * opacityWidth + x];

        stbi_image_free(opacityPixels);
    }

    const uint32_t imgSize = texWidth * texHeight * texChannels;
    std::vector<uint8_t> mipData(imgSize);
    uint8_t* dst = mipData.data();

    const int newW = std::min(texWidth, maxNewWidth);
    const int newH = std::min(texHeight, maxNewHeight);

    stbir_resize_uint8(src, texWidth, texHeight, 0, dst, newW, newH, 0, texChannels);

    stbi_write_png(newFile.c_str(), newW, newH, texChannels, dst, 0);

    if (pixels)
        stbi_image_free(pixels);

    return newFile;
}

void convertAndDownscaleAllTextures(
        const std::vector<MaterialDescription>& materials, const std::string& basePath, std::vector<std::string>& files, std::vector<std::string>& opacityMaps
)
{
    std::unordered_map<std::string, uint32_t> opacityMapIndices(files.size());

    for (const auto& m : materials)
        if (m.mOpacityMap != 0xFFFFFFFF && m.mAlbedoMap != 0xFFFFFFFF)
            opacityMapIndices[files[m.mAlbedoMap]] = (uint32_t)m.mOpacityMap;

    auto converter = [&](const std::string& s) -> std::string
    {
        return convertTexture(s, basePath, opacityMapIndices, opacityMaps);
    };

    std::transform(std::execution::par, std::begin(files), std::end(files), std::begin(files), converter);
}

std::vector<SceneConfig> readConfigFile(const char* cfgFileName)
{
    std::ifstream ifs(cfgFileName);
    if (!ifs.is_open())
    {
        printf("Failed to load configuration file.\n");
        exit(EXIT_FAILURE);
    }

    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document document;
    const rapidjson::ParseResult parseResult = document.ParseStream(isw);
    assert(!parseResult.IsError());
    assert(document.IsArray());

    std::vector<SceneConfig> configList;

    std::string s = "../../../";
    for (rapidjson::SizeType i = 0; i < document.Size(); i++)
    {
        configList.emplace_back(SceneConfig {
                .fileName = s + document[i]["input_scene"].GetString(),
                .outputMesh = s + document[i]["output_mesh"].GetString(),
                .outputScene = s + document[i]["output_scene"].GetString(),
                .outputMaterials = s + document[i]["output_materials"].GetString(),
                .scale = (float)document[i]["scale"].GetDouble(),
                .calculateLODs = document[i]["calculate_LODs"].GetBool(),
                .mergeInstances = document[i]["merge_instances"].GetBool()
        });
    }

    return configList;
}

void processScene(const SceneConfig& cfg)
{
    // clear mesh data from previous scene
    g_MeshData.meshes_.clear();
    g_MeshData.boxes_.clear();
    g_MeshData.indexData_.clear();
    g_MeshData.vertexData_.clear();

    g_indexOffset = 0;
    g_vertexOffset = 0;

    // extract base model path
    const std::size_t pathSeparator = cfg.fileName.find_last_of("/\\");
    const std::string basePath = (pathSeparator != std::string::npos) ? cfg.fileName.substr(0, pathSeparator + 1) : std::string();

    const unsigned int flags = 0 |
                               aiProcess_JoinIdenticalVertices |
                               aiProcess_Triangulate |
                               aiProcess_GenSmoothNormals |
                               aiProcess_LimitBoneWeights |
                               aiProcess_SplitLargeMeshes |
                               aiProcess_ImproveCacheLocality |
                               aiProcess_RemoveRedundantMaterials |
                               aiProcess_FindDegenerates |
                               aiProcess_FindInvalidData |
                               aiProcess_GenUVCoords;

    printf("Loading scene from '%s'...\n", cfg.fileName.c_str());

    const aiScene* scene = aiImportFile(cfg.fileName.c_str(), flags);

    if (!scene || !scene->HasMeshes())
    {
        printf("Unable to load '%s'\n", cfg.fileName.c_str());
        exit(EXIT_FAILURE);
    }

    // 1. Mesh conversion as in Chapter 5
    g_MeshData.meshes_.reserve(scene->mNumMeshes);
    g_MeshData.boxes_.reserve(scene->mNumMeshes);

    for (unsigned int i = 0; i != scene->mNumMeshes; i++)
    {
        printf("\nConverting meshes %u/%u...", i + 1, scene->mNumMeshes);
        Mesh mesh = convertAIMesh(scene->mMeshes[i], cfg);
        g_MeshData.meshes_.push_back(mesh);
    }

    recalculateBoundingBoxes(g_MeshData);

    saveMeshData(cfg.outputMesh.c_str(), g_MeshData);

    Scene ourScene;

    // 2. Material conversion
    std::vector<MaterialDescription> materials;
    std::vector<std::string>& materialNames = ourScene.mMaterialNames;

    std::vector<std::string> files;
    std::vector<std::string> opacityMaps;

    for (unsigned int m = 0 ; m < scene->mNumMaterials ; m++)
    {
        aiMaterial* mm = scene->mMaterials[m];

        printf("Material [%s] %u\n", mm->GetName().C_Str(), m);
        materialNames.emplace_back(mm->GetName().C_Str());

        MaterialDescription D = convertAIMaterialToDescription(mm, files, opacityMaps);
        materials.push_back(D);
        //dumpMaterial(files, D);
    }

    // 3. Texture processing, rescaling and packing
    convertAndDownscaleAllTextures(materials, basePath, files, opacityMaps);

    SaveMaterials(cfg.outputMaterials.c_str(), materials, files);

    // 4. Scene hierarchy conversion
    traverse(scene, ourScene, scene->mRootNode, -1, 0);

    SaveScene(cfg.outputScene.c_str(), ourScene);
}

void mergeBistro()
{
    Scene scene1, scene2;
    std::vector<Scene*> scenes = { &scene1, &scene2 };

    MeshData m1, m2;
    MeshFileHeader header1 = loadMeshData("../../../data/meshes/test.meshes", m1);
    MeshFileHeader header2 = loadMeshData("../../../data/meshes/test2.meshes", m2);

    std::vector<uint32_t> meshCounts = { header1.meshCount, header2.meshCount };

    LoadScene("../../../data/meshes/test.scene", scene1);
    LoadScene("../../../data/meshes/test2.scene", scene2);

    Scene scene;
    MergeScenes(scene, scenes, {}, meshCounts);

    MeshData meshData;
    std::vector<MeshData*> meshDatas = { &m1, &m2 };

    MeshFileHeader header = mergeMeshData(meshData, meshDatas);

    // now the material lists:
    std::vector<MaterialDescription> materials1, materials2;
    std::vector<std::string> textureFiles1, textureFiles2;
    LoadMaterials("../../../data/meshes/test.materials", materials1, textureFiles1);
    LoadMaterials("../../../data/meshes/test2.materials", materials2, textureFiles2);

    std::vector<MaterialDescription> allMaterials;
    std::vector<std::string> allTextures;

    MergeMaterialLists(
            { &materials1, &materials2 },
            { &textureFiles1, &textureFiles2 },
            allMaterials, allTextures);

    SaveMaterials("../../../data/meshes/bistro_all.materials", allMaterials, allTextures);

    printf("[Unmerged] scene items: %d\n", (int)scene.mHierarchy.size());
    MergeScene(scene, meshData, "Foliage_Linde_Tree_Large_Orange_Leaves");
    printf("[Merged orange leaves] scene items: %d\n", (int)scene.mHierarchy.size());
    MergeScene(scene, meshData, "Foliage_Linde_Tree_Large_Green_Leaves");
    printf("[Merged green leaves]  scene items: %d\n", (int)scene.mHierarchy.size());
    MergeScene(scene, meshData, "Foliage_Linde_Tree_Large_Trunk");
    printf("[Merged trunk]  scene items: %d\n", (int)scene.mHierarchy.size());

    recalculateBoundingBoxes(meshData);

    saveMeshData("../../../data/meshes/bistro_all.meshes", meshData);
    SaveScene("../../../data/meshes/bistro_all.scene", scene);
}

int main() {
    fs::create_directory("../../../data/out_textures");

    const auto configs = readConfigFile("../../../data/sceneconverter.json");

    for (const auto& cfg: configs)
        processScene(cfg);

    // Final step: optimize bistro scene
    mergeBistro();

    return 0;
}