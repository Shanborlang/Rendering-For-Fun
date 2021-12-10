#include "GLTexture.h"

#include "shared/Bitmap.h"
#include "shared/UtilsCubemap.h"

#include <cassert>
#include <cstdio>
#include <string>

#include <stb/stb_image.h>
#include <gli/gli.hpp>
#include <gli/texture2d.hpp>
#include <gli/load_ktx.hpp>

int GetNumMipMapLevels2D(int w, int h) {
    int levels = 1;
    while((w | h) >> levels)
        levels += 1;
    return levels;
}

// Draw a checkerboard on a pre-allocated square RGB image.
uint8_t* GenDefaultCheckerboardImage(int *width, int* height) {
    const int w = 128;
    const int h = 128;

    auto* imgData = (uint8_t*)malloc(w * h * 3);

    assert(imgData && w > 0 && h > 0);
    assert(w == h);

    if(!imgData || w <= 0 || h <= 0) return nullptr;
    if(w != h) return nullptr;

    for(int i = 0;i < w * h; i++) {
        const int row = i / w;
        const int col = i % w;
        imgData[i * 3 + 0] = imgData[i * 3 + 1] = imgData[i * 3 + 2] = 0xFF * ((row + col) % 2);
    }

    if(width) *width = w;
    if(height) *height = h;

    return imgData;
}

GLTexture::GLTexture(GLenum type, const char *fileName)
    : mType(type) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glCreateTextures(type, 1, &mHandle);
    glTextureParameteri(mHandle, GL_TEXTURE_MAX_LEVEL, 0);
    glTextureParameteri(mHandle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(mHandle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const char* ext = strrchr(fileName, '.');

    const  bool isKTX = ext && !strcmp(ext, ".ktx");

    switch (type) {
        case GL_TEXTURE_2D: {
            int w = 0;
            int h = 0;
            int numMipmaps = 0;
            if(isKTX) {
                gli::texture gliTex = gli::load_ktx(fileName);
                gli::gl GL(gli::gl::PROFILE_KTX);
                gli::gl::format const format = GL.translate(gliTex.format(), gliTex.swizzles());
                glm::tvec3<GLsizei> extent(gliTex.extent(0));
                w = extent.x;
                h = extent.y;
                numMipmaps = GetNumMipMapLevels2D(w, h);
                glTextureStorage2D(mHandle, numMipmaps, format.Internal, w, h);
                glTextureSubImage2D(mHandle, 0, 0, 0, w, h, format.External, format.Type, gliTex.data(0, 0, 0));
            }else {
                uint8_t* img = stbi_load(fileName, &w, &h, nullptr, STBI_rgb_alpha);

                // Note(Anton): replaced assert(img) with a fallback image to prevent crashes with missing files or bad (eg very long) paths.
                if (!img)
                {
                    fprintf(stderr, "WARNING: could not load image `%s`, using a fallback.\n", fileName);
                    img = GenDefaultCheckerboardImage(&w, &h);
                    if (!img)
                    {
                        fprintf(stderr, "FATAL ERROR: out of memory allocating image for fallback texture\n");
                        exit(EXIT_FAILURE);
                    }
                }

                numMipmaps = GetNumMipMapLevels2D(w, h);
                glTextureStorage2D(mHandle, numMipmaps, GL_RGBA8, w, h);
                glTextureSubImage2D(mHandle, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img);
                stbi_image_free((void*)img);
            }
            glGenerateTextureMipmap(mHandle);
            glTextureParameteri(mHandle, GL_TEXTURE_MAX_LEVEL, numMipmaps-1);
            glTextureParameteri(mHandle, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(mHandle, GL_TEXTURE_MAX_ANISOTROPY , 16);
            break;
        }
        case GL_TEXTURE_CUBE_MAP: {
            int w, h, comp;
            const float* img = stbi_loadf(fileName, &w, &h, &comp, 3);
            assert(img);
            Bitmap in(w, h, comp, eBitmapFormat::Float, img);
            const bool isEquirectangular = w == 2 * h;
            Bitmap out = isEquirectangular ? ConvertEquirectangularMapToVerticalCross(in) : in;
            stbi_image_free((void*)img);
            Bitmap cubemap = ConvertVerticalCrossToCubeMapFaces(out);

            const int numMipmaps = GetNumMipMapLevels2D(cubemap.mW, cubemap.mH);

            glTextureParameteri(mHandle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(mHandle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(mHandle, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTextureParameteri(mHandle, GL_TEXTURE_BASE_LEVEL, 0);
            glTextureParameteri(mHandle, GL_TEXTURE_MAX_LEVEL, numMipmaps-1);
            glTextureParameteri(mHandle, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(mHandle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            glTextureStorage2D(mHandle, numMipmaps, GL_RGB32F, cubemap.mW, cubemap.mH);
            const uint8_t* data = cubemap.mData.data();

            for (unsigned i = 0; i != 6; ++i)
            {
                glTextureSubImage3D(mHandle, 0, 0, 0, i, cubemap.mW, cubemap.mH, 1, GL_RGB, GL_FLOAT, data);
                data += cubemap.mW * cubemap.mH * cubemap.mComp * Bitmap::sGetBytesPerComponent(cubemap.mFmt);
            }

            glGenerateTextureMipmap(mHandle);
            break;
        }
        default:
            assert(false);
    }
    mHandleBindless = glGetTextureHandleARB(mHandle);
    glMakeTextureHandleResidentARB(mHandleBindless);
}

GLTexture::GLTexture(GLenum type, int width, int height, GLenum internalFormat)
: mType(type) {
    glCreateTextures(type, 1, &mHandle);
    glTextureParameteri(mHandle, GL_TEXTURE_MAX_LEVEL, 0);
    glTextureParameteri(mHandle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(mHandle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureStorage2D(mHandle, GetNumMipMapLevels2D(width, height), internalFormat, width, height);
}

GLTexture::GLTexture(int w, int h, const void *img)
    : mType(GL_TEXTURE_2D) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glCreateTextures(mType, 1, &mHandle);
    int numMipmaps = GetNumMipMapLevels2D(w, h);
    glTextureStorage2D(mHandle, numMipmaps, GL_RGBA8, w, h);
    glTextureSubImage2D(mHandle, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glGenerateTextureMipmap(mHandle);
    glTextureParameteri(mHandle, GL_TEXTURE_MAX_LEVEL, numMipmaps - 1);
    glTextureParameteri(mHandle, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(mHandle, GL_TEXTURE_MAX_ANISOTROPY, 16);
    mHandleBindless = glGetTextureHandleARB(mHandle);
    glMakeTextureHandleResidentARB(mHandleBindless);
}

GLTexture::~GLTexture() {
    if(mHandleBindless)
        glMakeTextureHandleNonResidentARB(mHandleBindless);
    glDeleteTextures(1, &mHandle);
}

GLTexture::GLTexture(GLTexture &&other) noexcept
    : mType(other.mType)
    , mHandle(other.mHandle)
    , mHandleBindless(other.mHandleBindless) {
    other.mType = 0;
    other.mHandle = 0;
    other.mHandleBindless = 0;
}
