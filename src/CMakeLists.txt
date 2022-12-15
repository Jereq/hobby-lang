find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

add_executable(hobbyc)

target_sources(hobbyc
        PRIVATE
        main.cpp
        interpreter/interpreter.cpp
        parser/parser.cpp
        PRIVATE
        FILE_SET HEADERS FILES
        ast/ast.hpp
        interpreter/interpreter.hpp
        parser/parser.hpp
        )

target_link_libraries(
        hobbyc
        PUBLIC project_options project_warnings
        PRIVATE CLI11::CLI11 fmt::fmt spdlog::spdlog)
target_compile_features(hobbyc PUBLIC cxx_std_20)

target_include_directories(hobbyc PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
