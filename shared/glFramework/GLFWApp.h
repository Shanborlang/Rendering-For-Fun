#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include "shared/debug.h"

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

class GLApp {
public:
    GLApp() {
        glfwSetErrorCallback(
                [](int error, const char* description) {
                    fprintf(stderr, "Error: %s\n", description);
                }
                );

        if(!glfwInit()) exit(EXIT_FAILURE);

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

        const GLFWvidmode* info = glfwGetVideoMode(glfwGetPrimaryMonitor());

        mWindow = glfwCreateWindow(info->width, info->height, "Window", nullptr, nullptr);

        if(!mWindow) {
            glfwTerminate();
            exit(EXIT_FAILURE);
        }

        glfwMakeContextCurrent(mWindow);
        gladLoadGL(glfwGetProcAddress);
        glfwSwapInterval(0);

        initDebug();
    }

    ~GLApp() {
        glfwDestroyWindow(mWindow);
        glfwTerminate();
    }

    [[nodiscard]] GLFWwindow* GetWindow() const { return mWindow; }
    [[nodiscard]] float GetDeltaSeconds() const { return mDeltaSeconds; }

    void SwapBuffers() {
        glfwSwapBuffers(mWindow);
        glfwPollEvents();
        auto err = glGetError();
        assert(err == GL_NO_ERROR);

        const double newTimeStamp = glfwGetTime();
        mDeltaSeconds = static_cast<float>(newTimeStamp - mTimeStamp);
        mTimeStamp = newTimeStamp;
    }

private:
    GLFWwindow* mWindow = nullptr;
    double mTimeStamp = glfwGetTime();
    float mDeltaSeconds = 0.f;
};