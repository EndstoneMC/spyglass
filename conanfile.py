from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class Spyglass(ConanFile):
    name = "spyglass"
    version = "0.1.0"
    package_type = "application"
    settings = "os", "arch", "compiler", "build_type"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("expected-lite/0.9.0")
        self.requires("funchook/1.1.3")
        self.requires("imgui/1.92.8-docking")
        self.requires("libhat/0.11.1")

    def validate(self):
        if self.settings.os != "Windows" or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("spyglass targets Windows x86_64 only")

    def generate(self):
        CMakeDeps(self).generate()
        CMakeToolchain(self).generate()
