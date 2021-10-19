#pragma once

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <cstring>

#ifdef __GNUC__
#define PACKED_STRUCT __attribute__((packed, aligned(1)))
#else
#define PACKED_STRUCT
#endif

struct PACKED_STRUCT gpuvec4 {
    float x, y, z, w;

    gpuvec4() = default;
    explicit gpuvec4(float v) : x(v), y(v), z(v), w(v) {}
    gpuvec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    explicit gpuvec4(const glm::vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
};

struct PACKED_STRUCT gpumat4 {
    float mData[16];

    gpumat4() = default;
    explicit gpumat4(const glm::mat4& m) { memcpy(mData, glm::value_ptr(m), 16 * sizeof(float)); }
};