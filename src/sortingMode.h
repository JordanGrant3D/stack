#pragma once

enum SortingMode {
    CpuDescending,
    CpuAscending,
    MemoryDescending,
    MemoryAscending,
    PidDescending,
    PidAscending, 
};

SortingMode& operator++(SortingMode& mode) {
    using Underlying = std::underlying_type_t<SortingMode>;
    auto current = static_cast<Underlying>(mode);
    auto total = static_cast<Underlying>(SortingMode::PidAscending + 1);
    
    mode = static_cast<SortingMode>((current + 1) % total);
    return mode;
}

SortingMode operator++(SortingMode& mode, int) {
    SortingMode temp = mode;
    ++mode;
    return temp;
}




