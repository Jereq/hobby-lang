add_library(wasm)
target_sources(
        wasm
        PRIVATE
        wasm.cpp
        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/hobbylang/wasm/wasm.hpp
)
target_link_libraries(
        wasm
        PUBLIC
        hobby_lang::project_options
        hobby_lang::project_warnings
        ast
)
