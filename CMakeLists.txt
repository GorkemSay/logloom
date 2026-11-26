cmake_minimum_required(VERSION 3.24)

if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(logloom VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOGLOOM_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(LOGLOOM_BUILD_TESTS "Build Catch2 test suite" ON)

if(LOGLOOM_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(logloom_warnings INTERFACE)
target_compile_options(logloom_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(tools)

if(LOGLOOM_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
  add_subdirectory(tests)
endif()
