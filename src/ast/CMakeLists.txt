add_library(ast INTERFACE)
target_sources(
        ast
        INTERFACE
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/hobbylang/ast/ast.hpp
)
