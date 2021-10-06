#include "GLShader.h"
#include "shared/Utils.h"

#include <cassert>

GLenum GLShaderTypeFromFileName(const char* fileName) {
    if (EndsWith(fileName, ".vert"))
        return GL_VERTEX_SHADER;

    if (EndsWith(fileName, ".frag"))
        return GL_FRAGMENT_SHADER;

    if (EndsWith(fileName, ".geom"))
        return GL_GEOMETRY_SHADER;

    if (EndsWith(fileName, ".tesc"))
        return GL_TESS_CONTROL_SHADER;

    if (EndsWith(fileName, ".tese"))
        return GL_TESS_EVALUATION_SHADER;

    if (EndsWith(fileName, ".comp"))
        return GL_COMPUTE_SHADER;

    assert(false);

    return 0;
}

GLShader::GLShader(const char *fileName)
: GLShader(GLShaderTypeFromFileName(fileName), ReadShaderFile(fileName).c_str(), fileName){}

GLShader::GLShader(GLenum type, const char *text, const char *debugFileName)
: mType(type)
, mHandle(glCreateShader(type))
{
    glShaderSource(mHandle, 1, &text, nullptr);
    glCompileShader(mHandle);

    char buffer[8192];
    GLsizei length = 0;
    glGetShaderInfoLog(mHandle, sizeof(buffer), &length, buffer);
    if(length) {
        std::printf("%s (File: %s)\n", buffer, debugFileName);
        PrintShaderSource(text);
        assert(false);
    }
}

GLShader::~GLShader() {
    glDeleteShader(mHandle);
}

void PrintProgramInfoLog(GLuint handle) {
    char buffer[8192];
    GLsizei length = 0;
    glGetProgramInfoLog(handle, sizeof(buffer), &length, buffer);
    if(length) {
        std::printf("%s\n", buffer);
        assert(false);
    }
}

GLProgram::GLProgram(const GLShader &a)
        : mHandle(glCreateProgram()){
    glAttachShader(mHandle, a.GetHandle());
    glLinkProgram(mHandle);
    PrintProgramInfoLog(mHandle);
}

GLProgram::GLProgram(const GLShader &a, const GLShader &b)
        : mHandle(glCreateProgram()){
    glAttachShader(mHandle, a.GetHandle());
    glAttachShader(mHandle, b.GetHandle());
    glLinkProgram(mHandle);
    PrintProgramInfoLog(mHandle);
}

GLProgram::GLProgram(const GLShader &a, const GLShader &b, const GLShader &c)
        : mHandle(glCreateProgram()){
    glAttachShader(mHandle, a.GetHandle());
    glAttachShader(mHandle, b.GetHandle());
    glAttachShader(mHandle, c.GetHandle());
    glLinkProgram(mHandle);
    PrintProgramInfoLog(mHandle);
}

GLProgram::GLProgram(const GLShader &a, const GLShader &b, const GLShader &c, const GLShader &d, const GLShader &e)
        : mHandle(glCreateProgram()){
    glAttachShader(mHandle, a.GetHandle());
    glAttachShader(mHandle, b.GetHandle());
    glAttachShader(mHandle, c.GetHandle());
    glAttachShader(mHandle, d.GetHandle());
    glAttachShader(mHandle, e.GetHandle());
    glLinkProgram(mHandle);
    PrintProgramInfoLog(mHandle);
}

GLProgram::~GLProgram() {
    glDeleteProgram(mHandle);
}

void GLProgram::UseProgram() const {
    glUseProgram(mHandle);
}

GLBuffer::GLBuffer(GLsizeiptr size, const void *data, GLbitfield flags) {
    glCreateBuffers(1, &mHandle);
    glNamedBufferStorage(mHandle, size, data, flags);
}

GLBuffer::~GLBuffer() {
    glDeleteBuffers(1, &mHandle);
}
