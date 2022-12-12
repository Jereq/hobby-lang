# hobby-lang

A hobby project to create a programming language.

## Dependencies

[Conan](https://conan.io/)

## Setup Instructions (CLion)

    conan install . -s build_type=Debug --build missing -c tools.cmake.cmaketoolchain.presets:max_schema_version=3
    conan install . -s build_type=Release --build missing -c tools.cmake.cmaketoolchain.presets:max_schema_version=3

Then Load CMake Presets.

### Running the tests

You can use the `ctest` command run the tests.

```shell
cd ./build/Debug
ctest
cd ../
```

## Licence ##

This project is provided under the MIT license. See LICENCE.md for the full text.
