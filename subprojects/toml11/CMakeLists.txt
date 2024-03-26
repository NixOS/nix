cmake_minimum_required(VERSION 3.5)
enable_testing()

project(toml11 VERSION 3.8.1)

option(toml11_BUILD_TEST "Build toml tests" OFF)
option(toml11_INSTALL "Install CMake targets during install step." ON)
option(toml11_TEST_WITH_ASAN  "use LLVM address sanitizer" OFF)
option(toml11_TEST_WITH_UBSAN "use LLVM undefined behavior sanitizer" OFF)

option(TOML11_USE_UNRELEASED_TOML_FEATURES
    "use features in toml-lang/toml master while testing" OFF)

include(CheckCXXCompilerFlag)
if("${CMAKE_VERSION}" VERSION_GREATER 3.1)
    set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "Boolean specifying whether compiler specific extensions are requested.")
    if(NOT DEFINED CMAKE_CXX_STANDARD)
        message(FATAL_ERROR "CMAKE_CXX_STANDARD is not defined. \
The C++ standard whose features are requested to *build* all targets. \
Remember that toml11 is a header only library that does NOT require compilation to use.")
    endif()
    set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Boolean describing whether the value of CXX_STANDARD is a requirement.")
else()
    # Manually check for C++11 compiler flag.
    CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
    CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
    CHECK_CXX_COMPILER_FLAG("-std=gnu++11" COMPILER_SUPPORTS_GNU11)
    CHECK_CXX_COMPILER_FLAG("-std=gnu++0x" COMPILER_SUPPORTS_GNU0X)
    if(COMPILER_SUPPORTS_CXX11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    elseif(COMPILER_SUPPORTS_CXXOX)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    elseif(COMPILER_SUPPORTS_GNU11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
    elseif(COMPILER_SUPPORTS_GNU0X)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")
    else()
        if(MSVC)
            if(MSVC_VERSION LESS 1900)
                message(SEND_ERROR "MSVC < 14.0 is not supported. Please update your compiler or use mingw")
            endif()
        else()
            message(SEND_ERROR "The ${CMAKE_CXX_COMPILER} compiler lacks C++11 support. Use another compiler.")
        endif()
    endif()
endif()

if(MSVC)
#     add_definitions("/Zc:__cplusplus")  # define __cplusplus value correctly
    add_definitions("/utf-8")           # enable to use u8"" literal
    if(MSVC_VERSION LESS 1910)
        message(STATUS "MSVC < 1910. DEFINE_CONVERSION_NON_INTRUSIVE is disabled")
        add_definitions(-DTOML11_WITHOUT_DEFINE_NON_INTRUSIVE)
    elseif(MSVC_VERSION LESS 1920)
        add_definitions("/experimental:preprocessor") # MSVC 2017
    else()
        add_definitions("/Zc:preprocessor") # MSVC 2019
    endif()
endif()

# Set some common directories
include(GNUInstallDirs)
set(toml11_install_cmake_dir         ${CMAKE_INSTALL_LIBDIR}/cmake/toml11)
set(toml11_install_include_dir       ${CMAKE_INSTALL_INCLUDEDIR})
set(toml11_config_dir                ${CMAKE_CURRENT_BINARY_DIR}/cmake/)
set(toml11_config                    ${toml11_config_dir}/toml11Config.cmake)
set(toml11_config_version            ${toml11_config_dir}/toml11ConfigVersion.cmake)

add_library(toml11 INTERFACE)
target_include_directories(toml11 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${toml11_install_include_dir}>
)
add_library(toml11::toml11 ALIAS toml11)

# Write config and version config files
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${toml11_config_version}
    VERSION ${toml11_VERSION}
    COMPATIBILITY SameMajorVersion
)

if (toml11_INSTALL)
    configure_package_config_file(
        cmake/toml11Config.cmake.in
        ${toml11_config}
        INSTALL_DESTINATION ${toml11_install_cmake_dir}
        PATH_VARS toml11_install_cmake_dir
    )

    # Install config files
    install(FILES ${toml11_config} ${toml11_config_version}
        DESTINATION ${toml11_install_cmake_dir}
    )

    # Install header files
    install(
        FILES toml.hpp
        DESTINATION "${toml11_install_include_dir}"
    )
    install(
        DIRECTORY "toml"
        DESTINATION "${toml11_install_include_dir}"
        FILES_MATCHING PATTERN "*.hpp"
    )

    # Export targets and install them
    install(TARGETS toml11
        EXPORT toml11Targets
    )
    install(EXPORT toml11Targets
        FILE toml11Targets.cmake
        DESTINATION ${toml11_install_cmake_dir}
        NAMESPACE toml11::
    )
endif()

if (toml11_BUILD_TEST)
    add_subdirectory(tests)
endif ()
