#include "UtilsCubemap.h"

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <algorithm>

using glm::vec3;
using glm::vec4;
using glm::ivec2;

vec3 FaceCoordsToXYZ(int i, int j, int faceID, int faceSize)
{
    const float A = 2.0f * float(i) / faceSize;
    const float B = 2.0f * float(j) / faceSize;

    if (faceID == 0) return {-1.0f, A - 1.0f, B - 1.0f};
    if (faceID == 1) return {A - 1.0f, -1.0f, 1.0f - B};
    if (faceID == 2) return {1.0f, A - 1.0f, 1.0f - B};
    if (faceID == 3) return {1.0f - A, 1.0f, 1.0f - B};
    if (faceID == 4) return {B - 1.0f, A - 1.0f, 1.0f};
    if (faceID == 5) return {1.0f - B, A - 1.0f, -1.0f};

    return {};
}

Bitmap ConvertEquirectangularMapToVerticalCross(const Bitmap& b) {
    if (b.mType != eBitmapType::TwoD) return {};
    const int faceSize = b.mW / 4;
    const int w = faceSize * 3;
    const int h = faceSize * 4;
    Bitmap result(w, h, b.mComp, b.mFmt);

    const ivec2 kFaceOffsets[] = {
            ivec2(faceSize, faceSize * 3),
            ivec2(0, faceSize),
            ivec2(faceSize, faceSize),
            ivec2(faceSize * 2, faceSize),
            ivec2(faceSize, 0),
            ivec2(faceSize, faceSize * 2)
    };

    const int clampW = b.mW - 1;
    const int clampH = b.mH - 1;

    for (int face = 0; face != 6; face++)
    {
        for (int i = 0; i != faceSize; i++)
        {
            for (int j = 0; j != faceSize; j++)
            {
                const vec3 P = FaceCoordsToXYZ(i, j, face, faceSize);
                const float R = hypot(P.x, P.y);
                const float theta = atan2(P.y, P.x);
                const float phi = atan2(P.z, R);
                //	float point source coordinates
                const float Uf = float(2.0f * faceSize * (theta + glm::pi<float>()) / glm::pi<float>());
                const float Vf = float(2.0f * faceSize * (glm::pi<float>() / 2.0f - phi) / glm::pi<float>());
                // 4-samples for bilinear interpolation
                const int U1 = std::clamp(int(floor(Uf)), 0, clampW);
                const int V1 = std::clamp(int(floor(Vf)), 0, clampH);
                const int U2 = std::clamp(U1 + 1, 0, clampW);
                const int V2 = std::clamp(V1 + 1, 0, clampH);
                // fractional part
                const float s = Uf - (float)U1;
                const float t = Vf - (float)V1;
                // fetch 4-samples
                const vec4 A = b.GetPixel(U1, V1);
                const vec4 B = b.GetPixel(U2, V1);
                const vec4 C = b.GetPixel(U1, V2);
                const vec4 D = b.GetPixel(U2, V2);
                // bilinear interpolation
                const vec4 color = A * (1 - s) * (1 - t) + B * (s) * (1 - t) + C * (1 - s) * t + D * (s) * (t);
                result.SetPixel(i + kFaceOffsets[face].x, j + kFaceOffsets[face].y, color);
            }
        };
    }
    return result;
}

Bitmap ConvertVerticalCrossToCubeMapFaces(const Bitmap& b) {
    const int faceWidth = b.mW / 3;
    const int faceHeight = b.mH / 4;

    Bitmap cubemap(faceWidth, faceHeight, 6, b.mComp, b.mFmt);

    const uint8_t * src = b.mData.data();
    uint8_t* dst = cubemap.mData.data();
    const int pixelSize = cubemap.mComp * Bitmap::sGetBytesPerComponent(cubemap.mFmt);

    for (int face = 0; face != 6; ++face)
    {
        for (int j = 0; j != faceHeight; ++j)
        {
            for (int i = 0; i != faceWidth; ++i)
            {
                int x = 0;
                int y = 0;

                switch (face)
                {
                    // GL_TEXTURE_CUBE_MAP_POSITIVE_X
                    case 0:
                        x = i;
                        y = faceHeight + j;
                        break;

                        // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
                    case 1:
                        x = 2 * faceWidth + i;
                        y = 1 * faceHeight + j;
                        break;

                        // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
                    case 2:
                        x = 2 * faceWidth - (i + 1);
                        y = 1 * faceHeight - (j + 1);
                        break;

                        // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
                    case 3:
                        x = 2 * faceWidth - (i + 1);
                        y = 3 * faceHeight - (j + 1);
                        break;

                        // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
                    case 4:
                        x = 2 * faceWidth - (i + 1);
                        y = b.mH - (j + 1);
                        break;

                        // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
                    case 5:
                        x = faceWidth + i;
                        y = faceHeight + j;
                        break;
                }

                memcpy(dst, src + (y * b.mW + x) * pixelSize, pixelSize);

                dst += pixelSize;
            }
        }
    }

    return cubemap;
}