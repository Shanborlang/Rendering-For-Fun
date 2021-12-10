#include <cstdio>
#include <cstdlib>
#include <vector>

#include "shared/glFramework/GLFWApp.h"
#include "shared/glFramework/GLShader.h"
#include "shared/glFramework/GLTexture.h"
#include "shared/glFramework/GLSceneData.h"
#include "shared/UtilsMath.h"
#include "shared/Camera.h"

#include "shared/scene/VtxData.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/version.h>

const GLuint kBufferIndex_PerFrameUniforms = 0;
const GLuint kBufferIndex_ModelMatrices = 1;
const GLuint kBufferIndex_Materials = 2;

struct PerFrameData {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
};

struct MouseState {
    glm::vec2 pos = glm::vec2(0.f);
    bool pressedLeft = false;
}mouseState;

CameraPositioner_FirstPerson positioner(vec3(-10.f, 3.f, 3.f), vec3(0.f, 0.f, -1.f), vec3(0.f, 1.f, 0.f));
Camera camera(positioner);

struct DrawElementsIndirectCommand {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLuint baseVertex;
    GLuint baseInstance;
};

class GLMesh final {
public:
    GLMesh(const GLSceneData& data)
    : mNumIndices(data.mHeader.indexDataSize / sizeof(uint32_t))
    , mBufferIndices(data.mHeader.indexDataSize, data.mMeshData.mIndexData.data(), 0)
    , mBufferVertices(data.mHeader.vertexDataSize, data.mMeshData.mVertexData.data(), 0)
    , mBufferMaterials(sizeof(MaterialDescription) * data.mMaterials.size(), data.mMaterials.data(), 0)
    , mBufferIndirect(sizeof(DrawElementsIndirectCommand)* data.mShapes.size() + sizeof(GLsizei), nullptr, GL_DYNAMIC_STORAGE_BIT)
    , mBufferModelMatrices(sizeof(glm::mat4)* data.mShapes.size(), nullptr, GL_DYNAMIC_STORAGE_BIT)
    {

        glCreateVertexArrays(1, &mVao);
        glVertexArrayElementBuffer(mVao, mBufferIndices.GetHandle());
        glVertexArrayVertexBuffer(mVao, 0, mBufferVertices.GetHandle(), 0, sizeof(vec3) + sizeof(vec3) + sizeof(vec2));

        // position
        glEnableVertexArrayAttrib(mVao, 0);
        glVertexArrayAttribFormat(mVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(mVao, 0, 0);

        // uv
        glEnableVertexArrayAttrib(mVao, 1);
        glVertexArrayAttribFormat(mVao, 1, 2, GL_FLOAT, GL_FALSE, sizeof(vec3));
        glVertexArrayAttribBinding(mVao, 1, 0);

        // normal
        glEnableVertexArrayAttrib(mVao, 2);
        glVertexArrayAttribFormat(mVao, 2, 3, GL_FLOAT, GL_TRUE, sizeof(vec3) + sizeof(vec2));
        glVertexArrayAttribBinding(mVao, 2, 0);

        std::vector<uint8_t> drawCommands;

        drawCommands.resize(sizeof(DrawElementsIndirectCommand) * data.mShapes.size() + sizeof(GLsizei));

        // store the number of draw commands in the very beginning of the buffer
        const auto numCommands = (GLsizei)data.mShapes.size();
        memcpy(drawCommands.data(), &numCommands, sizeof(numCommands));

        DrawElementsIndirectCommand* cmd = std::launder(
                reinterpret_cast<DrawElementsIndirectCommand*>(drawCommands.data() + sizeof(GLsizei))
                );

        // prepare indirect commands buffer
        for(uint32_t i = 0; i != data.mShapes.size(); i++) {
            const uint32_t meshIdx = data.mShapes[i].meshIndex;
            const uint32_t lod = data.mShapes[i].LOD;
            *cmd++ = {
                    .count = data.mMeshData.mMeshes[meshIdx].GetLODIndicesCount(lod),
                    .instanceCount = 1,
                    .firstIndex = data.mShapes[i].indexOffset,
                    .baseVertex = data.mShapes[i].vertexOffset,
                    .baseInstance = data.mShapes[i].materialIndex
            };
        }

        glNamedBufferSubData(mBufferIndirect.GetHandle(), 0, drawCommands.size(), drawCommands.data());

        std::vector<glm::mat4> matrices(data.mShapes.size());
        size_t i = 0;
        for(const auto& c: data.mShapes) {
            matrices[i++] = data.mScene.mGlobalTransform[c.transformIndex];
        }

        glNamedBufferSubData(mBufferModelMatrices.GetHandle(), 0, matrices.size() * sizeof(glm::mat4), matrices.data());
    }

    void Draw(const GLSceneData& data) const {
        glBindVertexArray(mVao);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBufferIndex_Materials, mBufferMaterials.GetHandle());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBufferIndex_ModelMatrices, mBufferModelMatrices.GetHandle());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mBufferIndirect.GetHandle());
        glBindBuffer(GL_PARAMETER_BUFFER, mBufferIndirect.GetHandle());
        glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)sizeof(GLsizei), 0, (GLsizei)data.mShapes.size(), 0);
    }

    ~GLMesh() {
        glDeleteVertexArrays(1, &mVao);
    }

    GLMesh(const GLMesh&) = delete;
    GLMesh(GLMesh&&) = delete;

private:
    GLuint mVao;
    uint32_t mNumIndices;

    GLBuffer mBufferIndices;
    GLBuffer mBufferVertices;
    GLBuffer mBufferMaterials;
    GLBuffer mBufferIndirect;
    GLBuffer mBufferModelMatrices;
};


int main() {
    GLApp app;
    GLShader shdGridVertex("../../../data/shaders/grid.vert");
    GLShader shdGridFragment("../../../data/shaders/grid.frag");
    GLProgram progGrid(shdGridVertex, shdGridFragment);

    const GLsizei kUniformBufferSize = sizeof(PerFrameData);

    GLBuffer perFrameDataBuffer(kUniformBufferSize, nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, perFrameDataBuffer.GetHandle(), 0, kUniformBufferSize);

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    GLShader shaderVertex("../../../data/shaders/mesh.vert");
    GLShader shaderFragment("../../../data/shaders/mesh.frag");
    GLProgram program(shaderVertex, shaderFragment);

    GLSceneData sceneData1("../../../data/meshes/test.meshes", "../../../data/meshes/test.scene", "../../../data/meshes/test.materials");
    GLSceneData sceneData2("../../../data/meshes/test2.meshes", "../../../data/meshes/test2.scene", "../../../data/meshes/test2.materials");

    GLMesh mesh1(sceneData1);
    GLMesh mesh2(sceneData2);

    glfwSetCursorPosCallback(
            app.GetWindow(),
            [](auto* window, double x, double y)
            {
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                mouseState.pos.x = static_cast<float>(x / width);
                mouseState.pos.y = static_cast<float>(y / height);
            }
    );

    glfwSetMouseButtonCallback(
            app.GetWindow(),
            [](auto* window, int button, int action, int mods)
            {
                if (button == GLFW_MOUSE_BUTTON_LEFT)
                    mouseState.pressedLeft = action == GLFW_PRESS;
            }
    );

    positioner.mMaxSpeed = 5.f;

    glfwSetKeyCallback(
            app.GetWindow(),
            [](GLFWwindow* window, int key, int scancode, int action, int mods)
            {
                const bool pressed = action != GLFW_RELEASE;
                if (key == GLFW_KEY_ESCAPE && pressed)
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                if (key == GLFW_KEY_W)
                    positioner.mMovement.mForward = pressed;
                if (key == GLFW_KEY_S)
                    positioner.mMovement.mBackward = pressed;
                if (key == GLFW_KEY_A)
                    positioner.mMovement.mLeft = pressed;
                if (key == GLFW_KEY_D)
                    positioner.mMovement.mRight = pressed;
                if (key == GLFW_KEY_1)
                    positioner.mMovement.mUp = pressed;
                if (key == GLFW_KEY_2)
                    positioner.mMovement.mDown = pressed;
                if (mods & GLFW_MOD_SHIFT)
                    positioner.mMovement.mFastSpeed = pressed;
                else
                    positioner.mMovement.mFastSpeed = false;
                if (key == GLFW_KEY_SPACE)
                    positioner.SetUpVector(vec3(0.0f, 1.0f, 0.0f));
            }
    );

    double timeStamp = glfwGetTime();
    float deltaSeconds = 0.f;

    while (!glfwWindowShouldClose(app.GetWindow())) {
        positioner.Update(deltaSeconds, mouseState.pos, mouseState.pressedLeft);

        const double newTimeStamp = glfwGetTime();
        deltaSeconds = static_cast<float>(newTimeStamp - timeStamp);
        timeStamp = newTimeStamp;

        int width, height;
        glfwGetFramebufferSize(app.GetWindow(), &width, &height);
        const float ratio = (float)width / (float)height;

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const mat4 p = glm::perspective(45.f, ratio, 0.1f, 1000.f);
        const mat4 view = camera.GetViewMatrix();

        const PerFrameData perFrameData = {
                .view = view,
                .proj = p,
                .cameraPos = glm::vec4(camera.GetPosition(), 1.0f)
        };
        glNamedBufferSubData(perFrameDataBuffer.GetHandle(), 0, kUniformBufferSize, &perFrameData);

        glDisable(GL_BLEND);
        program.UseProgram();
        mesh1.Draw(sceneData1);
        mesh2.Draw(sceneData2);

        glEnable(GL_BLEND);
        progGrid.UseProgram();
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES, 0, 6, 1, 0);

        app.SwapBuffers();
    }

    return 0;
}