#pragma once
#include "process.h"
#include <ftxui/ftxui.hpp>
#include <ftxui/component/component.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <format>

using namespace ftxui;

int safeStoi(const std::string& str, int defaultValue = 0) {
    if (str.empty()) return defaultValue;
    try {
        return std::stoi(str);
    } catch (const std::exception&) {
        return defaultValue;
    }
}

bool isPid(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isdigit(c);
    });
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::unordered_map<std::string, std::string> parseProcFile(const fs::path& filepath) {
    std::unordered_map<std::string, std::string> procDict;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        return procDict;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));
            procDict[key] = value;
        }
    }

    return procDict;
}

SystemMemory getSystemMemory() {
    auto meminfo = parseProcFile("/proc/meminfo");
    SystemMemory sysMem;

    auto parseKbToMb = [](const std::string& valStr) -> float {
        return static_cast<float>(safeStoi(valStr)) / 1024.0f;
    };

    if (meminfo.count("MemTotal")) {
        sysMem.ram_total_mb = parseKbToMb(meminfo["MemTotal"]);
    }
    if (meminfo.count("MemTotal") && meminfo.count("MemAvailable")) {
        float total = parseKbToMb(meminfo["MemTotal"]);
        float avail = parseKbToMb(meminfo["MemAvailable"]);
        sysMem.ram_used_mb = std::max(0.0f, total - avail);
    }
    if (meminfo.count("SwapTotal")) {
        sysMem.swap_total_mb = parseKbToMb(meminfo["SwapTotal"]);
    }
    if (meminfo.count("SwapTotal") && meminfo.count("SwapFree")) {
        float total = parseKbToMb(meminfo["SwapTotal"]);
        float free_swap = parseKbToMb(meminfo["SwapFree"]);
        sysMem.swap_used_mb = std::max(0.0f, total - free_swap);
    }

    return sysMem;
}

std::vector<CpuCoreStats> getCpuStats(const std::vector<CpuCoreStats>& prev_stats) {
    std::ifstream file("/proc/stat");
    std::string line;
    std::vector<CpuCoreStats> new_stats;

    while (std::getline(file, line)) {
        if (line.compare(0, 3, "cpu") != 0) continue;
        std::istringstream ss(line);
        std::string name;
        CpuData data{};
        ss >> name >> data.user >> data.nice >> data.system >> data.idle
           >> data.iowait >> data.irq >> data.softirq >> data.steal;

        CpuData prev{};
        for (const auto& p : prev_stats) {
            if (p.name == name) {
                prev = p.curr;
                break;
            }
        }

        CpuCoreStats core;
        core.name = name;
        core.prev = prev;
        core.curr = data;

        uint64_t total_diff = core.curr.getTotal() - core.prev.getTotal();
        uint64_t idle_diff = core.curr.getIdle() - core.prev.getIdle();

        if (total_diff > 0) {
            core.usage_ratio = static_cast<float>(total_diff - idle_diff) / static_cast<float>(total_diff);
        } else {
            core.usage_ratio = 0.0f;
        }

        new_stats.push_back(core);
    }
    return new_stats;
}

uint64_t getSystemTotalCpuTicks() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0;
    std::string line;
    if (std::getline(file, line) && line.compare(0, 3, "cpu") == 0) {
        std::istringstream ss(line);
        std::string name;
        uint64_t user = 0, nice = 0, system = 0, idle = 0;
        uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;
        ss >> name >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    return 0;
}

uint64_t getProcessCpuTicks(uint32_t pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    if (!file.is_open()) return 0;
    std::string line;
    if (!std::getline(file, line)) return 0;
    size_t last_paren = line.rfind(')');
    if (last_paren == std::string::npos || last_paren + 2 >= line.length()) return 0;

    std::string rest = line.substr(last_paren + 2);
    std::istringstream ss(rest);
    std::string token;
    uint64_t utime = 0, stime = 0;
    for (int i = 0; i < 13; ++i) {
        if (!(ss >> token)) return 0;
        try {
            if (i == 11) utime = std::stoull(token);
            if (i == 12) stime = std::stoull(token);
        } catch (...) {
            return 0;
        }
    }
    return utime + stime;
}

void updateProcessList(std::vector<Process>& processes,
                       SortingMode sortingMode,
                       std::unordered_map<uint32_t, uint64_t>& prev_proc_ticks,
                       uint64_t& prev_sys_ticks) {
    processes.clear();
    fs::path pidPath = "/proc";
    std::error_code ec;

    uint64_t curr_sys_ticks = getSystemTotalCpuTicks();
    uint64_t sys_diff = (curr_sys_ticks > prev_sys_ticks) ? (curr_sys_ticks - prev_sys_ticks) : 0;
    std::unordered_map<uint32_t, uint64_t> next_proc_ticks;

    for (const auto& folder : fs::directory_iterator(pidPath, ec)) {
        if (ec) continue;

        std::string filename = folder.path().filename().string();
        if (isPid(filename)) {
            fs::path programStatusPath = pidPath / filename / "status";
            auto status = parseProcFile(programStatusPath);
            
            if (status.find("Pid") == status.end()) continue;

            Process proc = {};
            proc.name = status.count("Name") ? status["Name"] : "Unknown";
            proc.pid = static_cast<uint32_t>(safeStoi(status["Pid"]));

            if (status.count("VmRSS") && status["VmRSS"].length() > 3) {
                std::string ramStr = status["VmRSS"];
                proc.ram = static_cast<float>(safeStoi(ramStr.substr(0, ramStr.length() - 3)));
            } else {
                proc.ram = 0.0f;
            }

            if (status.count("VmSwap") && status["VmSwap"].length() > 3) {
                std::string swapStr = status["VmSwap"];
                proc.swap = static_cast<float>(safeStoi(swapStr.substr(0, swapStr.length() - 3)));
            } else {
                proc.swap = 0.0f;
            }

            uint64_t curr_p_ticks = getProcessCpuTicks(proc.pid);
            uint64_t prev_p_ticks = prev_proc_ticks.count(proc.pid) ? prev_proc_ticks[proc.pid] : curr_p_ticks;
            uint64_t proc_diff = (curr_p_ticks > prev_p_ticks) ? (curr_p_ticks - prev_p_ticks) : 0;

            if (sys_diff > 0) {
                proc.cpu = (static_cast<float>(proc_diff) / static_cast<float>(sys_diff)) * 100.0f;
            } else {
                proc.cpu = 0.0f;
            }

            next_proc_ticks[proc.pid] = curr_p_ticks;
            processes.push_back(std::move(proc));
        }
    }

    prev_proc_ticks = std::move(next_proc_ticks);
    prev_sys_ticks = curr_sys_ticks;

    switch (sortingMode) {
        case CpuDescending:
            std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
                return a.cpu > b.cpu;
            });
            break;
        case CpuAscending:
            std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
                return a.cpu < b.cpu;
            });
            break;
        case MemoryDescending:
            std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
                return (a.ram + a.swap) > (b.ram + b.swap);
            });
            break;
        case MemoryAscending:
            std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
                return (a.ram + a.swap) < (b.ram + b.swap);
            });
            break;
        case PidDescending:
            std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
                return a.pid > b.pid;
            });
            break;
        case PidAscending:
            std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
                return a.pid < b.pid;
            });
            break;
    }
}

void updateProcessContainer(Component container,
                            const std::vector<Process>& processes,
                            const std::string& filterquery,
                            uint32_t& selected_pid) {
    container->DetachAllChildren();

    std::string query_lower = filterquery;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

    for (const auto& proc : processes) {
        std::string proc_name_lower = proc.name;
        std::transform(proc_name_lower.begin(), proc_name_lower.end(), proc_name_lower.begin(), ::tolower);
        std::string pid_str = std::to_string(proc.pid);

        if (!query_lower.empty() && 
            proc_name_lower.find(query_lower) == std::string::npos && 
            pid_str.find(query_lower) == std::string::npos) {
            continue;
        }

        float ramKiB = proc.ram;
        float swapKiB = proc.swap;

        std::string ramDisplay = std::to_string(static_cast<int>(ramKiB)) + " KiB";
        std::string swapDisplay = std::to_string(static_cast<int>(swapKiB)) + " KiB";

        if ((ramKiB / 1024) > 2) {
            ramDisplay = std::to_string(static_cast<int>(ramKiB / 1024)) + " MiB";
            if (((ramKiB / 1024) / 1024) > 2) {
                ramDisplay = std::to_string(static_cast<int>((ramKiB / 1024) / 1024)) + " GiB";
            }
        }

        if ((swapKiB / 1024) > 2) {
            swapDisplay = std::to_string(static_cast<int>(swapKiB / 1024)) + " MiB";
            if (((swapKiB / 1024) / 1024) > 2) {
                swapDisplay = std::to_string(static_cast<int>((swapKiB / 1024) / 1024)) + " GiB";
            }
        }

        std::string prefix = (selected_pid == proc.pid) ? "[x] " : "[ ] ";
        std::string label = std::format("{}Name: {} | Pid: {} | CPU: {:.1f}% | Ram: {} | Swap: {}", 
                                        prefix, proc.name, proc.pid, proc.cpu, ramDisplay, swapDisplay);

        auto btn = Button(label, [&selected_pid, p = proc.pid] {
            selected_pid = p;
        });

        container->Add(btn);
    }
}


