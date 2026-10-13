cmake_minimum_required(VERSION 3.20)
project(stack CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
# Add FTXUI directly from the local subdirectory
add_subdirectory(third_party/ftxui)

add_executable(stack 
    src/main.cpp
)

# Link the FTXUI targets directly
target_link_libraries(stack PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)

include(GNUInstallDirs)
install(TARGETS stack DESTINATION ${CMAKE_INSTALL_BINDIR})
