add_library(parser)
target_sources(
        parser
        PRIVATE
        parser.cpp
        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/hobbylang/parser/parser.hpp
)
target_link_libraries(
        parser
        PUBLIC
        hobby_lang::project_options
        hobby_lang::project_warnings
        ast
        fmt::fmt
)
