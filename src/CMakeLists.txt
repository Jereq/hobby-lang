find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(ast)
add_subdirectory(interpreter)
add_subdirectory(parser)
add_subdirectory(hobbyc)
