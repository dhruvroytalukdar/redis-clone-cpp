cmake_minimum_required(VERSION 3.20)

project(redis_clone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REDIS_SANITIZE "" CACHE STRING
    "Sanitizer to build with: address, thread, undefined, or empty for none")
option(REDIS_BUILD_TESTS "Build the unit tests (downloads doctest)" ON)

# Warnings and sanitizer flags live in one place; every target links this.
add_library(redis_options INTERFACE)
if(MSVC)
  target_compile_options(redis_options INTERFACE /W4 /permissive-)
else()
  target_compile_options(redis_options INTERFACE -Wall -Wextra -Wpedantic)
  if(REDIS_SANITIZE)
    target_compile_options(redis_options INTERFACE
      -fsanitize=${REDIS_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(redis_options INTERFACE -fsanitize=${REDIS_SANITIZE})
  endif()
endif()

# Everything except main(), so the tests can link the same code the server uses.
add_library(redis_core STATIC
  src/config.cpp
  src/socket.cpp
  src/utils.cpp
)
target_include_directories(redis_core PUBLIC src)
target_link_libraries(redis_core PUBLIC redis_options)
if(WIN32)
  target_link_libraries(redis_core PUBLIC ws2_32)
endif()

add_executable(redis src/main.cpp)
target_link_libraries(redis PRIVATE redis_core)

if(REDIS_BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
  )
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)

  add_executable(unit_tests
    tests/unit/test_main.cpp
    tests/unit/config_test.cpp
  )
  target_link_libraries(unit_tests PRIVATE redis_core doctest::doctest)
  add_test(NAME unit_tests COMMAND unit_tests)
endif()
