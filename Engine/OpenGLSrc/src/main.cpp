#include <cstdio>
#include <cstdlib>
#include <vector>

#include "shared/glFramework/GLFWApp.h"
#include "shared/glFramework/GLShader.h"
#include "shared/glFramework/GLTexture.h"
#include "shared/UtilsMath.h"
#include "shared/Camera.h"

#include "shared/scene/VtxData.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/version.h>

struct PerFrameData {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
};

struct MouseState {
    glm::vec2 pos = glm::vec2(0.f);
    bool pressedLeft = false;
}mouseState;

CameraPositioner_FirstPerson positioner(vec3(-32.5f, 7.5f, -9.5f), vec3(0.f, 0.f, -1.f), vec3(0.f, 1.f, 0.f));
Camera camera(positioner);

struct DrawElementsIndirectCommand {
    GLuint count_;
    GLuint instanceCount_;
    GLuint firstIndex_;
    GLuint baseVertex_;
    GLuint baseInstance_;
};

class GLMesh final {
public:
    GLMesh(const MeshFileHeader& header, const Mesh* meshes, const uint32_t* indices, const float* vertexData)
    : numIndices_(header.indexDataSize / sizeof(uint32_t))
    , bufferIndices_(header.indexDataSize, indices, 0)
    , bufferVertices_(header.vertexDataSize, vertexData, 0)
    , bufferIndirect_(sizeof(DrawElementsIndirectCommand)*header.meshCount + sizeof(GLsizei), nullptr, GL_DYNAMIC_STORAGE_BIT) {

        glCreateVertexArrays(1, &vao_);
        glVertexArrayElementBuffer(vao_, bufferIndices_.GetHandle());
        glVertexArrayVertexBuffer(vao_, 0, bufferVertices_.GetHandle(), 0, sizeof(vec3) + sizeof(vec3) + sizeof(vec2));
        check_for_opengl_error(__FILE__, __LINE__);

        // position
        glEnableVertexArrayAttrib(vao_, 0);
        glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(vao_, 0, 0);
        check_for_opengl_error(__FILE__, __LINE__);

        // uv
        glEnableVertexArrayAttrib(vao_, 1);
        glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, sizeof(vec3));
        glVertexArrayAttribBinding(vao_, 1, 0);
        check_for_opengl_error(__FILE__, __LINE__);

        // normal
        glEnableVertexArrayAttrib(vao_, 2);
        glVertexArrayAttribFormat(vao_, 2, 3, GL_FLOAT, GL_TRUE, sizeof(vec3) + sizeof(vec2));
        glVertexArrayAttribBinding(vao_, 2, 0);
        check_for_opengl_error(__FILE__, __LINE__);

        std::vector<uint8_t> drawCommands;

        const auto numCommands = (GLsizei)header.meshCount;

        drawCommands.resize(sizeof(DrawElementsIndirectCommand) * numCommands + sizeof(GLsizei));

        // store the number of draw commands in the very beginning of the buffer
        memcpy(drawCommands.data(), &numCommands, sizeof(numCommands));

        DrawElementsIndirectCommand* cmd = std::launder(
                reinterpret_cast<DrawElementsIndirectCommand*>(drawCommands.data() + sizeof(GLsizei))
                );

        // prepare indirect commands buffer
        for(uint32_t i = 0; i != numCommands; i++) {
            *cmd++ = {
                    .count_ = meshes[i].GetLODIndicesCount(0),
                    .instanceCount_ = 1,
                    .firstIndex_ = meshes[i].indexOffset,
                    .baseVertex_ = meshes[i].vertexOffset,
                    .baseInstance_ = 0
            };
        }

        glNamedBufferSubData(bufferIndirect_.GetHandle(), 0, drawCommands.size(), drawCommands.data());
        check_for_opengl_error(__FILE__, __LINE__);

        glBindVertexArray(vao_);
        check_for_opengl_error(__FILE__, __LINE__);
    }

    void Draw(const MeshFileHeader& header) const {
        glBindVertexArray(vao_);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bufferIndirect_.GetHandle());
        glBindBuffer(GL_PARAMETER_BUFFER, bufferIndirect_.GetHandle());
        glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)sizeof(GLsizei), 0, (GLsizei)header.meshCount, 0);
    }

    ~GLMesh() {
        glDeleteVertexArrays(1, &vao_);
    }

    GLMesh(const GLMesh&) = delete;
    GLMesh(GLMesh&&) = delete;

private:
    GLuint vao_;
    uint32_t numIndices_;

    GLBuffer bufferIndices_;
    GLBuffer bufferVertices_;
    GLBuffer bufferIndirect_;
};

class GLMeshPVP final {
public:
    GLMeshPVP(const uint32_t* indices, uint32_t indicesSize, const float* vertexData, uint32_t verticesSize)
        : mNumIndices(indicesSize / sizeof(uint32_t))
        , mBufferIndices(indicesSize, indices, 0)
        , mBufferVertices(verticesSize, vertexData, 0) {

        glCreateVertexArrays(1, &mVao);
        glVertexArrayElementBuffer(mVao, mBufferIndices.GetHandle());
    }

    void Draw() const {
        glBindVertexArray(mVao);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mBufferVertices.GetHandle());
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mNumIndices), GL_UNSIGNED_INT, nullptr);
    }

    ~GLMeshPVP() {
        glDeleteVertexArrays(1, &mVao);
    }

private:
    GLuint mVao;
    uint32_t mNumIndices;

    GLBuffer mBufferIndices;
    GLBuffer mBufferVertices;
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    GLShader shaderVertex("../../../data/shaders/PBR.vert");
    GLShader shaderFragment("../../../data/shaders/PBR.frag");
    GLProgram program(shaderVertex, shaderFragment);

    const aiScene* scene = aiImportFile("../../../deps/src/glTF-Sample-Models/2.0//DamagedHelmet/glTF/DamagedHelmet.gltf", aiProcess_Triangulate);
    if(!scene || !scene->HasMeshes()) {
        printf("Unable to load ../../../deps/src/glTF-Sample-Models/2.0//DamagedHelmet/glTF/DamagedHelmet.gltf\n");
        exit(255);
    }

    struct VertexData {
        glm::vec3 pos;
        glm::vec3 n;
        glm::vec2 tc;
    };

    std::vector<VertexData> vertices;
    std::vector<uint32_t> indices;
    {
        const aiMesh* mesh = scene->mMeshes[0];
        for (unsigned i = 0; i != mesh->mNumVertices; i++)
        {
            const aiVector3D v = mesh->mVertices[i];
            const aiVector3D n = mesh->mNormals[i];
            const aiVector3D t = mesh->mTextureCoords[0][i];
            vertices.push_back({ .pos = vec3(v.x, v.y, v.z), .n = vec3(n.x, n.y, n.z), .tc = vec2(t.x, 1.0f - t.y) });
        }
        for (unsigned i = 0; i != mesh->mNumFaces; i++)
        {
            for (unsigned j = 0; j != 3; j++)
                indices.push_back(mesh->mFaces[i].mIndices[j]);
        }
        aiReleaseImport(scene);
    }

    const size_t kSizeIndices = sizeof(uint32_t) * indices.size();
    const size_t kSizeVertices = sizeof(VertexData) * vertices.size();

    GLMeshPVP mesh(indices.data(), (uint32_t)kSizeIndices, (float*)vertices.data(), (uint32_t)kSizeVertices);

    GLTexture texAO(GL_TEXTURE_2D, "../../../deps/src/glTF-Sample-Models/2.0/DamagedHelmet/glTF/Default_AO.jpg");
    GLTexture texEmissive(GL_TEXTURE_2D, "../../../deps/src/glTF-Sample-Models/2.0/DamagedHelmet/glTF/Default_emissive.jpg");
    GLTexture texAlbedo(GL_TEXTURE_2D, "../../../deps/src/glTF-Sample-Models/2.0/DamagedHelmet/glTF/Default_albedo.jpg");
    GLTexture texMeR(GL_TEXTURE_2D, "../../../deps/src/glTF-Sample-Models/2.0/DamagedHelmet/glTF/Default_metalRoughness.jpg");
    GLTexture texNormal(GL_TEXTURE_2D, "../../../deps/src/glTF-Sample-Models/2.0/DamagedHelmet/glTF/Default_normal.jpg");

    const GLuint textures[] = { texAO.GetHandle(), texEmissive.GetHandle(), texAlbedo.GetHandle(), texMeR.GetHandle(), texNormal.GetHandle() };

    glBindTextures(0, sizeof(textures)/sizeof(textures[0]), textures);

    // cube map
    GLTexture envMap(GL_TEXTURE_CUBE_MAP, "../../../data/cubemap/spaichingen_hill_2k.hdr");
    GLTexture envMapIrradiance(GL_TEXTURE_CUBE_MAP, "../../../data/cubemap/spaichingen_hill_2k_irradiance.hdr");
    const GLuint envMaps[] = { envMap.GetHandle() , envMapIrradiance.GetHandle() };
    glBindTextures(5, 2, envMaps);

    // BRDF LUT
    GLTexture brdfLUT(GL_TEXTURE_2D, "../../../data/brdfLUT.ktx");
    glBindTextureUnit(7, brdfLUT.GetHandle());

    // model matrices
    const mat4 m(glm::scale(mat4(1.0f), vec3(2.0f)));
    GLBuffer modelMatrices(sizeof(mat4), value_ptr(m), GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, modelMatrices.GetHandle());

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

        const mat4 p = glm::perspective(45.f, ratio, 0.5f, 5000.f);
        const mat4 view = camera.GetViewMatrix();

        const PerFrameData perFrameData = {
                .view = view,
                .proj = p,
                .cameraPos = glm::vec4(camera.GetPosition(), 1.0f)
        };
        glNamedBufferSubData(perFrameDataBuffer.GetHandle(), 0, kUniformBufferSize, &perFrameData);

        const mat4 scale = glm::scale(mat4(1.0f), vec3(5.0f));
        const mat4 rot = glm::rotate(mat4(1.0f), glm::radians(90.0f), vec3(1.0f, 0.0f, 0.0f));
        const mat4 pos = glm::translate(mat4(1.0f), vec3(0.0f, 0.0f, -1.0f));
        const mat4 m = glm::rotate(scale * rot * pos, (float)glfwGetTime() * 0.1f, vec3(0.0f, 0.0f, 1.0f));
        glNamedBufferSubData(modelMatrices.GetHandle(), 0, sizeof(mat4), value_ptr(m));

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        program.UseProgram();
        mesh.Draw();

        glEnable(GL_BLEND);
        progGrid.UseProgram();
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES, 0, 6, 1, 0);

        app.SwapBuffers();
    }

    return 0;
}