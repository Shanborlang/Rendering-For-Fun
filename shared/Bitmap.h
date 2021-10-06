#pragma once

#include <cstring>
#include <vector>

#include <glm/glm.hpp>

enum class eBitmapType {
    TwoD,
    Cube
};

enum class eBitmapFormat {
    UnsignedByte,
    Float
};

/// R/RG/RGB/RGBA bitmaps
struct Bitmap {
    int mW = 0;
    int mH = 0;
    int mD = 1;
    int mComp = 3;
    eBitmapFormat mFmt = eBitmapFormat::UnsignedByte;
    eBitmapType mType = eBitmapType::TwoD;
    std::vector<uint8_t> mData;

    Bitmap() = default;
    Bitmap(int w, int h, int comp, eBitmapFormat fmt)
            : mW(w), mH(h), mComp(comp), mFmt(fmt), mData(w * h * comp * sGetBytesPerComponent(fmt)) {
        InitGetSetFuncs();
    }

    Bitmap(int w, int h, int d, int comp, eBitmapFormat fmt)
            : mW(w), mH(h), mD(d), mComp(comp), mFmt(fmt), mData(w * h * d *comp * sGetBytesPerComponent(fmt)) {
        InitGetSetFuncs();
    }

    Bitmap(int w, int h, int comp, eBitmapFormat fmt, const void* ptr)
            : mW(w), mH(h), mComp(comp), mFmt(fmt), mData(w * h * comp * sGetBytesPerComponent(fmt)) {
        InitGetSetFuncs();
        memcpy(mData.data(), ptr, mData.size());
    }

    static int sGetBytesPerComponent(eBitmapFormat fmt) {
        if (fmt == eBitmapFormat::UnsignedByte) return 1;
        if (fmt == eBitmapFormat::Float) return 4;
        return 0;
    }

    void SetPixel(int x, int y, const glm::vec4& c) {
        (*this.*SetPixelFunc)(x, y, c);
    }

    glm::vec4 GetPixel(int x, int y) const {
        return ((*this.*GetPixelFunc)(x, y));
    }

private:
    using SetPixel_t = void(Bitmap::*)(int, int, const glm::vec4&);
    using GetPixel_t = glm::vec4(Bitmap::*)(int, int) const;
    SetPixel_t  SetPixelFunc = &Bitmap::SetPixelUnsignedByte;
    GetPixel_t  GetPixelFunc = &Bitmap::GetPixelUnsignedByte;

    void InitGetSetFuncs() {
        switch (mFmt) {
            case eBitmapFormat::UnsignedByte:
                SetPixelFunc = &Bitmap::SetPixelUnsignedByte;
                GetPixelFunc = &Bitmap::GetPixelUnsignedByte;
                break;
            case eBitmapFormat::Float:
                SetPixelFunc = &Bitmap::SetPixelFloat;
                GetPixelFunc = &Bitmap::GetPixelFloat;
                break;
        }
    }

    void SetPixelFloat(int x, int y, const glm::vec4& c) {
        const int ofs = mComp * (y * mW + x);
        auto data = reinterpret_cast<float*>(mData.data());
        if(mComp > 0) data[ofs + 0] = c.x;
        if(mComp > 1) data[ofs + 1] = c.y;
        if(mComp > 2) data[ofs + 2] = c.z;
        if(mComp > 3) data[ofs + 3] = c.w;
    }

    glm::vec4  GetPixelFloat(int x, int y) const {
        const int ofs = mComp * (y * mW + x);
        auto data = reinterpret_cast<const float*>(mData.data());
        return {
        mComp > 0 ? data[ofs + 0] : 0.0f,
        mComp > 1 ? data[ofs + 1] : 0.0f,
        mComp > 2 ? data[ofs + 2] : 0.0f,
        mComp > 3 ? data[ofs + 3] : 0.0f
        };
    }

    void SetPixelUnsignedByte(int x, int y, const glm::vec4& c) {
        const int ofs = mComp * (y * mW + x);
        if(mComp > 0) mData[ofs + 0] = uint8_t(c.x * 255.0f);
        if(mComp > 1) mData[ofs + 1] = uint8_t(c.y * 255.0f);
        if(mComp > 2) mData[ofs + 2] = uint8_t(c.z * 255.0f);
        if(mComp > 3) mData[ofs + 3] = uint8_t(c.w * 255.0f);
    }

    glm::vec4 GetPixelUnsignedByte(int x, int y) const {
        const int ofs = mComp * (y * mW + x);
        return  {
            mComp > 0 ? float(mData[ofs + 0]) / 255.0f : 0.0f,
            mComp > 1 ? float(mData[ofs + 1]) / 255.0f : 0.0f,
            mComp > 2 ? float(mData[ofs + 2]) / 255.0f : 0.0f,
            mComp > 3 ? float(mData[ofs + 3]) / 255.0f : 0.0f
        };
    }
};