cmake_minimum_required(VERSION 3.20)

project(redis_clone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(redis
  src/main.cpp
  src/socket.cpp
  src/utils.cpp
)

if(WIN32)
  target_link_libraries(redis PRIVATE ws2_32)
endif()

if(MSVC)
  target_compile_options(redis PRIVATE /W4 /permissive-)
else()
  target_compile_options(redis PRIVATE -Wall -Wextra -Wpedantic)
endif()
