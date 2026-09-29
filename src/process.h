#pragma once
#include <filesystem>
#include "sortingMode.h"
namespace fs = std::filesystem;


class Process {
public:
    std::string name;
    uint32_t pid;
    float ram;
    float swap;
    float cpu = 0.0f;
};





