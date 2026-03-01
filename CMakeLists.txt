cmake_minimum_required(VERSION 3.15)

project(fake-it)

set(CMAKE_CXX_STANDARD 17)

find_package(cxxopts REQUIRED)
find_package(fmt REQUIRED)


find_package(libgit2 REQUIRED)

add_executable(fake-it src/main.cpp)
target_link_libraries(fake-it PRIVATE cxxopts::cxxopts fmt::fmt libgit2::libgit2)
