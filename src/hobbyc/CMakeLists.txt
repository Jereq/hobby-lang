add_executable(hobbyc)
target_sources(hobbyc PRIVATE main.cpp)

target_link_libraries(
        hobbyc
        PUBLIC
        hobby_lang::project_options
        hobby_lang::project_warnings
        PRIVATE
        ast
        interpreter
        parser
        wasm
        CLI11::CLI11
        fmt::fmt
        spdlog::spdlog
)

target_include_directories(hobbyc PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
