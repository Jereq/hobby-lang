find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

# Generic test that uses conan libs
add_executable(hobbyc main.cpp)
target_link_libraries(
  hobbyc
  PUBLIC project_options project_warnings
  PRIVATE CLI11::CLI11 fmt::fmt spdlog::spdlog)

target_include_directories(hobbyc PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
