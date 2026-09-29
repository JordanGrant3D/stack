add_rules("mode.debug", "mode.release")

add_requires("ftxui")

target("stack")
set_kind("binary")
add_files("src/*.cpp")
add_includedirs("src")
set_languages("c++20")
add_packages("ftxui")

on_install(function(target)
  local bindir = os.getenv("HOME") .. "/.local/bin"
  os.mkdir(bindir)
  os.cp(target:targetfile(), bindir .. "/")
end)
