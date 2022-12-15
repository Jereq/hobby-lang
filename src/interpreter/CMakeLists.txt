add_library(interpreter)
target_sources(
        interpreter
        PRIVATE
        interpreter.cpp
        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/hobbylang/interpreter/interpreter.hpp
)
target_link_libraries(
        interpreter
        PUBLIC
        hobby_lang::project_options
        hobby_lang::project_warnings
        ast
)
