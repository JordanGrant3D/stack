add_rules("mode.debug", "mode.release")

add_requires("ftxui")

target("stack")
set_kind("binary")
add_files("src/*.cpp")
set_languages("c++20")
add_packages("ftxui")
