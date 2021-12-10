#pragma once

#include <glad/gl.h>

class GLTexture {
public:
    GLTexture(GLenum type, const char* fileName);
    GLTexture(GLenum type, int width, int height, GLenum internalFormat);
    GLTexture(int w, int h, const void* img);
    ~GLTexture();
    GLTexture(const GLTexture&) = delete;
    GLTexture(GLTexture&&) noexcept ;
    [[nodiscard]] GLenum GetType() const { return mType; }
    GLuint GetHandle() const { return mHandle; }
    GLuint64 GetHandleBindless() const { return mHandleBindless; }


private:
    GLenum mType = 0;
    GLuint mHandle = 0;
    GLuint64 mHandleBindless = 0;
};