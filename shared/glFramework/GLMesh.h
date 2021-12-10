#pragma once

#include <glad/gl.h>

#include "shared/glFramework/GLShader.h"
#include "shared/scene/Material.h"

#include <vector>
#include <functional>

const GLuint kBufferIndex_PerFrameUniforms = 0;
const GLuint kBufferIndex_ModelMatrices = 1;
const GLuint kBufferIndex_Materials = 2;

struct DrawElementsIndirectCommand
{
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLuint baseVertex;
    GLuint baseInstance;
};

class GLIndirectBuffer final {
public:
    explicit GLIndirectBuffer(size_t maxDrawCommands)
        : mBufferIndirect(sizeof(DrawElementsIndirectCommand) * maxDrawCommands, nullptr, GL_DYNAMIC_STORAGE_BIT)
        , mDrawCommands(maxDrawCommands) {}

    GLuint GetHandle() const { return mBufferIndirect.GetHandle(); }

    void UploadIndirectBuffer() {
        glNamedBufferSubData(mBufferIndirect.GetHandle(), 0, sizeof(DrawElementsIndirectCommand) * mDrawCommands.size(), mDrawCommands.data());
    }


    void SelectTo(GLIndirectBuffer& buffer, const std::function<bool(const DrawElementsIndirectCommand)>& pred) {
        buffer.mDrawCommands.clear();
        for(const auto& c : mDrawCommands) {
            if(pred(c))
                buffer.mDrawCommands.push_back(c);
        }
        buffer.UploadIndirectBuffer();
    }

    std::vector<DrawElementsIndirectCommand> mDrawCommands;
private:
    GLBuffer mBufferIndirect;
};

template<typename GLSceneDataType>
class GLMesh final {
public:
    explicit GLMesh(const GLSceneDataType& data)
        : mNumIndices(data.mHeader.indexDataSize / sizeof(uint32_t))
        , mBufferIndices(data.mHeader.indexDataSize, data.mMeshData.mIndexData.data(), 0)
        , mBufferVertices(data.mHeader.vertexDataSize, data.mMeshData.mVertexData().data(), 0)
        , mBufferMaterials(sizeof(MaterialDescription) * data.mMaterials.size(), data.mMaterials.data(), GL_DYNAMIC_STORAGE_BIT)
        , mBufferModelMatrices(sizeof(glm::mat4) * data.mShapes.size(), nullptr, GL_DYNAMIC_STORAGE_BIT)
        , mBufferIndirect(data.mShapes.size()) {

        glCreateVertexArrays(1, &mVao);
        glVertexArrayElementBuffer(mVao, mBufferIndices.GetHandle());
        glVertexArrayVertexBuffer(mVao, 0, mBufferIndices.GetHandle(), 0, sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec2));
        // position
        glEnableVertexArrayAttrib(mVao, 0);
        glVertexArrayAttribFormat(mVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(mVao, 0, 0);
        // uv
        glEnableVertexArrayAttrib(mVao, 1);
        glVertexArrayAttribFormat(mVao, 1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec3));
        glVertexArrayAttribBinding(mVao, 1, 0);
        // normal
        glEnableVertexArrayAttrib(mVao, 2);
        glVertexArrayAttribFormat(mVao, 2, 3, GL_FLOAT, GL_TRUE, sizeof(glm::vec3) + sizeof(glm::vec2));
        glVertexArrayAttribBinding(mVao, 2, 0);

        std::vector<glm::mat4> matrices(data.mShapes.size());

        // prepare indirect commands buffer
        for (size_t i = 0; i != data.shapes_.size(); i++)
        {
            const uint32_t meshIdx = data.shapes_[i].meshIndex;
            const uint32_t lod = data.shapes_[i].LOD;
            mBufferIndirect.mDrawCommands[i] = {
                    .count = data.mMeshData.meshes_[meshIdx].GetLODIndicesCount(lod),
                    .instanceCount = 1,
                    .firstIndex = data.mShapes[i].indexOffset,
                    .baseVertex = data.mShapes[i].vertexOffset,
                    .baseInstance = data.mShapes[i].materialIndex + (uint32_t(i) << 16)
            };
            matrices[i] = data.mScene.mGlobalTransform[data.mShapes[i].transformIndex];
        }
        mBufferIndirect.UploadIndirectBuffer();

        glNamedBufferSubData(mBufferModelMatrices.GetHandle(), 0, matrices.size() * sizeof(glm::mat4), matrices.data());
    }

    void UpdateMaterialsBuffer(const GLSceneDataType& data) {
        glNamedBufferSubData(mBufferMaterials.GetHandle(), 0, sizeof(MaterialDescription) * data.mMaterials.size(), data.mMaterials.data());
    }

    void Draw(size_t numDrawCommands, const GLIndirectBuffer* buffer = nullptr) const {
        glBindVertexArray(mVao);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBufferIndex_Materials, mBufferMaterials.GetHandle());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBufferIndex_ModelMatrices, mBufferModelMatrices.GetHandle());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, (buffer ? *buffer : mBufferIndirect).GetHandle());
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, (GLsizei)numDrawCommands, 0);
    }

    ~GLMesh() {
        glDeleteVertexArrays(1, &mVao);
    }

    GLMesh(const GLMesh&) = delete;
    GLMesh(GLMesh&&) noexcept = default;

private:
    GLuint mVao;
    uint32_t mNumIndices;

    GLBuffer mBufferIndices;
    GLBuffer mBufferVertices;
    GLBuffer mBufferMaterials;
    GLBuffer mBufferModelMatrices;

    GLIndirectBuffer mBufferIndirect;
};