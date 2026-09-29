#pragma once

struct CpuCoreStats {
    std::string name;
    CpuData prev;
    CpuData curr;
    float usage_ratio = 0.0f; // 0.0 to 1.0
};


