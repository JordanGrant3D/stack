#pragma once
struct CpuData {
    uint64_t user = 0, nice = 0, system = 0, idle = 0;
    uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;

    uint64_t getTotal() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    uint64_t getIdle() const {
        return idle + iowait;
    }
};


