cmake_minimum_required(VERSION 3.20)
project(nix-tidy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake from: ${Clang_DIR}")

add_library(nix-tidy MODULE
  NixTidyModule.cc
  LockGuardTemporaryCheck.cc
  SqliteNonLiteralCheck.cc
  DanglingCStrCheck.cc
)

target_include_directories(nix-tidy PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS}
)

target_link_libraries(nix-tidy PRIVATE clang-cpp LLVM)

# No "lib" prefix on some platforms; ensure consistent output name
set_target_properties(nix-tidy PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME "nix-tidy"
)
