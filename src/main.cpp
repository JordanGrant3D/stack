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
#include "process.h"
#include <ftxui/ftxui.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/canvas.hpp>
#include "systemMemory.h"
#include "cpuData.h"
#include "cpuCoreStats.h"
#include "sortingMode.h"
#include "utils.h"
using namespace ftxui;
namespace fs = std::filesystem;




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
