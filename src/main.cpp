#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <format>
#include <csignal>
#include <sys/types.h>

#include <ftxui/ftxui.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/canvas.hpp>

using namespace ftxui;
namespace fs = std::filesystem;

class Process {
public:
    std::string name;
    uint32_t pid;
    float ram;
    float swap;
    float cpu = 0.0f;
};

struct SystemMemory {
    float ram_used_mb = 0.0f;
    float ram_total_mb = 0.0f;
    float swap_used_mb = 0.0f;
    float swap_total_mb = 0.0f;
};

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

struct CpuCoreStats {
    std::string name;
    CpuData prev;
    CpuData curr;
    float usage_ratio = 0.0f; // 0.0 to 1.0
};

enum SortingMode {
    CpuDescending,
    CpuAscending,
    MemoryDescending,
    MemoryAscending,
    PidDescending,
    PidAscending, 
};

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

int main() {
    SortingMode sortingMode = SortingMode::CpuDescending;
    std::vector<Process> processes = {};
    std::string cycleTitle = "Sort: CpuDesc";
    std::string search_query = "";
    uint32_t selected_pid = 0;

    // Navigation State: 0 = Processes, 1 = Memory, 2 = CPU
    int selected_tab = 0;
    std::vector<std::string> tab_entries = {"Processes", "Memory", "CPU"};

    // System Metrics Buffers
    std::vector<float> ram_history;
    std::vector<float> swap_history;
    SystemMemory sysMem;
    std::vector<CpuCoreStats> cpu_stats;

    // Process CPU Tracking Buffers
    std::unordered_map<uint32_t, uint64_t> prev_proc_ticks;
    uint64_t prev_sys_ticks = 0;

    auto container = Container::Vertical({});

    auto tab_toggle = Toggle(&tab_entries, &selected_tab);

    auto tab_bar = Container::Horizontal({
        tab_toggle,
    });

    // Process Controls inside Processes Tab
    auto search_input = Input(&search_query, "Search process...");
    auto btn_cycle_sorting = Button(&cycleTitle, [&]{ sortingMode++; });
    
    auto btn_end_process = Button("End Process", [&] {
        if (selected_pid > 0) {
            kill(static_cast<pid_t>(selected_pid), SIGTERM);
            selected_pid = 0;
        }
    }) | CatchEvent([](Event event) { return false; });

    auto process_controls = Container::Horizontal({
        search_input,
        btn_cycle_sorting,
        btn_end_process,
    });

    auto process_tab_container = Container::Vertical({
        process_controls,
        container,
    });

    // Processes View Renderer
    auto process_view = Renderer(process_tab_container, [&] {
        auto styled_end_btn = btn_end_process->Render() | color(Color::Red) | bold;

        return vbox({
            hbox({
                text(" Search: ") | center,
                search_input->Render() | flex | border,
                btn_cycle_sorting->Render(),
                styled_end_btn,
            }),
            separator(),
            container->Render()
                | vscroll_indicator
                | frame
                | flex
        }) | border | flex;
    });

    // Memory View Renderer
    auto memory_view = Renderer([&] {
        float ram_gb = sysMem.ram_used_mb / 1024.0f;
        float max_ram_gb = sysMem.ram_total_mb / 1024.0f;
        float swap_gb = sysMem.swap_used_mb / 1024.0f;
        float max_swap_gb = sysMem.swap_total_mb / 1024.0f;

        auto graph_canvas = canvas([&](Canvas& c) {
            int width = c.width();
            int height = c.height();
            int history_size = static_cast<int>(ram_history.size());

            if (history_size < 2) return;

            for (int x = 0; x < width - 1; ++x) {
                int idx1 = history_size - width + x;
                int idx2 = history_size - width + (x + 1);

                if (idx1 >= 0 && idx2 < history_size) {
                    float max_ram = (sysMem.ram_total_mb > 0) ? sysMem.ram_total_mb : 1.0f;
                    float max_swap = (sysMem.swap_total_mb > 0) ? sysMem.swap_total_mb : 1.0f;

                    int ram_y1 = (height - 1) - static_cast<int>((ram_history[idx1] / max_ram) * (height - 1));
                    int ram_y2 = (height - 1) - static_cast<int>((ram_history[idx2] / max_ram) * (height - 1));

                    int swap_y1 = (height - 1) - static_cast<int>((swap_history[idx1] / max_swap) * (height - 1));
                    int swap_y2 = (height - 1) - static_cast<int>((swap_history[idx2] / max_swap) * (height - 1));

                    ram_y1 = std::clamp(ram_y1, 0, height - 1);
                    ram_y2 = std::clamp(ram_y2, 0, height - 1);
                    swap_y1 = std::clamp(swap_y1, 0, height - 1);
                    swap_y2 = std::clamp(swap_y2, 0, height - 1);

                    c.DrawBlockLine(x, ram_y1, x + 1, ram_y2, Color::Cyan);
                    c.DrawBlockLine(x, swap_y1, x + 1, swap_y2, Color::Magenta);
                }
            }
        });

        return vbox({
            hbox({
                text("  RAM Usage: ") | bold,
                text(std::format("{:.2f} / {:.2f} GiB", ram_gb, max_ram_gb)) | color(Color::Cyan),
                separator(),
                text("  SWAP Usage: ") | bold,
                text(std::format("{:.2f} / {:.2f} GiB", swap_gb, max_swap_gb)) | color(Color::Magenta),
            }),
            separator(),
            graph_canvas | flex,
        }) | border | flex;
    });

    // CPU View Renderer
    auto cpu_view = Renderer([&] {
        if (cpu_stats.empty()) {
            return text(" Gathering CPU data... ") | center;
        }

        Elements core_elements;
        float total_combined_pct = 0.0f;
        int num_cores = 0;

        for (size_t i = 1; i < cpu_stats.size(); ++i) {
            const auto& core = cpu_stats[i];
            float pct = core.usage_ratio * 100.0f;
            total_combined_pct += pct;
            num_cores++;

            core_elements.push_back(
                hbox({
                    text(std::format(" {:<6}", core.name)) | bold,
                    gauge(core.usage_ratio) | color(Color::Green) | flex,
                    text(std::format(" {:5.1f}% ", pct)),
                })
            );
        }

        float max_combined_pct = static_cast<float>(num_cores) * 100.0f;
        float overall_avg_ratio = (cpu_stats.size() > 0) ? cpu_stats[0].usage_ratio : 0.0f;

        auto summary_header = vbox({
            hbox({
                text("  Combined Total Usage: ") | bold,
                text(std::format("{:.1f}% / {:.1f}%", total_combined_pct, max_combined_pct)) | color(Color::Yellow) | bold,
            }),
            hbox({
                text("  Overall CPU Gauge:    ") | bold,
                gauge(overall_avg_ratio) | color(Color::Yellow) | flex,
                text(std::format(" {:.1f}% ", overall_avg_ratio * 100.0f)),
            }),
        });

        Elements col1, col2;
        for (size_t i = 0; i < core_elements.size(); ++i) {
            if (i < (core_elements.size() + 1) / 2) {
                col1.push_back(core_elements[i]);
            } else {
                col2.push_back(core_elements[i]);
            }
        }

        Element cores_grid;
        if (!col2.empty()) {
            cores_grid = hbox({
                vbox(std::move(col1)) | flex,
                separator(),
                vbox(std::move(col2)) | flex,
            });
        } else {
            cores_grid = vbox(std::move(col1));
        }

        return vbox({
            summary_header,
            separator(),
            cores_grid | vscroll_indicator | frame | flex,
        }) | border | flex;
    });

    // Tab view switcher
    auto tab_content = Container::Tab({
        process_view,
        memory_view,
        cpu_view,
    }, &selected_tab);

    auto main_container = Container::Vertical({
        tab_bar,
        tab_content,
    });

    auto scroll_box = Renderer(main_container, [&] {
        return vbox({ 
            tab_bar->Render(),
            tab_content->Render() | flex,
        }) | border;
    });

    auto screen = ScreenInteractive::Fullscreen();

    // Initial Data Fetch
    sysMem = getSystemMemory();
    cpu_stats = getCpuStats(cpu_stats);
    updateProcessList(processes, sortingMode, prev_proc_ticks, prev_sys_ticks);
    updateProcessContainer(container, processes, search_query, selected_pid);

    std::atomic<bool> running = true;

    std::thread refresh_thread([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            sysMem = getSystemMemory();
            ram_history.push_back(sysMem.ram_used_mb);
            swap_history.push_back(sysMem.swap_used_mb);

            if (ram_history.size() > 500) {
                ram_history.erase(ram_history.begin());
                swap_history.erase(swap_history.begin());
            }

            cpu_stats = getCpuStats(cpu_stats);

            screen.Post([&] {
                if (selected_tab == 0) {
                    updateProcessList(processes, sortingMode, prev_proc_ticks, prev_sys_ticks);
                    updateProcessContainer(container, processes, search_query, selected_pid);
                }

                switch (sortingMode) {
                  case CpuDescending:   cycleTitle = "Sort: CpuDesc"; break;
                  case CpuAscending:    cycleTitle = "Sort: CpuAsc"; break;
                  case MemoryDescending: cycleTitle = "Sort: MemDesc"; break;
                  case MemoryAscending:  cycleTitle = "Sort: MemAsc"; break;
                  case PidDescending:   cycleTitle = "Sort: PidDesc"; break;
                  case PidAscending:    cycleTitle = "Sort: PidAsc"; break;
                }

                screen.RequestAnimationFrame();
            });
        }
    });

    screen.Loop(scroll_box);

    running = false;

    if (refresh_thread.joinable()) {
        refresh_thread.join();
    }

    return 0;
}
