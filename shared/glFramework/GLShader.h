#pragma once

#include <glad/gl.h>

class GLShader {
public:
    GLShader() = default;
    GLShader(const GLShader&) = delete;
    GLShader(GLShader&&) = delete;
    explicit GLShader(const char* fileName);
    GLShader(GLenum type, const char* text, const char* debugFileName = "");
    ~GLShader();

    GLenum GetType() const { return mType; }
    GLuint GetHandle() const { return mHandle; }

private:
    GLenum mType;
    GLuint mHandle;
};

class GLProgram {
public:
    GLProgram(const GLShader& a);
    GLProgram(const GLShader& a, const GLShader& b);
    GLProgram(const GLShader& a, const GLShader& b, const GLShader& c);
    GLProgram(const GLShader& a, const GLShader& b, const GLShader& c, const GLShader& d, const GLShader& e);
    ~GLProgram();

    void UseProgram() const;
    GLuint GetHandle() const { return mHandle; }

private:
    GLuint mHandle;
};

GLenum GLShaderTypeFromFileName(const char* fileName);

class GLBuffer {
public:
    GLBuffer() = default;
    GLBuffer(const GLBuffer&) = delete;
    GLBuffer(GLBuffer&&) = delete;
    explicit GLBuffer(GLsizeiptr size, const void* data, GLbitfield flags);
    ~GLBuffer();

    GLuint GetHandle() const { return mHandle; }

private:
    GLuint mHandle{};
};