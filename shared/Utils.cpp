#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "Utils.h"

#include <malloc.h>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>
#include <cassert>
#include <sys/stat.h>
#include <sstream>

int EndsWith(const char* s, const char* ext) {
    return (strstr(s, ext) - s == (strlen(s) - strlen(ext)));
}

void PrintShaderSource(const char* text) {
    int line = 1;
    std::printf("\n(%3i) ", line);

    while (text && *text++) {
        if(*text == '\n') std::printf("\n(%3i) ", ++line);
        else if(*text == '\r') {}
        else std::printf("%c", *text);
    }
    std::printf("\n");
}

bool file_exists(const char* fileName) {
    struct stat info;
    int ret = -1;

    ret = stat(fileName, &info);
    return 0 == ret;
}

std::string readFile(const char* filename) {
    std::ifstream  inFile(filename, std::ios::in);
    if(!inFile.is_open()) {
        std::cout << "Unable to open: " << filename << "\n";
        assert(false);
    }

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    return buffer.str();
}

std::string ReadShaderFile(const char* fileName) {

    std::ifstream  inFile(fileName, std::ios::in);
    if(!inFile.is_open()) {
        std::cout << "Unable to open: " << fileName << "\n";
        assert(false);
    }

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    std::string code(buffer.str());

    while (code.find("#include ") != std::string::npos) {
        const auto pos = code.find("#include ");
        const auto p1 = code.find('<', pos);
        const auto p2 = code.find('>', pos);
        if (p1 == std::string::npos || p2 == std::string::npos || p2 <= p1)
        {
            printf("Error while loading shader program: %s\n", code.c_str());
            return {};
        }
        const std::string name = code.substr(p1 + 1, p2 - p1 - 1);
        const std::string include = readFile(name.c_str());
        code.replace(pos, p2-pos+1, include.c_str());
    }

    return code;
}
