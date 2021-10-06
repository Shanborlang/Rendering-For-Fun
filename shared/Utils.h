#pragma once

#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <malloc.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>

int EndsWith(const char* s, const char* ext);

std::string ReadShaderFile(const char* fileName);

void PrintShaderSource(const char* text);

template<typename T>
inline void mergeVectors(std::vector<T>& v1, const std::vector<T>& v2) {
    v1.insert(v1.end(), v2.begin(), v2.end());
}