from conan import ConanFile
from conan.tools.cmake import cmake_layout


class HobbyLangConan(ConanFile):
    name = "hobby-lang"
    version = "0.0.1"
    description = "A hobby project to create a programming language."
    url = "https://github.com/Jereq/hobby-lang"
    licence = "MIT"
    author = "Sebastian Larsson (sjereq@gmail.com)"
    topics = "compiler"

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    requires = "catch2/3.2.0", "cli11/2.3.1", "spdlog/1.11.0"

    generators = "CMakeToolchain", "CMakeDeps"

    def layout(self):
        cmake_layout(self)
